#include "CrateTypeClass.h"

#include "CrateSource.h"

#include <Powerups.h>
#include <RulesClass.h>
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

int CrateTypeClass::GetMoneyMin() const
{
	return std::abs(this->MoneyMin.Get(0));
}

int CrateTypeClass::GetMoneyMax() const
{
	return std::abs(this->MoneyMax.Get(this->GetMoneyMin()));
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
		|| this->Heals()
		|| this->Protects()
		|| this->Freezes()
		|| this->Promotes()
		|| this->FiresTrigger()
		|| this->Reveal.Get()
		|| this->Reshroud.Get();
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

	this->HealTargets.Read(exINI, section, "Crate.HealTargets");
	this->HealWarhead.Read<true>(exINI, section, "Crate.HealWarhead");

	this->InvulnerabilityTargets.Read(exINI, section, "Crate.Invulnerability.Targets");
	this->InvulnerabilityDuration.Read(exINI, section, "Crate.Invulnerability.Duration");

	this->EMPTargets.Read(exINI, section, "Crate.EMP.Targets");
	this->EMPDuration.Read(exINI, section, "Crate.EMP.Duration");

	this->VeterancyTargets.Read(exINI, section, "Crate.Veterancy.Targets");
	this->VeterancyLevel.Read(exINI, section, "Crate.Veterancy.Level");

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
	this->CloakCollector.Read(exINI, section, "Crate.CloakCollector");

	this->Reshroud.Read(exINI, section, "Crate.Reshroud");

	this->Anim.Read<true>(exINI, section, "Crate.Anim");
	this->Sound.Read(exINI, section, "Crate.Sound");
	this->EVA.Read(exINI, section, "Crate.EVA");

	this->Chance.Read(exINI, section, "Crate.Chance");
	this->CollectOnWater.Read(exINI, section, "Crate.CollectOnWater");

	// Nothing below changes behaviour - it only reports settings that cannot do what they look
	// like they do, so a crate never silently ignores a key or silently pays out nothing.
	if (!this->HasEffect())
	{
		Debug::Log("[CrateType] [%s] sets no effect, so collecting its crate would only play the "
			"feedback. Set at least one of Crate.Money.Min, Crate.SuperWeapon, Crate.Weapon, "
			"Crate.Units, Crate.HealTargets, Crate.Invulnerability.Targets, Crate.EMP.Targets, "
			"Crate.Veterancy.Targets, Crate.Trigger, Crate.Reveal or Crate.Reshroud.\n", section);
	}

	if (!this->GivesSuperWeapon() && (hasSuperWeaponAction || !this->SuperWeaponStartsReady.Get()))
	{
		Debug::Log("[CrateType] [%s] sets its super weapon settings without Crate.SuperWeapon, so "
			"they do nothing.\n", section);
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

	if (this->UnitsCount.Get() != 1 && this->Units.empty())
	{
		Debug::Log("[CrateType] [%s] sets Crate.Units.Count without Crate.Units, so it does "
			"nothing.\n", section);
	}

	// An effect that targets technos needs both the audience and a magnitude to do anything.
	auto const reportUnpairedTargets = [section](const char* pMagnitudeKey, int magnitude,
		bool hasTargets, const char* pTargetsKey)
	{
		if (magnitude > 0 && !hasTargets)
			Debug::Log("[CrateType] [%s] sets %s without %s, so it does nothing.\n",
				section, pMagnitudeKey, pTargetsKey);
		else if (magnitude <= 0 && hasTargets)
			Debug::Log("[CrateType] [%s] sets %s without a positive %s, so it does nothing.\n",
				section, pTargetsKey, pMagnitudeKey);
	};

	reportUnpairedTargets("Crate.Invulnerability.Duration", this->InvulnerabilityDuration.Get(),
		this->Protects(), "Crate.Invulnerability.Targets");

	reportUnpairedTargets("Crate.EMP.Duration", this->EMPDuration.Get(),
		this->Freezes(), "Crate.EMP.Targets");

	reportUnpairedTargets("Crate.Veterancy.Level", this->VeterancyLevel.Get(),
		this->Promotes(), "Crate.Veterancy.Targets");

	if (this->VeterancyLevel.Get() > 2)
	{
		Debug::Log("[CrateType] [%s] has Crate.Veterancy.Level=%d, but only 1 (veteran) and 2 "
			"(elite) exist. Clamping.\n", section, this->VeterancyLevel.Get());

		this->VeterancyLevel = std::clamp(this->VeterancyLevel.Get(), 0, 2);
	}

	if (this->SpawnAtCollector.Get() && this->Units.empty())
	{
		Debug::Log("[CrateType] [%s] sets Crate.SpawnAtCollector without Crate.Units, so it does "
			"nothing.\n", section);
	}

	if (this->Reveal.Get() && this->Reshroud.Get())
	{
		Debug::Log("[CrateType] [%s] sets both Crate.Reveal and Crate.Reshroud, so whichever runs "
			"first is undone by the other.\n", section);
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
		.Process(this->HealTargets)
		.Process(this->HealWarhead)
		.Process(this->InvulnerabilityTargets)
		.Process(this->InvulnerabilityDuration)
		.Process(this->EMPTargets)
		.Process(this->EMPDuration)
		.Process(this->VeterancyTargets)
		.Process(this->VeterancyLevel)
		.Process(this->Trigger)
		.Process(this->Reveal)
		.Process(this->SpawnAtCollector)
		.Process(this->CloakCollector)
		.Process(this->Reshroud)
		.Process(this->Anim)
		.Process(this->Sound)
		.Process(this->EVA)
		.Process(this->Chance)
		.Process(this->CollectOnWater)
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
