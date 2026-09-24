#include "CrateTypeClass.h"

#include "CrateSource.h"

#include <HouseClass.h>
#include <HouseTypeClass.h>
#include <Powerups.h>
#include <RulesClass.h>
#include <TiberiumClass.h>
#include <TriggerTypeClass.h>
#include <WarheadTypeClass.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>

template <>
const char* Enumerable<CrateTypeClass>::GetMainSection()
{
	return "CrateTypes";
}

CrateTypeClass* CrateTypeClass::FindByTypeID(int typeID)
{
	for (auto const& pCrateType : CrateTypeClass::Array)
	{
		if (pCrateType->TypeID.Get(-1) == typeID)
			return pCrateType.get();
	}

	return nullptr;
}

namespace CrateSource
{
	CrateTypeClass* GetByTypeID(int typeID)
	{
		return IsCrateTypeID(typeID) ? CrateTypeClass::FindByTypeID(typeID) : nullptr;
	}

	int Parse(const char* pValue)
	{
		if (INIClass::IsBlank(pValue))
			return Default;

		// A [CrateTypes] entry takes precedence over a vanilla crate name, so a crate type can
		// deliberately shadow one of the vanilla names.
		if (auto const pCrateType = CrateTypeClass::Find(pValue))
			return pCrateType->TypeID.Get(-1);

		if (!_strcmpi(pValue, "random"))
			return Random;

		// A number selects a crate type by its Crate.TypeID, or a vanilla crate by its index.
		if (isdigit(static_cast<unsigned char>(*pValue)))
		{
			const int number = atoi(pValue);

			if (IsCrateTypeID(number))
				return GetByTypeID(number) ? number : Invalid;

			return (number >= 0 && number < FirstTypeID) ? number : Invalid;
		}

		for (size_t i = 0; i < std::size(Powerups::Effects); ++i)
		{
			if (Powerups::Effects[i] && !_strcmpi(pValue, Powerups::Effects[i]))
				return static_cast<int>(i);
		}

		return Invalid;
	}
}

namespace CrateSuperWeaponAction
{
	int Parse(const char* pValue)
	{
		static const std::pair<const char*, int> Names[] =
		{
			{ "grant", Grant },
			{ "onetime", OneTime },
			{ "charge", Charge },
		};

		for (auto const& [name, value] : Names)
		{
			if (!_strcmpi(pValue, name))
				return value;
		}

		return -1;
	}
}

namespace
{
	// Resolves the value of a Direction key (Crate.Building.Direction, Crate.Units.Direction).
	// Returns the degrees from north, clockwise, for one of the eight compass names or their long
	// forms, -1 for Any, -3 for Random, and -2 when the value names nothing.
	int ParseDirection(const char* pValue)
	{
		static const std::pair<const char*, int> Names[] =
		{
			{ "n", 0 }, { "north", 0 },
			{ "ne", 45 }, { "northeast", 45 },
			{ "e", 90 }, { "east", 90 },
			{ "se", 135 }, { "southeast", 135 },
			{ "s", 180 }, { "south", 180 },
			{ "sw", 225 }, { "southwest", 225 },
			{ "w", 270 }, { "west", 270 },
			{ "nw", 315 }, { "northwest", 315 },
			{ "any", -1 }, { "none", -1 }, { "all", -1 },
			{ "random", -3 },
		};

		for (const auto& [name, degrees] : Names)
		{
			if (!_strcmpi(pValue, name))
				return degrees;
		}

		return -2;
	}

	// Index of the [Tiberiums] entry carrying this name, or -1 when nothing does. The list lives
	// in the engine and is what the INI section is read against.
	int FindTiberiumByName(const char* pName)
	{
		for (int i = 0; i < TiberiumClass::Array.Count; ++i)
		{
			if (auto const pTiberium = TiberiumClass::Array[i])
			{
				if (!_strcmpi(pName, pTiberium->ID))
					return i;
			}
		}

		return -1;
	}

	// Index of a vanilla crate type named the way the [Powerups] list names its effects, or -1
	// when nothing carries that name. The list lives in the engine and is what the INI list is
	// read against, so both sides agree on the names.
	int FindPowerupTypeByName(const char* pName)
	{
		for (int i = 0; i < 19; ++i)
		{
			if (!_strcmpi(pName, Powerups::Effects[i]))
				return i;
		}

		return -1;
	}
}

// Both bounds keep their sign so that a negative range takes money away instead of granting it.
int CrateTypeClass::GetMoneyMin() const
{
	return this->MoneyMin.Get(0);
}

int CrateTypeClass::GetMoneyMax() const
{
	return this->MoneyMax.Get(this->GetMoneyMin());
}

WarheadTypeClass* CrateTypeClass::GetHealWarhead() const
{
	// The healing damage call used to reuse C4Warhead; keep it as the fallback so an unconfigured
	// healing crate still behaves like the vanilla crate healing.
	auto const pWarhead = this->HealWarhead.Get();

	return pWarhead ? pWarhead : RulesClass::Instance->C4Warhead;
}

bool CrateTypeClass::HasEffect() const
{
	return this->GivesMoney()
		|| this->GivesSuperWeapon()
		|| this->Weapon.Get() != nullptr
		|| !this->Units.empty()
				|| this->Building.Get() != nullptr
		|| this->Heals()
		|| this->Protects()
		|| this->Freezes()
		|| this->Promotes()
		|| this->Cloaks()
		|| this->UpgradesArmor()
		|| this->UpgradesFirepower()
		|| this->UpgradesSpeed()
		|| this->FiresTrigger()
		|| this->Reveal.Get()
		|| this->Reshroud.Get()
		|| this->Tiberium.Get() >= 0;
}

bool CrateTypeClass::CanBeCollectedBy(HouseClass* pHouse) const
{
	if (this->AllowedHouses.empty() || !pHouse)
		return true;

	auto const& houses = this->AllowedHouses;
	return std::find(houses.begin(), houses.end(), pHouse->Type) != houses.end();
}

void CrateTypeClass::LoadFromINI(CCINIClass* pINI)
{
	const char* section = this->Name;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	this->TypeID.Read(exINI, section, "Crate.TypeID");

	// The id is what a CrateType key and a map trigger use to select this crate type, so an
	// unusable one means nothing can select the crate at all. Everything below reports rather than
	// guessing what was meant.
	if (this->TypeID.isset() && this->TypeID.Get() < CrateSource::FirstTypeID)
	{
		Debug::Log("[CrateType] [%s] has Crate.TypeID=%d, which is in the vanilla crate range. Use "
			"%d or higher.\n", section, this->TypeID.Get(), CrateSource::FirstTypeID);

		this->TypeID.Reset();
	}

	if (!this->TypeID.isset())
	{
		Debug::Log("[CrateType] [%s] has no usable Crate.TypeID, so nothing can select its crate. "
			"Set one, %d or higher.\n", section, CrateSource::FirstTypeID);
	}
	else
	{
		for (auto const& pOther : CrateTypeClass::Array)
		{
			if (pOther.get() != this && pOther->TypeID.Get(-1) == this->TypeID.Get())
			{
				Debug::Log("[CrateType] [%s] and [%s] both use Crate.TypeID=%d, so only the first can "
					"be selected by it.\n", section, pOther->Name.data(), this->TypeID.Get());
				break;
			}
		}
	}

	this->MoneyMin.Read(exINI, section, "Crate.Money.Min");
	this->MoneyMax.Read(exINI, section, "Crate.Money.Max");

	this->SuperWeapon.Read(exINI, section, "Crate.SuperWeapon");

	// Names one of the three things a crate can do with its super weapon. A value that resolves to
	// nothing is reported and keeps the default instead of quietly behaving like another one.
	const bool hasSuperWeaponAction = exINI.ReadString(section, "Crate.SuperWeaponAction")
		&& !INIClass::IsBlank(exINI.value());

	if (hasSuperWeaponAction)
	{
		const int action = CrateSuperWeaponAction::Parse(exINI.value());

		if (action >= 0)
			this->SuperWeaponAction = action;
		else
			Debug::INIParseFailed(section, "Crate.SuperWeaponAction", exINI.value(),
				"Expected Grant, OneTime or Charge");
	}

	this->SuperWeaponStartsReady.Read(exINI, section, "Crate.SuperWeaponStartsReady");

	this->Weapon.Read<true>(exINI, section, "Crate.Weapon");

	this->Units.Read(exINI, section, "Crate.Units");
	this->UnitsCount.Read(exINI, section, "Crate.Units.Count");
	this->UnitsRollChances.Read(exINI, section, "Crate.Units.RollChances");

	// The weights groups, the way LimboDelivery reads them: RandomWeights0 upwards name one group
	// per draw, the bare RandomWeights is the group for every draw.
	char tempBuffer[32];

	for (size_t i = 0; ; ++i)
	{
		ValueableVector<int> weights;
		_snprintf_s(tempBuffer, sizeof(tempBuffer), "Crate.Units.RandomWeights%d", i);
		weights.Read(exINI, section, tempBuffer);

		if (!weights.size())
			break;

		if (this->UnitsRandomWeightsData.size() > i)
			this->UnitsRandomWeightsData[i] = std::move(weights);
		else
			this->UnitsRandomWeightsData.emplace_back(std::move(weights));
	}

	ValueableVector<int> weights;
	weights.Read(exINI, section, "Crate.Units.RandomWeights");

	if (weights.size())
	{
		if (this->UnitsRandomWeightsData.size())
			this->UnitsRandomWeightsData[0] = std::move(weights);
		else
			this->UnitsRandomWeightsData.emplace_back(std::move(weights));
	}

	this->HealTargets.Read(exINI, section, "Crate.HealTargets");
	this->HealWarhead.Read<true>(exINI, section, "Crate.HealWarhead");

	this->InvulnerabilityTargets.Read(exINI, section, "Crate.Invulnerability.Targets");
	this->InvulnerabilityDuration.Read(exINI, section, "Crate.Invulnerability.Duration");

	this->EMPTargets.Read(exINI, section, "Crate.EMP.Targets");
	this->EMPDuration.Read(exINI, section, "Crate.EMP.Duration");

	this->VeterancyTargets.Read(exINI, section, "Crate.Veterancy.Targets");
	this->VeterancyLevel.Read(exINI, section, "Crate.Veterancy.Level");
	this->VeterancyStack.Read(exINI, section, "Crate.Veterancy.Stack");

	// Radii are cell counts. Leaving one unset follows the [General] -> CrateRadius default; an
	// explicit 0 means no limit; a negative one is a mistake and falls back to the default rather
	// than being kept, because the effect code would otherwise filter out everything.
	const auto readRadius = [&](Nullable<int>& radius, const char* pKey)
	{
		radius.Read(exINI, section, pKey);

		if (radius.isset() && radius.Get() < 0)
		{
			Debug::Log("[CrateType] [%s] has %s=%d below zero. Radii are cell counts, resetting it "
				"to the [General] -> CrateRadius default.\n", section, pKey, radius.Get());

			radius.Reset();
		}
	};

	readRadius(this->HealRadius, "Crate.Heal.Radius");
	this->HealAllowTypes.Read(exINI, section, "Crate.Heal.AllowTypes");
	this->HealDisallowTypes.Read(exINI, section, "Crate.Heal.DisallowTypes");
	readRadius(this->InvulnerabilityRadius, "Crate.Invulnerability.Radius");
	this->InvulnerabilityAllowTypes.Read(exINI, section, "Crate.Invulnerability.AllowTypes");
	this->InvulnerabilityDisallowTypes.Read(exINI, section, "Crate.Invulnerability.DisallowTypes");
	readRadius(this->EMPRadius, "Crate.EMP.Radius");
	this->EMPAllowTypes.Read(exINI, section, "Crate.EMP.AllowTypes");
	this->EMPDisallowTypes.Read(exINI, section, "Crate.EMP.DisallowTypes");
	readRadius(this->VeterancyRadius, "Crate.Veterancy.Radius");
	this->VeterancyAllowTypes.Read(exINI, section, "Crate.Veterancy.AllowTypes");
	this->VeterancyDisallowTypes.Read(exINI, section, "Crate.Veterancy.DisallowTypes");

	// Directions are named; the eight compass names plus their long forms are accepted, plus Any
	// and Random. A value that names nothing is reported rather than guessed.
	const auto readDirection = [&](Valueable<int>& direction, const char* pKey)
	{
		if (exINI.ReadString(section, pKey) && !INIClass::IsBlank(exINI.value()))
		{
			const int parsed = ParseDirection(exINI.value());

			if (parsed == -2)
			{
				Debug::INIParseFailed(section, pKey, exINI.value(),
					"Expected N, NE, E, SE, S, SW, W, NW (or the long forms), Any or Random");
			}
			else
			{
				direction = parsed;
			}
		}
	};

	// Triggers are named by their id, the way map actions reference them. The map owns the trigger
	// definitions, so one that is missing right now is a rules/map mismatch and is reported.
	if (exINI.ReadString(section, "Crate.Trigger") && !INIClass::IsBlank(exINI.value()))
	{
		if (auto const pTriggerType = TriggerTypeClass::Find(exINI.value()))
			this->Trigger = pTriggerType;
		else
			Debug::INIParseFailed(section, "Crate.Trigger", exINI.value(),
				"No trigger with this id exists (yet) - the crate will not fire it");
	}

	this->Reveal.Read(exINI, section, "Crate.Reveal");

	this->SpawnAtCollector.Read(exINI, section, "Crate.SpawnAtCollector");
	this->UnitsLevel.Read(exINI, section, "Crate.Units.Level");

	this->UnitsMinDist.Read(exINI, section, "Crate.Units.MinDist");
	this->UnitsMaxDist.Read(exINI, section, "Crate.Units.MaxDist");
	readDirection(this->UnitsDirection, "Crate.Units.Direction");
	this->UnitsArc.Read(exINI, section, "Crate.Units.Arc");

	this->CloakTargets.Read(exINI, section, "Crate.Cloak.Targets");

	readRadius(this->CloakRadius, "Crate.Cloak.Radius");
	this->CloakAllowTypes.Read(exINI, section, "Crate.Cloak.AllowTypes");
	this->CloakDisallowTypes.Read(exINI, section, "Crate.Cloak.DisallowTypes");

	this->ArmorTargets.Read(exINI, section, "Crate.Armor.Targets");
	this->ArmorMultiplier.Read(exINI, section, "Crate.Armor.Multiplier");
	readRadius(this->ArmorRadius, "Crate.Armor.Radius");
	this->ArmorAllowTypes.Read(exINI, section, "Crate.Armor.AllowTypes");
	this->ArmorDisallowTypes.Read(exINI, section, "Crate.Armor.DisallowTypes");

	this->FirepowerTargets.Read(exINI, section, "Crate.Firepower.Targets");
	this->FirepowerMultiplier.Read(exINI, section, "Crate.Firepower.Multiplier");
	readRadius(this->FirepowerRadius, "Crate.Firepower.Radius");
	this->FirepowerAllowTypes.Read(exINI, section, "Crate.Firepower.AllowTypes");
	this->FirepowerDisallowTypes.Read(exINI, section, "Crate.Firepower.DisallowTypes");

	this->SpeedTargets.Read(exINI, section, "Crate.Speed.Targets");
	this->SpeedMultiplier.Read(exINI, section, "Crate.Speed.Multiplier");
	readRadius(this->SpeedRadius, "Crate.Speed.Radius");
	this->SpeedAllowTypes.Read(exINI, section, "Crate.Speed.AllowTypes");
	this->SpeedDisallowTypes.Read(exINI, section, "Crate.Speed.DisallowTypes");

	this->ArmorAllowStack.Read(exINI, section, "Crate.Armor.AllowStack");
	this->ArmorMaxMultiplier.Read(exINI, section, "Crate.Armor.MaxMultiplier");
	this->FirepowerAllowStack.Read(exINI, section, "Crate.Firepower.AllowStack");
	this->FirepowerMaxMultiplier.Read(exINI, section, "Crate.Firepower.MaxMultiplier");
	this->SpeedAllowStack.Read(exINI, section, "Crate.Speed.AllowStack");
	this->SpeedMaxMultiplier.Read(exINI, section, "Crate.Speed.MaxMultiplier");

	this->Building.Read<true>(exINI, section, "Crate.Building");
	this->BuildingBuildup.Read(exINI, section, "Crate.Building.Buildup");
	this->BuildingMinDist.Read(exINI, section, "Crate.Building.MinDist");
	this->BuildingMaxDist.Read(exINI, section, "Crate.Building.MaxDist");

	readDirection(this->BuildingDirection, "Crate.Building.Direction");

	this->BuildingArc.Read(exINI, section, "Crate.Building.Arc");

	this->Reshroud.Read(exINI, section, "Crate.Reshroud");

	// Tiberium: named the way the [Tiberiums] list names its types; an unknown one is reported
	// rather than guessed.
	if (exINI.ReadString(section, "Crate.Tiberium") && !INIClass::IsBlank(exINI.value()))
	{
		const int type = FindTiberiumByName(exINI.value());

		if (type < 0)
		{
			Debug::INIParseFailed(section, "Crate.Tiberium", exINI.value(),
				"Expected one of the [Tiberiums] names");
		}
		else
		{
			this->Tiberium = type;
		}
	}

	this->TiberiumCount.Read(exINI, section, "Crate.Tiberium.Count");
	this->TiberiumStage.Read(exINI, section, "Crate.Tiberium.Stage");
	readRadius(this->TiberiumRadius, "Crate.Tiberium.Radius");
	this->TiberiumClear.Read(exINI, section, "Crate.Tiberium.Clear");
	this->TiberiumClearAmount.Read(exINI, section, "Crate.Tiberium.ClearAmount");

	this->Anim.Read<true>(exINI, section, "Crate.Anim");
	this->Sound.Read(exINI, section, "Crate.Sound");
	this->EVA.Read(exINI, section, "Crate.EVA");

	// The vanilla crate type whose feedback this crate borrows. Named the way the [Powerups]
	// list names its effects; anything else is reported rather than guessed.
	if (exINI.ReadString(section, "Crate.DefaultRemindType") && !INIClass::IsBlank(exINI.value()))
	{
		const int type = FindPowerupTypeByName(exINI.value());

		if (type < 0)
		{
			Debug::INIParseFailed(section, "Crate.DefaultRemindType", exINI.value(),
				"Expected one of the vanilla crate effect names");
		}
		else
		{
			this->DefaultRemindType = type;
		}
	}

	this->Chance.Read(exINI, section, "Crate.Chance");
	this->CollectOnWater.Read(exINI, section, "Crate.CollectOnWater");
	this->AllowedHouses.Read(exINI, section, "Crate.AllowedHouses");

	// Nothing below changes behaviour - it only reports settings that cannot do what they look
	// like they do, so a crate never silently ignores a key or silently pays out nothing.
	if (!this->HasEffect())
	{
		Debug::Log("[CrateType] [%s] sets no effect, so collecting its crate would only play the "
			"feedback. Set at least one of Crate.Money.Min, Crate.SuperWeapon, Crate.Weapon, "
			"Crate.Units, Crate.Building, Crate.HealTargets, Crate.Invulnerability.Targets, "
			"Crate.EMP.Targets, Crate.Veterancy.Targets, Crate.Cloak.Targets, Crate.Trigger, "
			"Crate.Reveal or Crate.Reshroud.\n", section);
	}

	if (!this->GivesSuperWeapon() && (hasSuperWeaponAction || !this->SuperWeaponStartsReady.Get()))
	{
		Debug::Log("[CrateType] [%s] sets its super weapon settings without Crate.SuperWeapon, so "
			"they do nothing.\n", section);
	}

	// Crate.Money.Max alone does nothing: whether the crate handles money at all is decided by
	// Crate.Money.Min alone, so this pairing is reported the same way the others are.
	if (!this->GivesMoney() && this->MoneyMax.isset())
	{
		Debug::Log("[CrateType] [%s] sets Crate.Money.Max without Crate.Money.Min, so the crate "
			"neither grants nor takes any money. The money range is driven by Crate.Money.Min.\n",
			section);
	}

	if (this->GivesSuperWeapon()
		&& this->SuperWeaponAction.Get() == CrateSuperWeaponAction::Charge
		&& !this->SuperWeaponStartsReady.Get())
	{
		Debug::Log("[CrateType] [%s] sets Crate.SuperWeaponStartsReady=false with "
			"Crate.SuperWeaponAction=Charge, but Charge always makes the weapon ready, so it has no "
			"effect.\n", section);
	}

	if (this->HealWarhead.Get() && !this->Heals())
	{
		Debug::Log("[CrateType] [%s] sets Crate.HealWarhead without Crate.HealTargets, so it does "
			"nothing.\n", section);
	}

	if (!this->Units.empty() && this->UnitsCount.size() > 1
		&& this->UnitsCount.size() != this->Units.size())
	{
		Debug::Log("[CrateType] [%s] has Crate.Units.Count=%d entries for %d Crate.Units. Only a "
			"single value or exactly one count per entry works; using the first value only.\n",
			section, this->UnitsCount.size(), this->Units.size());
	}

	// Crate.Units spawns vehicles, infantry and aircraft. Anything else - a building or a warhead
	// type, say - resolves as a techno type but would never be spawned by the code that reads the
	// list, so it is reported here instead of being skipped on every single collection.
	for (auto const& pType : this->Units)
	{
		if (pType && pType->WhatAmI() != AbstractType::UnitType
			&& pType->WhatAmI() != AbstractType::InfantryType
			&& pType->WhatAmI() != AbstractType::AircraftType)
		{
			Debug::Log("[CrateType] [%s] lists [%s] in Crate.Units, which is not a vehicle, "
				"infantry or aircraft, so it is never spawned.\n", section, pType->ID);
		}
	}

	if (!this->UnitsRandomWeightsData.empty() && this->Units.empty())
	{
		Debug::Log("[CrateType] [%s] sets Crate.Units.RandomWeights without Crate.Units, so it "
			"does nothing.\n", section);
	}

	if (!this->Building.Get() && (this->BuildingBuildup.Get()
		|| this->BuildingMinDist.Get() != 1 || this->BuildingMaxDist.Get() != 12
		|| this->BuildingDirection.Get() >= 0 || this->BuildingArc.isset()))
	{
		Debug::Log("[CrateType] [%s] sets Crate.Building settings without Crate.Building, so they "
			"do nothing.\n", section);
	}

	for (size_t i = 0; i < this->UnitsRandomWeightsData.size(); ++i)
	{
		if (this->UnitsRandomWeightsData[i].size() != this->Units.size())
		{
			Debug::Log("[CrateType] [%s] has Crate.Units.RandomWeights%d=%d entries for %d "
				"Crate.Units. Entries beyond the list are ignored.\n", section, i,
				this->UnitsRandomWeightsData[i].size(), this->Units.size());
		}
	}

	for (auto& chance : this->UnitsRollChances)
	{
		if (chance < 0.0f || chance > 1.0f)
		{
			Debug::Log("[CrateType] [%s] has a Crate.Units.RollChances entry outside 0.0-1.0. "
				"Clamping it.\n", section);

			chance = std::clamp(chance, 0.0f, 1.0f);
		}
	}

	if (this->UnitsRollChances.size() && this->UnitsCount.size() > 1)
	{
		Debug::Log("[CrateType] [%s] sets both Crate.Units.RollChances and a Crate.Units.Count "
			"list. The rolls decide how many are spawned, so the counts are ignored.\n", section);
	}

	if (this->UnitsCount.size() == this->Units.size() && !this->Units.empty()
		&& (this->UnitsRandomWeightsData.size() || this->UnitsRollChances.size()))
	{
		Debug::Log("[CrateType] [%s] lists one Crate.Units.Count per entry, so every entry spawns "
			"exactly that often and the weights are not used.\n", section);
	}

	if (this->UnitsLevel.Get() > 2)
	{
		Debug::Log("[CrateType] [%s] has Crate.Units.Level=%d, but only 0 (rookie), 1 (veteran) "
			"and 2 (elite) exist. Clamping.\n", section, this->UnitsLevel.Get());

		this->UnitsLevel = std::clamp(this->UnitsLevel.Get(), 0, 2);
	}

	if (this->UnitsLevel.Get() > 0 && this->Units.empty())
	{
		Debug::Log("[CrateType] [%s] sets Crate.Units.Level without Crate.Units, so it does "
			"nothing.\n", section);
	}

	// An effect that targets technos needs both the audience and a magnitude to do anything. The
	// magnitude counts as set when it is positive, or non-zero for the effects that also accept a
	// negative value to work in the other direction.
	auto const reportUnpairedTargets = [section](const char* pMagnitudeKey, int magnitude,
		bool hasTargets, const char* pTargetsKey, bool allowNegative = false)
	{
		const bool hasMagnitude = allowNegative ? magnitude != 0 : magnitude > 0;

		if (hasMagnitude && !hasTargets)
			Debug::Log("[CrateType] [%s] sets %s without %s, so it does nothing.\n",
				section, pMagnitudeKey, pTargetsKey);
		else if (!hasMagnitude && hasTargets)
			Debug::Log("[CrateType] [%s] sets %s without a %s %s, so it does nothing.\n",
				section, pTargetsKey, allowNegative ? "non-zero" : "positive", pMagnitudeKey);
	};

	reportUnpairedTargets("Crate.Invulnerability.Duration", this->InvulnerabilityDuration.Get(),
		this->Protects(), "Crate.Invulnerability.Targets");

	reportUnpairedTargets("Crate.EMP.Duration", this->EMPDuration.Get(),
		this->Freezes(), "Crate.EMP.Targets");

	reportUnpairedTargets("Crate.Veterancy.Level", this->VeterancyLevel.Get(),
		this->Promotes(), "Crate.Veterancy.Targets", true);

	if (this->VeterancyLevel.Get() > 2 || this->VeterancyLevel.Get() < -2)
	{
		Debug::Log("[CrateType] [%s] has Crate.Veterancy.Level=%d, but only -2 (demote to rookie), "
			"-1 (demote to veteran), 1 (veteran) and 2 (elite) exist. Clamping.\n", section,
			this->VeterancyLevel.Get());

		this->VeterancyLevel = std::clamp(this->VeterancyLevel.Get(), -2, 2);
	}

	if (this->SpawnAtCollector.Get() && this->Units.empty())
	{
		Debug::Log("[CrateType] [%s] sets Crate.SpawnAtCollector without Crate.Units or Crate.Building, "
			"so it does nothing.\n", section);
	}

	if (this->CloakRadius.isset() && !this->Cloaks())
	{
		Debug::Log("[CrateType] [%s] sets Crate.Cloak.Radius without Crate.Cloak.Targets, so it "
			"does nothing.\n", section);
	}

	// The upgrade crates: a multiplier at or below zero would zero the stat out or turn it into a
	// penalty, so it falls back to the [Powerups] default instead of being kept.
	const auto checkMultiplier = [&](Nullable<double>& multiplier, const char* pKey)
	{
		if (multiplier.isset() && multiplier.Get() <= 0.0)
		{
			Debug::Log("[CrateType] [%s] has %s=%f at or below zero. Multipliers are factors, "
				"resetting it to the [Powerups] default.\n", section, pKey, multiplier.Get());

			multiplier.Reset();
		}
	};

	checkMultiplier(this->ArmorMultiplier, "Crate.Armor.Multiplier");
	checkMultiplier(this->FirepowerMultiplier, "Crate.Firepower.Multiplier");
	checkMultiplier(this->SpeedMultiplier, "Crate.Speed.Multiplier");

	// The stacking controls: a cap only means something while stacking is allowed, and one at or
	// below one could never be reached by a multiplier above one.
	const auto checkStacking = [&](const Valueable<bool>& allowStack, Nullable<double>& maxMultiplier,
		const char* pMaxKey)
	{
		if (!maxMultiplier.isset())
			return;

		if (!allowStack.Get())
		{
			Debug::Log("[CrateType] [%s] sets %s without AllowStack, so it does nothing - a techno "
				"that is not stacked on never reaches the cap.\n", section, pMaxKey);
		}

		if (maxMultiplier.Get() <= 1.0)
		{
			Debug::Log("[CrateType] [%s] has %s=%f at or below 1.0, which no multiplier above one "
				"can ever reach. Dropping it.\n", section, pMaxKey, maxMultiplier.Get());

			maxMultiplier.Reset();
		}
	};

	checkStacking(this->ArmorAllowStack, this->ArmorMaxMultiplier, "Crate.Armor.MaxMultiplier");
	checkStacking(this->FirepowerAllowStack, this->FirepowerMaxMultiplier, "Crate.Firepower.MaxMultiplier");
	checkStacking(this->SpeedAllowStack, this->SpeedMaxMultiplier, "Crate.Speed.MaxMultiplier");

	// Type filters: a type listed in both lists is reported, because the disallow list wins and the
	// allow list entry can then never do anything.
	const auto checkTypeFilters = [&](const ValueableVector<TechnoTypeClass*>& allow,
		const ValueableVector<TechnoTypeClass*>& disallow, const char* pPrefix)
	{
		for (auto const& pType : allow)
		{
			if (pType && std::find(disallow.begin(), disallow.end(), pType) != disallow.end())
			{
				Debug::Log("[CrateType] [%s] lists [%s] in both %sAllowTypes and %sDisallowTypes. "
					"The disallow list wins, so it is never affected.\n", section, pType->ID,
					pPrefix, pPrefix);
			}
		}
	};

	const auto reportUnusedUpgrade = [&](bool hasSettings, bool hasTargets, const char* pPrefix)
	{
		if (hasSettings && !hasTargets)
		{
			Debug::Log("[CrateType] [%s] sets %sMultiplier, %sRadius, %sAllowStack or "
				"%sMaxMultiplier without %sTargets, so they do nothing.\n", section, pPrefix,
				pPrefix, pPrefix, pPrefix, pPrefix);
		}
	};

	reportUnusedUpgrade(this->ArmorMultiplier.isset() || this->ArmorRadius.isset()
		|| this->ArmorAllowStack.Get() || this->ArmorMaxMultiplier.isset(),
		this->UpgradesArmor(), "Crate.Armor.");
	reportUnusedUpgrade(this->FirepowerMultiplier.isset() || this->FirepowerRadius.isset()
		|| this->FirepowerAllowStack.Get() || this->FirepowerMaxMultiplier.isset(),
		this->UpgradesFirepower(), "Crate.Firepower.");
	reportUnusedUpgrade(this->SpeedMultiplier.isset() || this->SpeedRadius.isset()
		|| this->SpeedAllowStack.Get() || this->SpeedMaxMultiplier.isset(),
		this->UpgradesSpeed(), "Crate.Speed.");

	checkTypeFilters(this->HealAllowTypes, this->HealDisallowTypes, "Crate.Heal.");
	checkTypeFilters(this->InvulnerabilityAllowTypes, this->InvulnerabilityDisallowTypes, "Crate.Invulnerability.");
	checkTypeFilters(this->EMPAllowTypes, this->EMPDisallowTypes, "Crate.EMP.");
	checkTypeFilters(this->VeterancyAllowTypes, this->VeterancyDisallowTypes, "Crate.Veterancy.");
	checkTypeFilters(this->CloakAllowTypes, this->CloakDisallowTypes, "Crate.Cloak.");
	checkTypeFilters(this->ArmorAllowTypes, this->ArmorDisallowTypes, "Crate.Armor.");
	checkTypeFilters(this->FirepowerAllowTypes, this->FirepowerDisallowTypes, "Crate.Firepower.");
	checkTypeFilters(this->SpeedAllowTypes, this->SpeedDisallowTypes, "Crate.Speed.");

	// The placement range keys (Crate.Building, Crate.Units) share their checks: distances are
	// cell counts and are clamped, an inverted range is swapped, and an arc width is clamped and
	// only does something with a direction to narrow.
	const auto checkPlacementRange = [&](Valueable<int>& min, Valueable<int>& max,
		Valueable<int>& direction, Nullable<int>& arc, const char* pPrefix)
	{
		if (min.Get() < 0 || max.Get() < 0)
		{
			Debug::Log("[CrateType] [%s] has a negative %sMinDist or %sMaxDist. Distances are "
				"cell counts, clamping to 0.\n", section, pPrefix, pPrefix);

			min = std::max(min.Get(), 0);
			max = std::max(max.Get(), 0);
		}

		if (min.Get() > max.Get())
		{
			Debug::Log("[CrateType] [%s] has %sMinDist=%d above %sMaxDist=%d. Swapping them.\n",
				section, pPrefix, min.Get(), pPrefix, max.Get());

			std::swap(min, max);
		}

		if (arc.isset() && (arc.Get() < 1 || arc.Get() > 360))
		{
			Debug::Log("[CrateType] [%s] has %sArc=%d outside 1-360. Clamping.\n",
				section, pPrefix, arc.Get());

			arc = std::clamp(arc.Get(), 1, 360);
		}

		if (arc.isset() && direction.Get() < 0)
		{
			Debug::Log("[CrateType] [%s] sets %sArc without %sDirection, so it does nothing.\n",
				section, pPrefix, pPrefix);
		}
	};

	checkPlacementRange(this->BuildingMinDist, this->BuildingMaxDist, this->BuildingDirection,
		this->BuildingArc, "Crate.Building.");

	checkPlacementRange(this->UnitsMinDist, this->UnitsMaxDist, this->UnitsDirection,
		this->UnitsArc, "Crate.Units.");

	if (this->Reveal.Get() && this->Reshroud.Get())
	{
		Debug::Log("[CrateType] [%s] sets both Crate.Reveal and Crate.Reshroud, so whichever runs "
			"first is undone by the other.\n", section);
	}

	// The tiberium settings only mean something together with Crate.Tiberium.
	if (this->Tiberium.Get() < 0
		&& (this->TiberiumCount.Get() != 1 || this->TiberiumStage.Get() >= 0
			|| this->TiberiumRadius.isset() || this->TiberiumClear.Get()
			|| this->TiberiumClearAmount.Get() != 0))
	{
		Debug::Log("[CrateType] [%s] sets Crate.Tiberium settings without Crate.Tiberium, so they "
			"do nothing.\n", section);
	}

	if (this->TiberiumCount.Get() < 0)
	{
		Debug::Log("[CrateType] [%s] has Crate.Tiberium.Count=%d below zero. Counts are cell "
			"counts, clamping to 0 (every cell in range).\n", section, this->TiberiumCount.Get());

		this->TiberiumCount = 0;
	}

	if (this->TiberiumClearAmount.Get() < 0)
	{
		Debug::Log("[CrateType] [%s] has a negative Crate.Tiberium.ClearAmount. Amounts are ore "
			"shares, clamping to 0 (the whole cell).\n", section);

		this->TiberiumClearAmount = 0;
	}

	if (this->TiberiumClearAmount.Get() != 0 && !this->TiberiumClear.Get())
	{
		Debug::Log("[CrateType] [%s] sets Crate.Tiberium.ClearAmount without Crate.Tiberium.Clear, "
			"so it does nothing.\n", section);
	}

	if (this->Chance.Get() < 0.0 || this->Chance.Get() > 1.0)
	{
		Debug::Log("[CrateType] [%s] has Crate.Chance=%.4f outside 0.0-1.0. It is a probability, "
			"not a weight.\n", section, this->Chance.Get());

		this->Chance = std::clamp(this->Chance.Get(), 0.0, 1.0);
	}
}

template <typename T>
void CrateTypeClass::Serialize(T& Stm)
{
	Stm
		.Process(this->TypeID)
		.Process(this->MoneyMin)
		.Process(this->MoneyMax)
		.Process(this->SuperWeapon)
		.Process(this->SuperWeaponAction)
		.Process(this->SuperWeaponStartsReady)
		.Process(this->Weapon)
		.Process(this->Units)
		.Process(this->UnitsCount)
		.Process(this->UnitsRollChances)
		.Process(this->UnitsRandomWeightsData)
		.Process(this->HealTargets)
		.Process(this->HealWarhead)
		.Process(this->InvulnerabilityTargets)
		.Process(this->InvulnerabilityDuration)
		.Process(this->EMPTargets)
		.Process(this->EMPDuration)
		.Process(this->VeterancyTargets)
		.Process(this->VeterancyLevel)
		.Process(this->VeterancyStack)
		.Process(this->HealRadius)
		.Process(this->HealAllowTypes)
		.Process(this->HealDisallowTypes)
		.Process(this->InvulnerabilityRadius)
		.Process(this->InvulnerabilityAllowTypes)
		.Process(this->InvulnerabilityDisallowTypes)
		.Process(this->EMPRadius)
		.Process(this->EMPAllowTypes)
		.Process(this->EMPDisallowTypes)
		.Process(this->VeterancyRadius)
		.Process(this->VeterancyAllowTypes)
		.Process(this->VeterancyDisallowTypes)
		.Process(this->Trigger)
		.Process(this->Reveal)
		.Process(this->SpawnAtCollector)
		.Process(this->UnitsLevel)
		.Process(this->UnitsMinDist)
		.Process(this->UnitsMaxDist)
		.Process(this->UnitsDirection)
		.Process(this->UnitsArc)
		.Process(this->CloakTargets)
		.Process(this->CloakRadius)
		.Process(this->CloakAllowTypes)
		.Process(this->CloakDisallowTypes)
		.Process(this->ArmorTargets)
		.Process(this->ArmorMultiplier)
		.Process(this->ArmorRadius)
		.Process(this->ArmorAllowTypes)
		.Process(this->ArmorDisallowTypes)
		.Process(this->FirepowerTargets)
		.Process(this->FirepowerMultiplier)
		.Process(this->FirepowerRadius)
		.Process(this->FirepowerAllowTypes)
		.Process(this->FirepowerDisallowTypes)
		.Process(this->SpeedTargets)
		.Process(this->SpeedMultiplier)
		.Process(this->SpeedRadius)
		.Process(this->SpeedAllowTypes)
		.Process(this->SpeedDisallowTypes)
		.Process(this->ArmorAllowStack)
		.Process(this->ArmorMaxMultiplier)
		.Process(this->FirepowerAllowStack)
		.Process(this->FirepowerMaxMultiplier)
		.Process(this->SpeedAllowStack)
		.Process(this->SpeedMaxMultiplier)
		.Process(this->Building)
		.Process(this->BuildingBuildup)
		.Process(this->BuildingMinDist)
		.Process(this->BuildingMaxDist)
		.Process(this->BuildingDirection)
		.Process(this->BuildingArc)
		.Process(this->Reshroud)
		.Process(this->Tiberium)
		.Process(this->TiberiumCount)
		.Process(this->TiberiumStage)
		.Process(this->TiberiumRadius)
		.Process(this->TiberiumClear)
		.Process(this->TiberiumClearAmount)
		.Process(this->Anim)
		.Process(this->Sound)
		.Process(this->EVA)
		.Process(this->DefaultRemindType)
		.Process(this->Chance)
		.Process(this->CollectOnWater)
		.Process(this->AllowedHouses)
		;
}

void CrateTypeClass::LoadFromStream(PhobosStreamReader& Stm)
{
	this->Serialize(Stm);
}

void CrateTypeClass::SaveToStream(PhobosStreamWriter& Stm)
{
	this->Serialize(Stm);
}
