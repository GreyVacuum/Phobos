#include <AnimClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <Memory.h>
#include <Powerups.h>
#include <RulesClass.h>
#include <ScenarioClass.h>
#include <SidebarClass.h>
#include <SuperClass.h>
#include <TriggerClass.h>
#include <TriggerTypeClass.h>
#include <UnitClass.h>
#include <VoxClass.h>
#include <VocClass.h>
#include <WarheadTypeClass.h>
#include <WeaponTypeClass.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>

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
	//
	// Walked backwards by index: the action can retire the object it is called on, and a forward
	// walk would then keep going against a stale end.
	template <typename TAction>
	void ForEachAffectedTechno(AffectedHouse targets, HouseClass* pCollectorHouse, TAction&& action)
	{
		if (!pCollectorHouse || targets == AffectedHouse::None)
			return;

		for (int i = TechnoClass::Array.Count - 1; i >= 0; --i)
		{
			auto const pTechno = TechnoClass::Array[i];

			if (!pTechno || !pTechno->IsAlive || pTechno->InLimbo || !pTechno->Owner)
				continue;

			if (!EnumFunctions::CanTargetHouse(targets, pCollectorHouse, pTechno->Owner))
				continue;

			action(pTechno);
		}
	}

	// Heals every techno the collecting house is allowed to affect, as picked by Crate.HealTargets.
	// Only the health that is actually missing is requested, so nothing is healed past full.
	void HealAffectedTargets(CrateTypeClass* pCrateType, FootClass* pCollector)
	{
		auto const pCollectorHouse = pCollector->Owner;

		if (!pCollectorHouse)
			return;

		auto const pHealWarhead = pCrateType->GetHealWarhead();

		ForEachAffectedTechno(pCrateType->HealTargets.Get(), pCollectorHouse, [&](TechnoClass* pTechno)
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
		});
	}

	// Makes technos the collecting house is allowed to affect take no damage for a while, the way
	// the iron curtain super weapon does. An iron curtain already running for longer is left alone,
	// so a crate can never cut one short.
	void ProtectAffectedTargets(CrateTypeClass* pCrateType, FootClass* pCollector)
	{
		auto const duration = pCrateType->InvulnerabilityDuration.Get();

		ForEachAffectedTechno(pCrateType->InvulnerabilityTargets.Get(), pCollector->Owner, [duration](TechnoClass* pTechno)
		{
			if (pTechno->IronCurtainTimer.GetTimeLeft() < duration)
				pTechno->IronCurtainTimer.Start(duration);
		});
	}

	// Freezes technos the collecting house is allowed to affect, the way an EMP does. A longer
	// freeze already running is left alone.
	void FreezeAffectedTargets(CrateTypeClass* pCrateType, FootClass* pCollector)
	{
		auto const duration = pCrateType->EMPDuration.Get();

		ForEachAffectedTechno(pCrateType->EMPTargets.Get(), pCollector->Owner, [duration](TechnoClass* pTechno)
		{
			if (pTechno->EMPLockRemaining < static_cast<DWORD>(duration))
				pTechno->EMPLockRemaining = static_cast<DWORD>(duration);
		});
	}

	// Promotes technos the collecting house is allowed to affect. Technos already at or above the
	// level are left alone, so a crate can never demote anything, and types that cannot gain
	// experience at all are skipped entirely.
	void PromoteAffectedTargets(CrateTypeClass* pCrateType, FootClass* pCollector)
	{
		const float level = static_cast<float>(std::clamp(pCrateType->VeterancyLevel.Get(), 0, 2));

		ForEachAffectedTechno(pCrateType->VeterancyTargets.Get(), pCollector->Owner, [level](TechnoClass* pTechno)
		{
			auto const pType = pTechno->GetTechnoType();

			if (!pType || !pType->Trainable)
				return;

			if (pTechno->Veterancy.Veterancy < level)
				pTechno->Veterancy.Veterancy = level;
		});
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

	// Spawns Crate.Units.Count units of a randomly chosen Crate.Units entry next to the crate, or
	// next to the collecting techno when Crate.SpawnAtCollector is set.
	void SpawnUnits(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const& units = pCrateType->Units;

		if (units.empty() || !pCollector->Owner)
			return;

		auto const pBaseCell = (pCrateType->SpawnAtCollector.Get() && pCollector->GetCell())
			? pCollector->GetCell() : pCell;

		auto& random = ScenarioClass::Instance->Random;
		const int count = std::max(pCrateType->UnitsCount.Get(), 0);
		int spawned = 0;

		for (int i = 0; i < count; ++i)
		{
			auto const pUnitType = units[random.RandomRanged(0, static_cast<int>(units.size()) - 1)];

			if (!pUnitType)
				continue;

			auto const pUnit = static_cast<UnitClass*>(pUnitType->CreateObject(pCollector->Owner));

			if (!pUnit)
				continue;

			// Look for a free cell nearby so the unit cannot end up inside a wall or a building.
			CellStruct placeCell = CellStruct::Empty;

			MapClass::Instance.NearByLocation(placeCell, pBaseCell->MapCoords, SpeedType::Track, -1,
				MovementZone::Normal, false, 1, 1, true, false, false, true, CellStruct::Empty, false, false);

			if (placeCell == CellStruct::Empty)
			{
				GameDelete(pUnit);
				continue;
			}

			const CoordStruct placeCoords = CellClass::Cell2Coord(placeCell,
				MapClass::Instance.GetCellFloorHeight(CellClass::Cell2Coord(placeCell)));

			// ScenarioInit overrides the placement checks the way the game does when it puts an
			// object on a map.
			++Unsorted::ScenarioInit;
			const bool placed = pUnit->Unlimbo(placeCoords, DirType::North);
			--Unsorted::ScenarioInit;

			if (!placed)
			{
				GameDelete(pUnit);
				continue;
			}

			pUnit->QueueMission(Mission::Guard, true);
			++spawned;
		}

		// Only worth telling the house that its build options may have changed if something appeared.
		if (spawned > 0 && !pCollector->Owner->IsObserver())
			pCollector->Owner->RecheckTechTree = true;
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

	// Applies Crate.SuperWeapon. Anything that cannot do what it was asked to is reported rather
	// than quietly doing something else.
	void ApplySuperWeapon(CrateTypeClass* pCrateType, HouseClass* pHouse)
	{
		auto const superIndex = pCrateType->SuperWeapon.Get();
		auto const pSuper = pHouse->Supers.GetItemOrDefault(superIndex);

		if (!pSuper)
		{
			Debug::Log("[CrateType] [%s] names a Crate.SuperWeapon this house has no slot for, so the "
				"crate does nothing with it.\n", pCrateType->Name.data());
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
			}

			// Unlike setting the ready flag on its own, this writes the recharge timer as well, so
			// the sidebar countdown stays consistent.
			pSuper->SetCharge(100);
			return;
		}

		if (pSuper->IsPresent)
		{
			Debug::Log("[CrateType] [%s] names a super weapon the collecting house already has, so the "
				"crate does nothing with it.\n", pCrateType->Name.data());
			return;
		}

		// OneTime hands over a single use, which is what the vanilla crate that grants a super
		// weapon does. Grant hands over a weapon that recharges as usual.
		if (!pSuper->Grant(pCrateType->SuperWeaponAction.Get() == CrateSuperWeaponAction::OneTime, false, false))
		{
			Debug::Log("[CrateType] [%s] could not hand its super weapon to the collecting house, so "
				"the crate does nothing with it.\n", pCrateType->Name.data());
			return;
		}

		AddSuperWeaponToSidebar(pHouse, superIndex);

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

			pHouse->GiveMoney(ScenarioClass::Instance->Random.RandomRanged(std::min(minMoney, maxMoney),
				std::max(minMoney, maxMoney)));
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

		if (pCrateType->Heals())
			HealAffectedTargets(pCrateType, pCollector);

		if (pCrateType->Protects())
			ProtectAffectedTargets(pCrateType, pCollector);

		if (pCrateType->Freezes())
			FreezeAffectedTargets(pCrateType, pCollector);

		if (pCrateType->Promotes())
			PromoteAffectedTargets(pCrateType, pCollector);

		if (pCrateType->FiresTrigger())
			FireTrigger(pCrateType, pCell, pCollector);

		if (pCrateType->Reveal.Get())
			MapClass::Instance.Reveal(pHouse);

		// Cloaking is a state on the collecting techno itself, so it is applied directly instead of
		// through the techno filter: a crate is picked up by exactly one thing.
		if (pCrateType->CloakCollector.Get())
		{
			if (auto const pType = pCollector->GetTechnoType(); pType && pType->Cloakable)
				pCollector->Cloak(false);
		}

		if (pCrateType->Reshroud.Get())
			MapClass::Instance.Reshroud(pHouse);
	}

	// Plays the crate's animation, sound and EVA line. This is feedback only - none of it changes
	// the game state, so it is played whether or not an effect applied.
	void PlayFeedback(CrateTypeClass* pCrateType, CellClass* pCell, FootClass* pCollector)
	{
		auto const pHouse = pCollector->Owner;

		CoordStruct coords = CellClass::Cell2Coord(pCell->MapCoords,
			MapClass::Instance.GetCellFloorHeight(CellClass::Cell2Coord(pCell->MapCoords)));

		if (auto const pAnimType = pCrateType->Anim.Get())
		{
			if (auto const pAnim = GameCreate<AnimClass>(pAnimType, coords))
				pAnim->Owner = pHouse;
		}

		if (pCrateType->Sound.Get() >= 0)
			VocClass::PlayAt(pCrateType->Sound.Get(), coords, nullptr);

		if (pCrateType->EVA.Get() >= 0 && pHouse && pHouse->IsCurrentPlayer())
			VoxClass::PlayIndex(pCrateType->EVA.Get());
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

#pragma region Crate collection

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

	if (!PendingCustomCrate)
	{
		// A crate the game picks a type for on pickup may become a custom one. The threshold is
		// the game's own test for exactly those crates.
		if (pCell->OverlayData >= CrateHelpers::PowerupCount)
		{
			auto const pRolled = CrateHelpers::PickChanceCrateType();

			// A crate type that is not collectible here must not be handed to the player just
			// because the roll landed on it; the game keeps its own crate instead.
			if (pRolled && (pCell->LandType != LandType::Water || pRolled->CollectOnWater.Get()))
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
