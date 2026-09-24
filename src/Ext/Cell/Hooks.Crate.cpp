#include <AircraftClass.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <InfantryTypeClass.h>
#include <MapClass.h>
#include <Memory.h>
#include <Utilities/GeneralUtils.h>
#include <Powerups.h>
#include <RulesClass.h>
#include <ScenarioClass.h>
#include <SidebarClass.h>
#include <SuperClass.h>
#include <TiberiumClass.h>
#include <TriggerClass.h>
#include <TriggerTypeClass.h>
#include <UnitClass.h>
#include <VoxClass.h>
#include <VocClass.h>
#include <WarheadTypeClass.h>
#include <WeaponTypeClass.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <numbers>
#include <vector>

#include "Body.h"

#include <Ext/Bullet/Body.h>
#include <Ext/TechnoType/Body.h>
#include <New/Type/CrateSource.h>
#include <New/Type/CrateTypeClass.h>
#include <Utilities/EnumFunctions.h>
#include <Utilities/Macro.h>

// Custom crate (powerup) types ([CrateTypes]) - see docs/New-or-Enhanced-Logics.md.
//
// A crate on a cell is a vanilla crate overlay plus a powerup index kept in
// CellClass::OverlayData. Indices 0-18 are the vanilla powerup slots and 19 marks "no powerup".
// A custom crate stores its Crate.TypeID - which by construction is above all of those - in that
// same field, and records the id in CellExt::CustomCrateTypeID as well so the cell can be repaired
// if anything clears its overlay.
//
// The effect of such a crate is applied by this file instead of the vanilla powerup switch. The
// crate is recognised while CellClass::CollectCrate is still deciding on the powerup, and the
// effect itself is applied after the crate has been removed from the cell - at which point the
// vanilla effect switch has not run yet, so returning the function's success epilogue skips it.
//
// Custom crates depend on neither the [Powerups] table nor any specific vanilla powerup slot being
// configured, and a crate type with nothing set does nothing rather than falling back to something
// the author did not ask for.

namespace CrateHelpers
{
	// How many vanilla powerup slots the game has. Its value is also the cell OverlayData
	// threshold from which the game rolls a crate's type when it is collected.
	constexpr int PowerupCount = static_cast<int>(std::size(Powerups::Effects));
	static_assert(PowerupCount == 19, "Unexpected number of vanilla powerup slots.");

	// Powerup index that has no entry in the effect switch's jump table (which only covers the
	// slots below it), so the switch takes its default branch and applies nothing. Used to keep the
	// vanilla effect from firing alongside a custom crate.
	constexpr int NoEffectPowerup = PowerupCount - 1;
	static_assert(NoEffectPowerup == 18, "Unexpected last vanilla powerup index.");

	// The value MapClass::PlacePowerupCrate treats as "do not store a type at all", i.e. roll one
	// when the crate is collected. It is also what a map trigger passes to ask for that crate.
	constexpr int RandomPowerup = PowerupCount + 1;
	static_assert(RandomPowerup == CrateSource::Random, "CrateSource::Random must match the engine.");

	// A [CrateTypes] entry is passed to PlacePowerupCrate as its Crate.TypeID, and those start above
	// the vanilla crate indices so the two can never be confused.
	static_assert(CrateSource::FirstTypeID > RandomPowerup, "Crate ids must start above the vanilla crates.");

	// Returns the crate type of a custom crate placed on a cell, or nullptr when the cell does not
	// hold one. Requiring both the recorded id and the OverlayData marker keeps a stale id
	// harmless: every crate placement overwrites OverlayData and every crate removal resets it, so
	// a crate placed on the cell afterwards can never satisfy both conditions.
	CrateTypeClass* GetPlacedCrateType(CellClass* pCell)
	{
		if (pCell->OverlayData != CellExt::CustomCrateOverlayMarker)
			return nullptr;

		auto const pExt = CellExt::TryFetch(pCell);

		return pExt ? CrateSource::GetByTypeID(pExt->CustomCrateTypeID) : nullptr;
	}

	// The CrateType this techno type is configured with.
	int GetConfiguredCrateSource(const TechnoTypeClass* pType)
	{
		auto const pExt = TechnoTypeExt::TryFetch(pType);

		return pExt ? pExt->CrateType.Get() : CrateSource::Default;
	}

	// Maps a CrateType value onto the powerup index MapClass::PlacePowerupCrate takes.
	// A crate type id, CrateSource::Random and a vanilla crate index all pass through unchanged:
	// the first is what PlacePowerupCrate stores for the crate, the rest are the game's own values.
	// fallback is used for a type that does not name a crate.
	Powerup ToPowerup(int source, int fallback)
	{
		if (source == CrateSource::Default || source == CrateSource::Invalid)
			source = fallback;

		return static_cast<Powerup>(source);
	}

	// Rolls whether a crate whose type the game picks on pickup becomes a custom one, and which.
	// Crate.Chance is a probability, so the sum of all of them is how likely such a crate is to be
	// a custom one at all; the individual values only decide the split between them.
	// Returns the crate type, or nullptr to leave the crate to the game.
	// Consumes no randomness at all when nothing set a usable chance, so the default configuration
	// cannot affect the game's random crate distribution.
	CrateTypeClass* PickChanceCrateType()
	{
		// A crate type that nothing can name would be unusable even if it were rolled, so it does
		// not take part.
		auto const weightOf = [](CrateTypeClass* pCrateType)
		{
			return pCrateType->TypeID.isset() ? std::clamp(pCrateType->Chance.Get(), 0.0, 1.0) : 0.0;
		};

		double total = 0.0;

		for (auto const& pCrateType : CrateTypeClass::Array)
			total += weightOf(pCrateType.get());

		if (total <= 0.0)
			return nullptr;

		auto& random = ScenarioClass::Instance->Random;

		if (random.RandomDouble() >= std::min(total, 1.0))
			return nullptr;

		const double roll = random.RandomDouble() * total;
		double cumulative = 0.0;

		for (auto const& pCrateType : CrateTypeClass::Array)
		{
			cumulative += weightOf(pCrateType.get());

			if (roll < cumulative)
				return pCrateType.get();
		}

		return nullptr;
	}

	// Runs action for every techno the given house filter matches, relative to the collecting house.
	// When pCenter is given and radius is positive, only technos within that many cells of the
	// center are included. The optional type filters narrow the audience further: an empty allow
	// list admits every type, and a type on the disallow list is always left out, even when it is
	// also on the allow list.
	//
	// Walked backwards by index: the action can retire the object it is called on, and a forward
	// walk would then keep going against a stale end.
	template <typename TAction>
	void ForEachAffectedTechno(AffectedHouse targets, HouseClass* pCollectorHouse,
		CellClass* pCenter, int radius, TAction&& action,
		const std::vector<TechnoTypeClass*>* pAllowTypes = nullptr,
		const std::vector<TechnoTypeClass*>* pDisallowTypes = nullptr)
	{
		if (!pCollectorHouse || targets == AffectedHouse::None)
			return;

		const bool limited = pCenter && radius > 0;
		const CellStruct center = limited ? pCenter->MapCoords : CellStruct::Empty;

		const auto isFilteredOut = [&](const TechnoTypeClass* pType)
		{
			if (pDisallowTypes
				&& std::find(pDisallowTypes->begin(), pDisallowTypes->end(), pType) != pDisallowTypes->end())
			{
				return true;
			}

			return pAllowTypes && !pAllowTypes->empty()
				&& std::find(pAllowTypes->begin(), pAllowTypes->end(), pType) == pAllowTypes->end();
		};

		for (int i = TechnoClass::Array.Count - 1; i >= 0; --i)
		{
			auto const pTechno = TechnoClass::Array[i];

			if (!pTechno || !pTechno->IsAlive || pTechno->InLimbo || !pTechno->Owner)
				continue;

			if (!EnumFunctions::CanTargetHouse(targets, pCollectorHouse, pTechno->Owner))
				continue;

			if (isFilteredOut(pTechno->GetTechnoType()))
				continue;

			if (limited)
			{
				auto const pTechnoCell = pTechno->GetCell();

				if (!pTechnoCell)
					continue;

				const auto& here = pTechnoCell->MapCoords;

				if (std::max(std::abs(here.X - center.X), std::abs(here.Y - center.Y)) > radius)
					continue;
			}

			action(pTechno);
		}
	}

	// True when any [CrateTypes] entry names this super weapon type via Crate.SuperWeapon. The
	// lists hold the loaded types themselves, so this is a plain pointer comparison and does not
	// depend on the engine keeping SuperWeaponTypeClass::ArrayIndex anywhere in particular. Crate
	// types are few and this is checked rarely, so a linear walk is fine.
	bool IsCrateReferencedSuper(SuperWeaponTypeClass* pType)
	{
		if (!pType)
			return false;

		for (auto const& pCrateType : CrateTypeClass::Array)
		{
			for (auto const& pListed : pCrateType->SuperWeapon)
			{
				if (pListed == pType)
					return true;
			}
		}

		return false;
	}

	// True when some BuildingType in the rules grants this super weapon, i.e. the engine's tech
	// recheck has a legitimate reason to manage - and take away - this weapon.
	bool IsBuildingGrantedSuper(SuperWeaponTypeClass* pType)
	{
		if (!pType)
			return false;

		const int count = SuperWeaponTypeClass::Array.Count;

		const auto grants = [&](int index)
		{
			return index >= 0 && index < count && SuperWeaponTypeClass::Array[index] == pType;
		};

		for (int i = 0; i < BuildingTypeClass::Array.Count; ++i)
		{
			if (auto const pBuildingType = BuildingTypeClass::Array[i])
			{
				if (grants(pBuildingType->SuperWeapon) || grants(pBuildingType->SuperWeapon2))
					return true;
			}
		}

		return false;
	}

	// The radius a filtered effect searches around the crate's cell: the crate's own setting, or
	// the [CrateRules] -> CrateRadius default when the crate does not set one. The engine reads
	// that value as leptons - its reader at 0x66BA90 multiplies the INI value by 256 before
	// storing it at RulesClass+0x1732 - so it converts back to cells here. Note the setting lives
	// in [CrateRules], not [General].
	int ResolveRadius(const Nullable<int>& key)
	{
		if (key.isset())
			return std::max(key.Get(), 0);

		const int leptons = *reinterpret_cast<const int*>(
			reinterpret_cast<const char*>(RulesClass::Instance) + 0x1732);

		return std::max(leptons / 256, 0);
	}

	// The multiplier an upgrade crate hands out: the crate's own setting, or the [Powerups]
	// parameter of the matching vanilla crate type when it does not set one (1.5 for armor, 2.0
	// for firepower and 1.2 for speed in the stock rules).
	double ResolveMultiplier(const Nullable<double>& key, const char* pPowerupName)
	{
		if (key.isset())
			return key.Get();

		for (int i = 0; i < 19; ++i)
		{
			if (!_strcmpi(Powerups::Effects[i], pPowerupName))
				return Powerups::Arguments[i];
		}

		return 1.0;
	}

	// The three vanilla upgrade crates. Armor and firepower apply to every affected techno, speed
	// only to things that move on the ground or water - the vanilla code checks the foot flag and
	// skips aircraft, whose speed the flight logic owns. A techno whose stat has already been
	// multiplied is left alone, exactly as the vanilla code skips anything whose multiplier is no
	// longer 1.0, so collecting the crate again can never compound the upgrade.
	enum class UpgradeKind { Armor, Firepower, Speed };

	void ApplyUpgrade(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector,
		UpgradeKind kind)
	{
		AffectedHouse targets = AffectedHouse::None;
		int radius = 0;
		double multiplier = 1.0;
		bool allowStack = false;
		double maxMultiplier = 0.0;
		const char* pPowerupName = nullptr;
		const std::vector<TechnoTypeClass*>* pAllowTypes = nullptr;
		const std::vector<TechnoTypeClass*>* pDisallowTypes = nullptr;

		switch (kind)
		{
		case UpgradeKind::Armor:
			targets = pCrateType->ArmorTargets.Get();
			radius = ResolveRadius(pCrateType->ArmorRadius);
			multiplier = ResolveMultiplier(pCrateType->ArmorMultiplier, "Armor");
			allowStack = pCrateType->ArmorAllowStack.Get();

			if (pCrateType->ArmorMaxMultiplier.isset())
				maxMultiplier = pCrateType->ArmorMaxMultiplier.Get();

			pPowerupName = "Armor";
			pAllowTypes = &pCrateType->ArmorAllowTypes;
			pDisallowTypes = &pCrateType->ArmorDisallowTypes;
			break;
		case UpgradeKind::Firepower:
			targets = pCrateType->FirepowerTargets.Get();
			radius = ResolveRadius(pCrateType->FirepowerRadius);
			multiplier = ResolveMultiplier(pCrateType->FirepowerMultiplier, "Firepower");
			allowStack = pCrateType->FirepowerAllowStack.Get();

			if (pCrateType->FirepowerMaxMultiplier.isset())
				maxMultiplier = pCrateType->FirepowerMaxMultiplier.Get();

			pPowerupName = "Firepower";
			pAllowTypes = &pCrateType->FirepowerAllowTypes;
			pDisallowTypes = &pCrateType->FirepowerDisallowTypes;
			break;
		case UpgradeKind::Speed:
			targets = pCrateType->SpeedTargets.Get();
			radius = ResolveRadius(pCrateType->SpeedRadius);
			multiplier = ResolveMultiplier(pCrateType->SpeedMultiplier, "Speed");
			allowStack = pCrateType->SpeedAllowStack.Get();

			if (pCrateType->SpeedMaxMultiplier.isset())
				maxMultiplier = pCrateType->SpeedMaxMultiplier.Get();

			pPowerupName = "Speed";
			pAllowTypes = &pCrateType->SpeedAllowTypes;
			pDisallowTypes = &pCrateType->SpeedDisallowTypes;
			break;
		}

		// The new value for an already present multiplier: multiplied again while stacking is
		// allowed - capped when a maximum is set - and left alone otherwise, which is the vanilla
		// behaviour. A stat at exactly 1.0 has never been upgraded, so it always gets its first.
		auto const upgrade = [&](double current)
		{
			if (current != 1.0 && !allowStack)
				return current;

			double result = current * multiplier;

			if (maxMultiplier > 0.0)
				result = std::min(result, maxMultiplier);

			return result;
		};

		int upgraded = 0;

		ForEachAffectedTechno(targets, pCollector->Owner, pCell, radius, [&](TechnoClass* pTechno)
		{
			switch (kind)
			{
			case UpgradeKind::Armor:
			{
				const double result = upgrade(pTechno->ArmorMultiplier);

				if (result == pTechno->ArmorMultiplier)
					return;

				pTechno->ArmorMultiplier = result;
				break;
			}
			case UpgradeKind::Firepower:
			{
				const double result = upgrade(pTechno->FirepowerMultiplier);

				if (result == pTechno->FirepowerMultiplier)
					return;

				pTechno->FirepowerMultiplier = result;
				break;
			}
			case UpgradeKind::Speed:
			{
				if (!(pTechno->AbstractFlags & AbstractFlags::Foot)
					|| pTechno->WhatAmI() == AbstractType::Aircraft)
				{
					return;
				}

				auto const pFoot = static_cast<FootClass*>(pTechno);
				const double result = upgrade(pFoot->SpeedMultiplier);

				if (result == pFoot->SpeedMultiplier)
					return;

				pFoot->SpeedMultiplier = result;
				break;
			}
			}

			pTechno->Flash(100);
			++upgraded;
		}, pAllowTypes, pDisallowTypes);
		if (upgraded > 0)
		{
			Debug::Log("[CrateType] [%s] upgraded %d technos of house %d (%s x%f%s within %d "
				"cells).\n", pCrateType->Name.data(), upgraded, pCollector->Owner->ArrayIndex,
				pPowerupName, multiplier, allowStack ? " stacking" : "", radius);
		}
	}

	// Heals every techno the collecting house is allowed to affect, as picked by Crate.HealTargets.
	// Only the health that is actually missing is requested, so nothing is healed past full.
	void HealAffectedTargets(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const pCollectorHouse = pCollector->Owner;

		if (!pCollectorHouse)
			return;

		auto const pHealWarhead = pCrateType->GetHealWarhead();
		const int radius = ResolveRadius(pCrateType->HealRadius);

		ForEachAffectedTechno(pCrateType->HealTargets.Get(), pCollectorHouse, pCell, radius, [&](TechnoClass* pTechno)
		{
			auto const pType = pTechno->GetTechnoType();

			if (!pType)
				return;

			const int missingHealth = pType->Strength
				- static_cast<int>(pType->Strength * pTechno->GetHealthPercentage());

			if (missingHealth <= 0)
				return;

			int damage = -missingHealth;

			pTechno->ReceiveDamage(&damage, 0, pHealWarhead, pCollector, true, true, pCollectorHouse);
			pTechno->Flash(100);
		}, &pCrateType->HealAllowTypes, &pCrateType->HealDisallowTypes);
	}

	// Makes technos the collecting house is allowed to affect take no damage for a while, the way
	// the iron curtain super weapon does. An iron curtain already running for longer is left alone,
	// so a crate can never cut one short.
	//
	// Note that this shares the iron curtain's expiry behaviour as well: infantry and other organic
	// units die when their timer runs out, exactly as they do under the super weapon.
	void ProtectAffectedTargets(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const duration = pCrateType->InvulnerabilityDuration.Get();
		const int radius = ResolveRadius(pCrateType->InvulnerabilityRadius);

		ForEachAffectedTechno(pCrateType->InvulnerabilityTargets.Get(), pCollector->Owner, pCell, radius, [duration](TechnoClass* pTechno)
		{
			if (pTechno->IronCurtainTimer.GetTimeLeft() < duration)
				pTechno->IronCurtainTimer.Start(duration);
		}, &pCrateType->InvulnerabilityAllowTypes, &pCrateType->InvulnerabilityDisallowTypes);
	}

	// Freezes technos the collecting house is allowed to affect, the way an EMP does. A longer
	// freeze already running is left alone.
	void FreezeAffectedTargets(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const duration = pCrateType->EMPDuration.Get();
		const int radius = ResolveRadius(pCrateType->EMPRadius);

		ForEachAffectedTechno(pCrateType->EMPTargets.Get(), pCollector->Owner, pCell, radius, [duration](TechnoClass* pTechno)
		{
			if (pTechno->EMPLockRemaining < static_cast<DWORD>(duration))
				pTechno->EMPLockRemaining = static_cast<DWORD>(duration);
		}, &pCrateType->EMPAllowTypes, &pCrateType->EMPDisallowTypes);
	}

	// Cloaks technos the collecting house is allowed to affect. Types that cannot cloak at all are
	// left as they are, since the engine's cloak state machine has nothing to work with on them.
	void CloakAffectedTargets(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		const int radius = ResolveRadius(pCrateType->CloakRadius);

		ForEachAffectedTechno(pCrateType->CloakTargets.Get(), pCollector->Owner, pCell, radius, [](TechnoClass* pTechno)
		{
			if (auto const pType = pTechno->GetTechnoType(); pType && pType->Cloakable)
				pTechno->Cloak(false);
		}, &pCrateType->CloakAllowTypes, &pCrateType->CloakDisallowTypes);
	}

	// Promotes - or demotes - technos the collecting house is allowed to affect. Without
	// Crate.Veterancy.Stack, the level is the rank to end up at: a positive one promotes technos
	// below it, a negative one demotes technos above the rank it names, and neither moves a techno
	// the wrong way. With Stack, the level is instead how much experience to add, and a negative
	// one takes that much away with a rookie as the floor. Nothing is filtered by Trainable here:
	// the vanilla crate does not check it either, and a type that cannot use veterancy simply
	// ignores the rank.
	void PromoteAffectedTargets(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		const int level = std::clamp(pCrateType->VeterancyLevel.Get(), -2, 2);
		const bool stack = pCrateType->VeterancyStack.Get();
		const int radius = ResolveRadius(pCrateType->VeterancyRadius);
		// The rank a negative level names: -1 means "no higher than a veteran".
		const float demotedTo = static_cast<float>(-level);
		int affected = 0;

		ForEachAffectedTechno(pCrateType->VeterancyTargets.Get(), pCollector->Owner, pCell, radius, [&](TechnoClass* pTechno)
		{
			if (stack)
			{
				pTechno->Veterancy.Add(static_cast<double>(level));

				// Add only clamps the ceiling, so a demotion has to stop at a rookie itself.
				if (pTechno->Veterancy.IsNegative())
					pTechno->Veterancy.Veterancy = 0.0f;
			}
			else if (level >= 0)
			{
				if (pTechno->Veterancy.Veterancy < static_cast<float>(level))
					pTechno->Veterancy.Veterancy = static_cast<float>(level);
			}
			else if (pTechno->Veterancy.Veterancy > demotedTo)
			{
				pTechno->Veterancy.Veterancy = demotedTo;
			}

			pTechno->Flash(100);
			++affected;
		}, &pCrateType->VeterancyAllowTypes, &pCrateType->VeterancyDisallowTypes);

		// One line per collection, so a test that affects nobody can be told apart from one that
		// never ran: the house, the radius, the level and the count are all in the log.
		Debug::Log("[CrateType] [%s] rank change for house %d: level %d%s within %d cells hit %d "
			"technos.\n", pCrateType->Name.data(), pCollector->Owner->ArrayIndex, level,
			stack ? " (stacking)" : "", radius, affected);
	}

	// Grows or clears tiberium around the crate's cell, the way the terrain type that spawns
	// tiberium does it: the engine's own IncreaseTiberium handles the overlay, the density and the
	// growth bookkeeping behind it, and CanTiberiumGerminate keeps the cells that cannot take ore
	// out of the running. Everything is driven by the synchronized RNG, so every machine grows the
	// same cells.
	void ModifyTiberium(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		const int index = pCrateType->Tiberium.Get();

		if (index < 0 || index >= TiberiumClass::Array.Count)
			return;

		auto const pTiberium = TiberiumClass::Array[index];

		if (!pTiberium)
			return;

		// Note that a radius of 0 means the crate's own cell here, unlike the other effects, where
		// it would mean the whole map - covering a map in ore is not a feature anyone wants.
		const int radius = ResolveRadius(pCrateType->TiberiumRadius);
		const CellStruct base = pCell->MapCoords;
		const auto cells = GeneralUtils::AdjacentCellsInRange(static_cast<unsigned int>(radius));

		if (pCrateType->ClearsTiberium())
		{
			const int requested = std::max(pCrateType->TiberiumClearAmount.Get(), 0);
			int cleared = 0;

			for (auto const& offset : cells)
			{
				auto const pTarget = MapClass::Instance.TryGetCellAt(base + offset);

				if (!pTarget)
					continue;

				const int contained = pTarget->GetContainedTiberiumValue();

				if (contained <= 0)
					continue;

				// The cell's ore is stored as a value; the amount is in shares of the type's own
				// value, which is what ReduceTiberium takes.
				auto const pContained = TiberiumClass::Array.GetItemOrDefault(
					pTarget->GetContainedTiberiumIndex());
				const int value = pContained ? std::max(pContained->Value, 1) : 1;
				const int shares = contained / value;
				const int amount = requested > 0 ? std::min(requested, shares) : shares;

				if (amount > 0)
				{
					pTarget->ReduceTiberium(amount);
					++cleared;
				}
			}

			if (cleared > 0)
			{
				Debug::Log("[CrateType] [%s] cleared tiberium from %d cells for house %d.\n",
					pCrateType->Name.data(), cleared, pCollector->Owner->ArrayIndex);
			}

			return;
		}

		// Growing: the cells that can take this type first, then the picks from them.
		std::vector<CellClass*> candidates;

		for (auto const& offset : cells)
		{
			if (auto const pTarget = MapClass::Instance.TryGetCellAt(base + offset))
			{
				if (pTarget->CanTiberiumGerminate(pTiberium))
					candidates.push_back(pTarget);
			}
		}

		if (candidates.empty())
		{
			Debug::Log("[CrateType] [%s] found no cell within %d cells that can grow [%s]. Note that "
				"the crate itself is an overlay on its own cell, so a Crate.Tiberium.Radius of 0 can "
				"never grow anything.\n",
				pCrateType->Name.data(), radius, pTiberium->ID);

			return;
		}

		const int maxStage = std::max(pTiberium->NumFrames - 1, 0);
		const int stage = pCrateType->TiberiumStage.Get() >= 0
			? std::clamp(pCrateType->TiberiumStage.Get(), 0, maxStage) : maxStage;

		int count = std::max(pCrateType->TiberiumCount.Get(), 0);

		if (count == 0 || count > static_cast<int>(candidates.size()))
			count = static_cast<int>(candidates.size());

		auto& random = ScenarioClass::Instance->Random;

		for (int i = static_cast<int>(candidates.size()) - 1; i > 0; --i)
			std::swap(candidates[i], candidates[random.RandomRanged(0, i)]);

		int grown = 0;

		for (int i = 0; i < count; ++i)
		{
			if (candidates[i]->IncreaseTiberium(index, stage))
				++grown;
		}

		if (grown > 0)
		{
			Debug::Log("[CrateType] [%s] grew [%s] at stage %d on %d cells for house %d.\n",
				pCrateType->Name.data(), pTiberium->ID, stage, grown,
				pCollector->Owner->ArrayIndex);
		}
	}

	// Runs the actions of the named map trigger, unconditionally and as if its events had all
	// occurred. Trigger instances are created on demand, the same way the game creates them the
	// first time one of their events fires.
	void FireTrigger(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		if (auto const pTriggerType = pCrateType->Trigger.Get())
		{
			if (auto const pTrigger = TriggerClass::GetInstance(pTriggerType))
				pTrigger->FireActions(pCollector, pCell->MapCoords);
		}
	}

	// Walks the cells around the base cell ring by ring, near to far, and returns the first cell
	// the check accepts. A direction of 0-360 narrows each ring to the sector of that many degrees
	// around it (from north, clockwise, in screen coordinates where +Y is south), +- half the arc.
	// -1 searches every direction in fixed order; -3 walks each ring in a shuffled order, so the
	// accepted cell lands in whichever direction chance favours. The shuffle uses the
	// multiplayer-synchronized scenario RNG, so every machine walks the cells in the same order
	// and picks the same spot. Returns CellStruct::Empty when nothing within the range works.
	template <typename TCheck>
	CellStruct FindCellNear(CellStruct base, int minDist, int maxDist, int direction, int arc,
		TCheck&& passes)
	{
		auto& random = ScenarioClass::Instance->Random;

		// A direction of 0-360 narrows the ring to the sector of that many degrees around it.
		auto const passesDirection = [&](CellStruct candidate)
		{
			if (direction < 0)
				return true;

			const int dx = candidate.X - base.X;
			const int dy = candidate.Y - base.Y;

			// The base cell itself has no direction.
			if (dx == 0 && dy == 0)
				return false;

			const double angle = std::fmod(std::atan2(static_cast<double>(dx),
				static_cast<double>(-dy)) * (180.0 / std::numbers::pi) + 360.0, 360.0);

			double offset = std::fabs(angle - direction);

			if (offset > 180.0)
				offset = 360.0 - offset;

			return offset <= arc / 2.0;
		};

		for (int r = minDist; r <= maxDist; ++r)
		{
			if (direction == -3)
			{
				std::vector<CellStruct> ring;

				for (int dy = -r; dy <= r; ++dy)
				{
					for (int dx = -r; dx <= r; ++dx)
					{
						if (std::max(std::abs(dx), std::abs(dy)) == r)
							ring.push_back(CellStruct { static_cast<short>(base.X + dx), static_cast<short>(base.Y + dy) });
					}
				}

				for (int i = int(ring.size()) - 1; i > 0; --i)
					std::swap(ring[i], ring[random.RandomRanged(0, i)]);

				for (auto const& candidate : ring)
				{
					if (passes(candidate))
						return candidate;
				}

				continue;
			}

			for (int dy = -r; dy <= r; ++dy)
			{
				for (int dx = -r; dx <= r; ++dx)
				{
					if (std::max(std::abs(dx), std::abs(dy)) != r)
						continue;

					CellStruct candidate { static_cast<short>(base.X + dx), static_cast<short>(base.Y + dy) };

					if (!passesDirection(candidate))
						continue;

					if (passes(candidate))
						return candidate;
				}
			}
		}

		return CellStruct::Empty;
	}

	// Finds the cell to place the crate's building on. Two checks per candidate:
	//
	// - The game's own placement validation, which covers the foundation against other buildings
	//   and the terrain. It does not consider units standing on the foundation - and it has an
	//   "always placeable" type flag - so it alone would let the building land on the collector
	//   and crush it, or displace the infantry around.
	// - Every foundation cell is checked for occupants, so nothing can end up under the building.
	CellStruct FindBuildingPlaceCell(BuildingTypeClass* pType, CellStruct base, HouseClass* pHouse,
		int minDist, int maxDist, int direction, int arc)
	{
		auto const passes = [&](CellStruct candidate)
		{
			if (!pType->CanPlaceHere(&candidate, pHouse))
				return false;

			// The foundation data is offset-terminated, the way every other use of it in the
			// codebase walks it.
			for (auto pOffset = pType->FoundationData; ; ++pOffset)
			{
				if (*pOffset == CellStruct { 0x7FFF, 0x7FFF })
					break;

				auto const pFoundationCell = MapClass::Instance.TryGetCellAt(
					CellStruct { static_cast<short>(candidate.X + pOffset->X), static_cast<short>(candidate.Y + pOffset->Y) });

				if (!pFoundationCell
					|| pFoundationCell->GetBuilding()
					|| pFoundationCell->GetUnit(false)
					|| pFoundationCell->GetInfantry(false))
				{
					return false;
				}
			}

			return true;
		};

		return FindCellNear(base, minDist, maxDist, direction, arc, passes);
	}

	// Finds a cell for one of the crate's spawned units, infantry or aircraft. Aircraft only need
	// the cell to be on the map - they are placed at cruise height and never touch the ground.
	// Ground units get a cell of their own, so nothing ends up crushed or displaced: no building,
	// vehicle or infantry on it, and land or water matching the type's Naval flag.
	CellStruct FindUnitPlaceCell(TechnoTypeClass* pType, CellStruct base, int minDist, int maxDist,
		int direction, int arc, bool atCruiseHeight)
	{
		auto const passes = [&](CellStruct candidate)
		{
			auto const pCell = MapClass::Instance.TryGetCellAt(candidate);

			if (!pCell)
				return false;

			if (atCruiseHeight)
				return true;

			if (pType->Naval != (pCell->LandType == LandType::Water))
				return false;

			return !pCell->GetBuilding() && !pCell->GetUnit(false) && !pCell->GetInfantry(false);
		};

		return FindCellNear(base, minDist, maxDist, direction, arc, passes);
	}

	// Spawns Crate.Units.Count entries of the randomly chosen Crate.Units list, placed by the same
	// direction/distance search the building placement uses - next to the crate, or next to the
	// collecting techno when Crate.SpawnAtCollector is set. Entries may be vehicles, infantry or
	// aircraft; the entry's type decides how it is spawned, with aircraft coming out at cruise
	// height. All of them come out with the rank of Crate.Units.Level.
	void SpawnUnits(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const& units = pCrateType->Units;

		if (units.empty() || !pCollector->Owner)
			return;

		auto const pBaseCell = (pCrateType->SpawnAtCollector.Get() && pCollector->GetCell())
			? pCollector->GetCell() : pCell;

		auto& random = ScenarioClass::Instance->Random;
		const auto& counts = pCrateType->UnitsCount;
		const auto& rollChances = pCrateType->UnitsRollChances;
		const auto& weightsData = pCrateType->UnitsRandomWeightsData;
		const int level = std::clamp(pCrateType->UnitsLevel.Get(), 0, 2);
		const int minDist = std::max(pCrateType->UnitsMinDist.Get(), 0);
		const int maxDist = std::max(pCrateType->UnitsMaxDist.Get(), 0);
		const int direction = pCrateType->UnitsDirection.Get();
		const int arc = std::clamp(pCrateType->UnitsArc.Get(45), 1, 360);
		int spawned = 0;

		// The entries this collection is going to spawn. Three ways to decide, in order:
		//
		// - Units.Count listing one value per entry: each entry spawns exactly that many times,
		//   no randomness involved.
		// - RollChances: the weighted rolls system the LimboDelivery super weapon logic uses. One
		//   independent 0-1 roll per entry of the list, and each roll that passes picks an entry
		//   with its weights group - RandomWeights0 upwards, one group per draw, falling back to
		//   the last group that is present. The same roll decides the pick, the way the super
		//   weapon handler does it.
		// - Otherwise Units.Count draws, from an even share or with the first weights group.
		std::vector<TechnoTypeClass*> picks;

		if (counts.size() == units.size())
		{
			for (size_t i = 0; i < units.size(); ++i)
			{
				for (int k = 0; k < std::max(counts[i], 0); ++k)
					picks.push_back(units[i]);
			}
		}
		else if (rollChances.size())
		{
			for (size_t i = 0; i < rollChances.size(); ++i)
			{
				const double roll = random.RandomDouble();

				if (roll > rollChances[i])
					continue;

				const bool haveGroup = weightsData.size()
					&& weightsData[std::min(weightsData.size() - 1, i)].size();
				const size_t groupIndex = weightsData.size()
					? std::min(weightsData.size() - 1, i) : 0;

				const int index = haveGroup
					? GeneralUtils::ChooseOneWeighted(roll, &weightsData[groupIndex])
					: random.RandomRanged(0, static_cast<int>(units.size()) - 1);

				if (index >= 0 && index < static_cast<int>(units.size()))
					picks.push_back(units[index]);
			}
		}
		else
		{
			const int count = counts.empty() ? 1 : std::max(counts[0], 0);

			for (int i = 0; i < count; ++i)
			{
				const double roll = random.RandomDouble();

				const int index = weightsData.size()
					? GeneralUtils::ChooseOneWeighted(roll, &weightsData[0])
					: random.RandomRanged(0, static_cast<int>(units.size()) - 1);

				if (index >= 0 && index < static_cast<int>(units.size()))
					picks.push_back(units[index]);
			}
		}

		// The shared placement: a cell of the entry's own found by the same direction/distance
		// search the building placement uses, the ScenarioInit-guarded Unlimbo the game itself
		// uses to put objects on the map, a guard mission and the crate's rank.
		auto const placeAndSetup = [&](TechnoClass* pSpawned, TechnoTypeClass* pSpawnType, bool atCruiseHeight)
		{
			auto const placeCell = FindUnitPlaceCell(pSpawnType, pBaseCell->MapCoords,
				minDist, maxDist, direction, arc, atCruiseHeight);

			if (placeCell == CellStruct::Empty)
				return false;

			CoordStruct placeCoords = CellClass::Cell2Coord(placeCell,
				MapClass::Instance.GetCellFloorHeight(CellClass::Cell2Coord(placeCell)));

			// Aircraft go straight to their cruise height instead of starting on the ground.
			if (atCruiseHeight)
				placeCoords.Z += RulesClass::Instance->FlightLevel;

			// ScenarioInit overrides the placement checks the way the game does when it puts an
			// object on a map.
			++Unsorted::ScenarioInit;
			const bool placed = pSpawned->Unlimbo(placeCoords, DirType::North);
			--Unsorted::ScenarioInit;

			if (!placed)
				return false;

			pSpawned->QueueMission(Mission::Guard, true);

			if (level > 0 && pSpawned->Veterancy.Veterancy < static_cast<float>(level))
				pSpawned->Veterancy.Veterancy = static_cast<float>(level);

			return true;
		};

		for (auto const pType : picks)
		{
			if (!pType)
				continue;

			switch (pType->WhatAmI())
			{
			case AbstractType::UnitType:
			{
				auto const pUnit = static_cast<UnitClass*>(pType->CreateObject(pCollector->Owner));

				if (!pUnit)
					continue;

				if (placeAndSetup(pUnit, pType, false))
					++spawned;
				else
					GameDelete(pUnit);

				break;
			}
			case AbstractType::InfantryType:
			{
				auto const pInfantry = static_cast<InfantryClass*>(pType->CreateObject(pCollector->Owner));

				if (!pInfantry)
					continue;

				if (placeAndSetup(pInfantry, pType, false))
					++spawned;
				else
					GameDelete(pInfantry);

				break;
			}
			case AbstractType::AircraftType:
			{
				auto const pAircraft = static_cast<AircraftClass*>(pType->CreateObject(pCollector->Owner));

				if (!pAircraft)
					continue;

				if (placeAndSetup(pAircraft, pType, true))
					++spawned;
				else
					GameDelete(pAircraft);

				break;
			}
			default:
				break;
			}
		}

		// Anything that found no cell is reported rather than quietly missing, the way the other
		// placement paths report their failures.
		if (spawned < static_cast<int>(picks.size()))
			Debug::Log("[CrateType] [%s] placed %d of %d Crate.Units - the rest found no cell "
				"within %d-%d cells of the base.\n", pCrateType->Name.data(), spawned,
				picks.size(), minDist, maxDist);

		// Only worth telling the house that its build options may have changed if something appeared.
		if (spawned > 0 && !pCollector->Owner->IsObserver())
			pCollector->Owner->RecheckTechTree = true;
	}

	// Spawns the crate's building next to the base cell, for the collecting house. The sequence
	// mirrors what the game does for the map trigger that builds a building: the construction
	// mission has to be entered and left before the building goes on the map, or Unlimbo would
	// route it into BuildingClass::Place instead.
	void SpawnBuilding(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const pBuildingType = pCrateType->Building.Get();

		if (!pBuildingType || !pCollector->Owner)
			return;

		auto const pBaseCell = (pCrateType->SpawnAtCollector.Get() && pCollector->GetCell())
			? pCollector->GetCell() : pCell;

		auto const placeCell = FindBuildingPlaceCell(pBuildingType, pBaseCell->MapCoords,
			pCollector->Owner, std::max(pCrateType->BuildingMinDist.Get(), 0),
			std::max(pCrateType->BuildingMaxDist.Get(), 0), pCrateType->BuildingDirection.Get(),
			std::clamp(pCrateType->BuildingArc.Get(45), 1, 360));

		if (placeCell == CellStruct::Empty)
		{
			Debug::Log("[CrateType] [%s] found no cell its Crate.Building may be placed on within "
				"%d-%d cells, so the crate builds nothing.\n", pCrateType->Name.data(),
				std::max(pCrateType->BuildingMinDist.Get(), 0),
				std::max(pCrateType->BuildingMaxDist.Get(), 0));
			return;
		}

		CoordStruct placeCoords = CellClass::Cell2Coord(placeCell,
			MapClass::Instance.GetCellFloorHeight(CellClass::Cell2Coord(placeCell)));

		if (auto const pBuilding = static_cast<BuildingClass*>(pBuildingType->CreateObject(pCollector->Owner)))
		{
			// Set before the building goes on the map, otherwise Unlimbo calls
			// BuildingClass::Place with it still in its construction mission.
			pBuilding->QueueMission(Mission::Construction, false);
			pBuilding->NextMission();

			if (!pBuilding->ForceCreate(placeCoords))
			{
				pBuilding->UnInit();

				Debug::Log("[CrateType] [%s] could not place its Crate.Building at the cell it "
					"found, so the crate builds nothing.\n", pCrateType->Name.data());
				return;
			}

			// Without buildup the building is completed at once; with it, the construction mission
			// that was entered above plays the buildup sequence and finishes on its own.
			if (!pCrateType->BuildingBuildup.Get())
			{
				pBuilding->BeginMode(BStateType::Idle);
				pBuilding->QueueMission(Mission::Guard, false);
				pBuilding->NextMission();
				pBuilding->Place(false);
			}

			if (!pCollector->Owner->IsObserver())
				pCollector->Owner->RecheckTechTree = true;

			Debug::Log("[CrateType] [%s] built [%s] for house %d (%s).\n", pCrateType->Name.data(),
				pBuildingType->ID, pCollector->Owner->ArrayIndex,
				pCrateType->BuildingBuildup.Get() ? "with buildup" : "completed");
		}
	}

	// Adds a newly obtained super weapon to this machine's sidebar, which is what makes it usable at
	// all - the engine only records that the house has it. The vanilla crate that grants a super
	// weapon does the same. The sidebar belongs to the player at this computer, so it is only
	// touched for the house that player owns.
	void AddSuperWeaponToSidebar(HouseClass* pHouse, int superIndex)
	{
		if (!pHouse->IsCurrentPlayer())
			return;

		if (SidebarClass::Instance.AddCameo(AbstractType::Special, superIndex))
		{
			SidebarClass::Instance.RepaintSidebar(
				SidebarClass::GetObjectTabIdx(AbstractType::Special, superIndex, 0));
		}
	}

	// Makes a permanently granted super weapon invisible to the tech recheck's removal scan.
	//
	// The recheck takes away every present super weapon that no building of the house grants, and
	// its scan only skips one-time weapons. Vanilla never hands out a permanent super weapon
	// without a building behind it - the crate and the trigger action both grant one-time weapons -
	// so there is nothing to copy verbatim. Clearing CanHold opts the instance out of that scan
	// through the same flag the scan itself reads, without touching index spaces or call sites.
	void OptOutOfTechRecheck(SuperClass* pSuper)
	{
		pSuper->CanHold = false;
	}

	// Applies Crate.SuperWeapon: one of the listed weapons is drawn when the crate is collected, so
	// a crate can offer a whole set without needing one crate type per weapon. Anything that cannot
	// do what it was asked to is reported rather than quietly doing something else.
	void ApplySuperWeapon(CrateTypeClass* pCrateType, HouseClass* pHouse)
	{
		auto const& weapons = pCrateType->SuperWeapon;

		if (weapons.empty())
			return;

		// Only the listed weapons this house actually has a slot for take part in the draw, so one
		// ungrantable entry does not turn an otherwise fine roll into a wasted crate. The state of
		// the houses' super weapon slots is synchronized, so every machine narrows the list to the
		// same entries and the synchronized RNG below keeps the draw identical.
		std::vector<SuperWeaponTypeClass*> usable;
		usable.reserve(weapons.size());

		for (auto const& pListed : weapons)
		{
			if (pListed && pHouse->Supers.GetItemOrDefault(
				SuperWeaponTypeClass::Array.FindItemIndex(pListed)))
			{
				usable.push_back(pListed);
			}
		}

		if (usable.empty())
		{
			Debug::Log("[CrateType] [%s] has no Crate.SuperWeapon entry this house has a slot for, so "
				"the crate does nothing with it.\n", pCrateType->Name.data());
			return;
		}

		auto const pType = usable[ScenarioClass::Instance->Random.RandomRanged(0,
			static_cast<int>(usable.size()) - 1)];

		const int superIndex = SuperWeaponTypeClass::Array.FindItemIndex(pType);
		auto const pSuper = pHouse->Supers.GetItemOrDefault(superIndex);

		if (!pSuper)
		{
			Debug::Log("[CrateType] [%s] drew [%s] from Crate.SuperWeapon, but this house has no slot "
				"for it, so the crate does nothing with it.\n", pCrateType->Name.data(), pType->ID);
			return;
		}

		if (pCrateType->SuperWeaponAction.Get() == CrateSuperWeaponAction::Charge)
		{
			if (!pSuper->IsPresent)
			{
				// SetCharge does nothing at all for a weapon the house does not have.
				if (!pSuper->Grant(false, false, false))
				{
					Debug::Log("[CrateType] [%s] could not hand its super weapon to the collecting "
						"house, so the crate does nothing with it.\n", pCrateType->Name.data());
					return;
				}

				AddSuperWeaponToSidebar(pHouse, superIndex);
				OptOutOfTechRecheck(pSuper);
			}

			// Unlike setting the ready flag on its own, this writes the recharge timer as well, so
			// the sidebar countdown stays consistent.
			pSuper->SetCharge(100);

			Debug::Log("[CrateType] [%s] handed [%s] to house %d, ready to fire.\n",
				pCrateType->Name.data(), pSuper->Type->ID, pHouse->ArrayIndex);
			return;
		}

		if (pSuper->IsPresent)
		{
			Debug::Log("[CrateType] [%s] names a super weapon house %d already has, so the crate "
				"does nothing with it.\n", pCrateType->Name.data(), pHouse->ArrayIndex);
			return;
		}

		// OneTime hands over a single use, which is what the vanilla crate that grants a super
		// weapon does. Grant hands over a weapon that recharges as usual.
		const bool oneTime = pCrateType->SuperWeaponAction.Get() == CrateSuperWeaponAction::OneTime;

		if (!pSuper->Grant(oneTime, false, false))
		{
			Debug::Log("[CrateType] [%s] could not hand its super weapon to the collecting house, so "
				"the crate does nothing with it.\n", pCrateType->Name.data());
			return;
		}

		AddSuperWeaponToSidebar(pHouse, superIndex);

		// A one-time weapon is already exempt from the tech recheck's removal scan; a permanent one
		// opts out through its CanHold flag instead, or any finished building takes it away again.
		if (!oneTime)
			OptOutOfTechRecheck(pSuper);

		Debug::Log("[CrateType] [%s] handed [%s] to house %d (%s).\n",
			pCrateType->Name.data(), pSuper->Type->ID, pHouse->ArrayIndex,
			oneTime ? "one-time use" : "permanent grant");

		// A weapon that is not meant to be usable straight away starts charging from empty instead,
		// which is what turns OneTime into a single use the player has to wait for.
		if (!pCrateType->SuperWeaponStartsReady.Get())
			pSuper->SetCharge(0);
	}

	// Applies every effect the crate type asks for, in a fixed order. Effects are independent, so
	// setting several of them runs all of them.
	void ApplyEffects(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const pHouse = pCollector->Owner;

		if (!pHouse)
			return;

		const CoordStruct coords = CellClass::Cell2Coord(pCell->MapCoords,
			MapClass::Instance.GetCellFloorHeight(CellClass::Cell2Coord(pCell->MapCoords)));

		if (pCrateType->GivesMoney())
		{
			const int minMoney = pCrateType->GetMoneyMin();
			const int maxMoney = pCrateType->GetMoneyMax();
			int amount = ScenarioClass::Instance->Random.RandomRanged(std::min(minMoney, maxMoney),
				std::max(minMoney, maxMoney));

			// A negative range takes money away, but never past an empty wallet.
			if (amount < 0)
				amount = -std::min(-amount, static_cast<int>(pHouse->Available_Money()));

			if (amount != 0)
				pHouse->TransactMoney(amount);
		}

		if (pCrateType->GivesSuperWeapon())
			ApplySuperWeapon(pCrateType, pHouse);

		if (auto const pWeapon = pCrateType->Weapon.Get())
		{
			// Mind control projectiles need a firer to attach to, so they cannot be detonated on a
			// bare cell.
			if (pWeapon->Projectile && !(pWeapon->Warhead && pWeapon->Warhead->MindControl && pWeapon->LimboLaunch))
			{
				BulletExt::Detonate(coords, pCollector, pWeapon->Damage, pHouse, pCell,
					pWeapon->Bright, pWeapon, pWeapon->Warhead);
			}
			else
			{
				Debug::Log("[CrateType] [%s] cannot detonate its Crate.Weapon here, so the crate does not "
					"fire it.\n", pCrateType->Name.data());
			}
		}

		if (!pCrateType->Units.empty())
			SpawnUnits(pCrateType, pCell, pCollector);

		if (pCrateType->Building.Get())
			SpawnBuilding(pCrateType, pCell, pCollector);

		if (pCrateType->Heals())
			HealAffectedTargets(pCrateType, pCell, pCollector);

		if (pCrateType->Protects())
			ProtectAffectedTargets(pCrateType, pCell, pCollector);

		if (pCrateType->Freezes())
			FreezeAffectedTargets(pCrateType, pCell, pCollector);

		if (pCrateType->Promotes())
			PromoteAffectedTargets(pCrateType, pCell, pCollector);

		if (pCrateType->UpgradesArmor())
			ApplyUpgrade(pCrateType, pCell, pCollector, UpgradeKind::Armor);

		if (pCrateType->UpgradesFirepower())
			ApplyUpgrade(pCrateType, pCell, pCollector, UpgradeKind::Firepower);

		if (pCrateType->UpgradesSpeed())
			ApplyUpgrade(pCrateType, pCell, pCollector, UpgradeKind::Speed);

		if (pCrateType->Cloaks())
			CloakAffectedTargets(pCrateType, pCell, pCollector);

		if (pCrateType->FiresTrigger())
			FireTrigger(pCrateType, pCell, pCollector);

		if (pCrateType->Reveal.Get())
			MapClass::Instance.Reveal(pHouse);

		if (pCrateType->Reshroud.Get())
			MapClass::Instance.Reshroud(pHouse);

		if (pCrateType->SpawnsTiberium() || pCrateType->ClearsTiberium())
			ModifyTiberium(pCrateType, pCell, pCollector);
	}

	// Plays the crate's animation, sound and EVA line. This is feedback only - none of it changes
	// the game state, so it is played whether or not an effect applied.
	void PlayFeedback(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const pHouse = pCollector->Owner;

		CoordStruct coords = CellClass::Cell2Coord(pCell->MapCoords,
			MapClass::Instance.GetCellFloorHeight(CellClass::Cell2Coord(pCell->MapCoords)));

		// The vanilla crate type whose feedback this crate borrows, if any. The explicit keys
		// always win; the borrowed type only fills what they leave unset. The pickup animation
		// comes from the engine's own per-type table, and the vanilla crate code plays an EVA
		// line for exactly three types - the upgrade ones.
		AnimTypeClass* pDefaultAnimType = nullptr;
		const char* pDefaultEVA = nullptr;

		if (pCrateType->DefaultRemindType.Get() >= 0)
		{
			const int type = pCrateType->DefaultRemindType.Get();
			const int animIndex = Powerups::Anims[type];

			if (animIndex >= 0 && animIndex < AnimTypeClass::Array.Count)
				pDefaultAnimType = AnimTypeClass::Array[animIndex];

			if (!_strcmpi(Powerups::Effects[type], "Armor"))
				pDefaultEVA = "EVA_UnitArmorUpgraded";
			else if (!_strcmpi(Powerups::Effects[type], "Speed"))
				pDefaultEVA = "EVA_UnitSpeedUpgraded";
			else if (!_strcmpi(Powerups::Effects[type], "FirePower"))
				pDefaultEVA = "EVA_UnitFirePowerUpgraded";
		}

		auto const pAnimType = pCrateType->Anim.Get()
			? pCrateType->Anim.Get() : pDefaultAnimType;

		if (pAnimType)
		{
			if (auto const pAnim = GameCreate<AnimClass>(pAnimType, coords))
				pAnim->Owner = pHouse;
		}

		if (pCrateType->Sound.Get() >= 0)
			VocClass::PlayAt(pCrateType->Sound.Get(), coords, nullptr);

		if (pCrateType->EVA.Get() >= 0 && pHouse && pHouse->IsCurrentPlayer())
			VoxClass::PlayIndex(pCrateType->EVA.Get());
		else if (pDefaultEVA && pHouse && pHouse->IsCurrentPlayer())
			VoxClass::Play(pDefaultEVA);
	}
}

// Set while CellClass::CollectCrate is collecting a custom crate: read before the crate is
// removed, consumed after the vanilla effect switch has been skipped. Collecting a crate cannot
// re-enter the collection of another crate, and the value is cleared before the effect runs.
static CrateTypeClass* PendingCustomCrate = nullptr;

#pragma region Crate placement

// MapClass::PlacePowerupCrate stores the crate's powerup index in the cell's OverlayData.
// A [CrateTypes] entry is passed to PlacePowerupCrate as its Crate.TypeID, which is also what a map
// trigger action passes, so both routes arrive here identically. This code path is only reached when
// the caller asked for a specific crate type - requesting 20 (the vanilla "roll on pickup" crate)
// never stores OverlayData at all - so vanilla crate placement is left completely untouched.
DEFINE_HOOK(0x56BFF9, MapClass_PlacePowerupCrate_CustomCrate, 0x6)
{
	enum { ReturnTrue = 0x56BFFF };

	GET(CellClass*, pCell, EAX);
	GET(int, powerup, EDX);

	if (powerup >= CrateSource::FirstTypeID)
	{
		auto const pCrateType = CrateSource::GetByTypeID(powerup);
		auto const pExt = CellExt::TryFetch(pCell);

		// A crate type the mod removed since the crate was placed would leave an id that no longer
		// names anything; fall back rather than record it.
		if (pExt)
			pExt->CustomCrateTypeID = pCrateType ? powerup : -1;

		if (!pExt || !pCrateType)
		{
			Debug::Log("[CrateType] no [CrateTypes] entry uses Crate.TypeID=%d, placing a vanilla crate "
				"whose type is rolled on pickup instead.\n", powerup);
		}

		// Keep OverlayData inside the range the rest of the game expects for a crate.
		pCell->OverlayData = CellExt::CustomCrateOverlayMarker;

		return ReturnTrue;
	}

	return 0;
}

// UnitClass::ReceiveDamage places a crate when the dying unit's type has CarriesCrate=yes.
// Originally this is hardcoded to a crate whose type is rolled on pickup; route it through the
// type's CrateType instead when it names one.
//
// This hook never returns 0, so the stolen bytes - which end mid-instruction - are never replayed.
DEFINE_HOOK(0x73844A, UnitClass_ReceiveDamage_CarriesCrateType, 0x6)
{
	enum { SkipGameCode = 0x738457 };

	GET(CellStruct, cell, EAX);
	GET(UnitClass*, pUnit, ESI);

	const int source = CrateHelpers::GetConfiguredCrateSource(pUnit->GetTechnoType());

	MapClass::Instance.PlacePowerupCrate(cell, CrateHelpers::ToPowerup(source, CrateSource::Random));

	return SkipGameCode;
}

// A BuildingClass with CrateBeneath=yes places a crate on its own cell when it is removed.
// Originally this is hardcoded to a money crate when CrateBeneathIsMoney is set and to a crate
// whose type is rolled on pickup otherwise. A CrateType that names a crate overrides both.
DEFINE_HOOK(0x442215, BuildingClass_CrateBeneath_CrateType, 0x7)
{
	enum { SkipGameCode = 0x442226 };

	GET(BuildingTypeClass*, pBldType, EDX);
	GET(BuildingClass*, pBuilding, EBX);

	const int source = CrateHelpers::GetConfiguredCrateSource(pBldType);

	// Left unnamed, the game decides between a money crate and a rolled one on its own.
	if (source == CrateSource::Default || source == CrateSource::Invalid)
		return 0;

	const bool placed = MapClass::Instance.PlacePowerupCrate(pBuilding->GetMapCoords(),
		CrateHelpers::ToPowerup(source, CrateSource::Random));

	R->AL(placed);

	return SkipGameCode;
}

#pragma endregion

#pragma region Super weapon persistence

// HouseClass::RecheckTechTree runs whenever a building is placed, sold or the power level changes,
// and removes every present super weapon that no building of the house grants anymore by calling
// SuperClass::Lose - this is the only place Lose is ever called, and the sidebar keeps a cameo
// visible exactly while the weapon's SuperClass::IsPresent stays set. A crate-granted super weapon
// has no building behind it, so the completion of any building would take it away again; the
// vanilla crate avoids this only because its grant is marked one-time and one-time weapons are
// exempt from the scan.
//
// Kept here are super weapons that some [CrateTypes] entry lists in Crate.SuperWeapon, and super
// weapons that no BuildingType can grant at all - for those, Lose has nothing legitimate to revoke.
// The trade-off is that a building granting the same weapon no longer removes it when sold either,
// so a mod that wants a removable super weapon should not hand that same one out with a crate.
DEFINE_HOOK(0x50B169, HouseClass_RecheckTechTree_KeepCrateGrantedSupers, 0x5)
{
	enum { ContinueAfterLose = 0x50B16E };

	GET(SuperClass*, pSuper, ECX);
	GET(HouseClass*, pHouse, EBP);

	if (pSuper && pHouse)
	{
		const bool fromCrate = CrateHelpers::IsCrateReferencedSuper(pSuper->Type);
		const bool buildingGranted = CrateHelpers::IsBuildingGrantedSuper(pSuper->Type);

		if (pSuper->IsOneTime || (fromCrate || !buildingGranted))
		{
			Debug::Log("[CrateType] keeping super weapon [%s] of house %d from the tech recheck "
				"(%s).\n", pSuper->Type ? pSuper->Type->ID : "<null>", pHouse->ArrayIndex,
				pSuper->IsOneTime ? "one-time weapon" :
				fromCrate ? "used by a crate" : "no building grants it");

			// Pretend Lose() returned false, which makes the caller skip the sidebar cleanup.
			R->AL(false);
			return ContinueAfterLose;
		}

		Debug::Log("[CrateType] tech recheck revokes [%s] of house %d (building-backed weapon, "
			"its granting building is gone).\n", pSuper->Type ? pSuper->Type->ID : "<null>",
			pHouse->ArrayIndex);
	}

	return 0;
}

#pragma endregion

// Runs while CellClass::CollectCrate is still deciding which powerup the crate grants, before the
// crate is removed. CellClass::RemoveCrate clears the cell's OverlayData, so the custom crate has
// to be recognised here and remembered for the effect below.
//
// The stolen bytes are the game's own "OverlayData below the random-crate range means use it as-is,
// otherwise roll a powerup" test; returning 0 replays them, using whatever EBX was left at.
DEFINE_HOOK(0x481ACE, CellClass_CollectCrate_PrepareCustomCrate, 0x5)
{
	enum { SkipCollection = 0x483389 };

	GET(CellClass*, pCell, ESI);

	// The engine keeps the collector in EDI for the whole function: it is loaded from the single
	// argument right at the top (mov edi, [ebp+arg_0] at 0x481A0F) and reloaded after every call
	// that clobbers it, so EDI still holds it here. There is no collector on the stack at this
	// point - this hook runs mid-function, where [esp+4] is just some stack value.
	GET(FootClass*, pCollector, EDI);

	// A crate that was placed as a specific [CrateTypes] entry keeps that entry.
	PendingCustomCrate = CrateHelpers::GetPlacedCrateType(pCell);

	if (PendingCustomCrate && !PendingCustomCrate->CollectOnWater.Get()
		&& pCell->LandType == LandType::Water)
	{
		// Deliberately not collected: the crate stays where it is and expires on its own, rather
		// than turning into a payout the author did not ask for.
		PendingCustomCrate = nullptr;
		return SkipCollection;
	}

	if (PendingCustomCrate && !PendingCustomCrate->CanBeCollectedBy(pCollector ? pCollector->Owner : nullptr))
	{
		// Deliberately not collected either: a house that Crate.AllowedHouses does not list walks
		// past the crate, which stays where it is for one that is listed to pick up.
		PendingCustomCrate = nullptr;
		return SkipCollection;
	}

	if (!PendingCustomCrate)
	{
		// A crate the game picks a type for on pickup may become a custom one. The threshold is
		// the game's own test for exactly those crates.
		if (pCell->OverlayData >= CrateHelpers::PowerupCount)
		{
			auto const pRolled = CrateHelpers::PickChanceCrateType();

			// A crate type that is not collectible here must not be handed to the player just
			// because the roll landed on it; the game keeps its own crate instead.
			if (pRolled && (pCell->LandType != LandType::Water || pRolled->CollectOnWater.Get())
				&& pRolled->CanBeCollectedBy(pCollector ? pCollector->Owner : nullptr))
			{
				if (auto const pExt = CellExt::TryFetch(pCell))
				{
					pExt->CustomCrateTypeID = pRolled->TypeID.Get(-1);
					pCell->OverlayData = CellExt::CustomCrateOverlayMarker;
					PendingCustomCrate = pRolled;
				}
			}
		}
	}

	// Keep the vanilla effect from firing alongside the custom one: NoEffectPowerup has no entry in
	// the effect switch's jump table, so the switch does nothing. Without a custom crate nothing is
	// changed here, and the game rolls the powerup below exactly as it always did.
	if (PendingCustomCrate)
		R->EBX(CrateHelpers::NoEffectPowerup);

	return 0;
}

// The game degrades a crate to money when it is collected on water and the rolled powerup is not
// allowed there. That check reads the powerup index, which says nothing about a custom crate, so
// it is skipped while one is being collected - a custom crate decides for itself instead.
//
// Hooking the water table lookup rather than the comparison before it keeps the hook independent
// of flags set by replayed stolen bytes: the test after it sets its own.
DEFINE_HOOK(0x481D5B, CellClass_CollectCrate_SkipWaterResetForCustomCrate, 0x6)
{
	enum { SkipWaterReset = 0x481D6B };

	return PendingCustomCrate ? SkipWaterReset : 0;
}

// Runs right before the vanilla effect switch, after the crate has been removed from the cell.
// A custom crate takes over here: its effects are applied and the function returns success without
// touching any vanilla powerup slot. When no custom crate is being collected this is a no-op and
// the vanilla switch runs as usual.
DEFINE_HOOK(0x481DCD, CellClass_CollectCrate_CustomCrate, 0x5)
{
	enum { ReturnTrue = 0x483389 };

	auto const pCrateType = PendingCustomCrate;

	if (!pCrateType)
		return 0;

	// Cleared before the effects run so nothing they do can observe or clobber it.
	PendingCustomCrate = nullptr;

	GET(CellClass*, pCell, ESI);
	GET(FootClass*, pCollector, EDI);

	// The crate is consumed either way, so drop the record before anything else can observe it.
	if (auto const pExt = CellExt::TryFetch(pCell))
		pExt->CustomCrateTypeID = -1;

	CrateHelpers::ApplyEffects(pCrateType, pCell, pCollector);
	CrateHelpers::PlayFeedback(pCrateType, pCell, pCollector);

	return ReturnTrue;
}

#pragma endregion
