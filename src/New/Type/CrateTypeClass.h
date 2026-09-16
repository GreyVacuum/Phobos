#pragma once

#include <Utilities/Enumerable.h>
#include <Utilities/Enum.h>
#include <Utilities/TemplateDef.h>

class AnimTypeClass;
class SuperWeaponTypeClass;
class TriggerTypeClass;
class UnitTypeClass;
class VocClass;
class VoxClass;
class WarheadTypeClass;
class WeaponTypeClass;

// Values of Crate.SuperWeaponAction: what a crate does with its Crate.SuperWeapon.
//
// Firing the weapon from the crate itself is deliberately not offered. The engine's launch entry
// points take an x87 argument YRpp does not model, silently do nothing for super weapon kinds
// outside the built-in set, and some kinds need state (a Chrono Warp source coordinate, a charged
// weapon) that a crate cannot provide - so an automatic shot would work for some super weapons and
// quietly fail for others. OneTime gives the receiving player a single use to fire themselves,
// which is what the vanilla crate that grants a super weapon does, and Crate.Weapon fires an
// ordinary weapon at the crate's cell.
namespace CrateSuperWeaponAction
{
	// Hand the weapon over and let it recharge as usual. An already present one is left alone and
	// reported.
	constexpr int Grant = 0;
	// Hand it over as a single use weapon, the way the vanilla crate that grants a super weapon
	// does. An already present one is left alone and reported.
	constexpr int OneTime = 1;
	// Make the house's weapon ready to fire, handing it over first if the house lacks it.
	constexpr int Charge = 2;

	// Resolves the value of a SuperWeaponAction key. Returns -1 when it names nothing.
	int Parse(const char* pValue);
}

// Custom crate (powerup) types, defined in [CrateTypes] in rulesmd.ini.
//
// Every key in a crate's section acts on its own: a crate type is described by listing the things
// it does, so there is no crate "kind" to pick between and no key that is quietly ignored because
// a different key chose a different kind.
class CrateTypeClass final : public Enumerable<CrateTypeClass>
{
public:
	// Number that CrateType keys and map trigger actions use to select this crate type. It has to be
	// at least CrateSource::FirstTypeID so it can never be taken for a vanilla crate index.
	Nullable<int> TypeID;

	// What collecting the crate does. A key that is not set does nothing.
	Nullable<int> MoneyMin;
	Nullable<int> MoneyMax;

	ValueableIdx<SuperWeaponTypeClass> SuperWeapon;
	Valueable<int> SuperWeaponAction;

	// Whether a handed over weapon is ready at once. Clearing this makes the player wait for it to
	// recharge instead, which turns OneTime into a single use that still has to be earned.
	Valueable<bool> SuperWeaponStartsReady;

	Valueable<WeaponTypeClass*> Weapon;

	ValueableVector<UnitTypeClass*> Units;
	Valueable<int> UnitsCount;

	Valueable<AffectedHouse> HealTargets;
	Valueable<WarheadTypeClass*> HealWarhead;

	// Makes technos the collecting house is allowed to affect take no damage for a while.
	Valueable<AffectedHouse> InvulnerabilityTargets;
	Valueable<int> InvulnerabilityDuration;

	// Freezes technos the collecting house is allowed to affect, the way an EMP does.
	Valueable<AffectedHouse> EMPTargets;
	Valueable<int> EMPDuration;

	// Promotes technos the collecting house is allowed to affect. Level 1 is a veteran, 2 an elite;
	// technos already above the level are left alone and types that cannot gain experience at all
	// are never touched.
	Valueable<AffectedHouse> VeterancyTargets;
	Valueable<int> VeterancyLevel;

	// The map trigger whose actions run when the crate is collected. It is named by its id as the
	// map references it, and its actions fire unconditionally - its events are not consulted.
	Valueable<TriggerTypeClass*> Trigger;

	// Removes the shroud for the collecting house, the way the vanilla Reveal crate does.
	Valueable<bool> Reveal;

	// Spawns Crate.Units around the collecting techno instead of around the crate's cell.
	Valueable<bool> SpawnAtCollector;

	// Cloaks the collecting techno. Types that cannot cloak are left as they are.
	Valueable<bool> CloakCollector;

	Valueable<bool> Reshroud;

	// Feedback for the collecting player.
	Valueable<AnimTypeClass*> Anim;
	ValueableIdx<VocClass> Sound;
	ValueableIdx<VoxClass> EVA;

	// Where the crate may appear on its own.
	Valueable<double> Chance;
	Valueable<bool> CollectOnWater;

	CrateTypeClass(const char* const pTitle) : Enumerable<CrateTypeClass>(pTitle)
		, TypeID { }
		, MoneyMin { }
		, MoneyMax { }
		, SuperWeapon { -1 }
		, SuperWeaponAction { CrateSuperWeaponAction::Charge }
		, SuperWeaponStartsReady { true }
		, Weapon { nullptr }
		, Units { }
		, UnitsCount { 1 }
		, HealTargets { AffectedHouse::None }
		, HealWarhead { nullptr }
		, InvulnerabilityTargets { AffectedHouse::None }
		, InvulnerabilityDuration { 0 }
		, EMPTargets { AffectedHouse::None }
		, EMPDuration { 0 }
		, VeterancyTargets { AffectedHouse::None }
		, VeterancyLevel { 0 }
		, Trigger { nullptr }
		, Reveal { false }
		, SpawnAtCollector { false }
		, CloakCollector { false }
		, Reshroud { false }
		, Anim { nullptr }
		, Sound { -1 }
		, EVA { -1 }
		, Chance { 0.0 }
		, CollectOnWater { true }
	{ }

	bool GivesMoney() const { return this->MoneyMin.isset(); }
	bool GivesSuperWeapon() const { return this->SuperWeapon.Get() >= 0; }
	bool Heals() const { return this->HealTargets.Get() != AffectedHouse::None; }
	bool Protects() const { return this->InvulnerabilityTargets.Get() != AffectedHouse::None; }
	bool Freezes() const { return this->EMPTargets.Get() != AffectedHouse::None; }
	bool Promotes() const { return this->VeterancyTargets.Get() != AffectedHouse::None; }
	bool FiresTrigger() const { return this->Trigger.Get() != nullptr; }

	int GetMoneyMin() const;
	int GetMoneyMax() const;

	// Warhead used for the healing damage call, falling back to C4Warhead like the vanilla crate
	// healing does.
	WarheadTypeClass* GetHealWarhead() const;

	// True when the crate changes the game state, i.e. it is more than decoration. Crate types
	// without this are reported on load, because collecting one would visibly do nothing.
	bool HasEffect() const;

	// The crate type carrying this Crate.TypeID, or nullptr when no loaded crate type does.
	static CrateTypeClass* FindByTypeID(int typeID);

	void LoadFromINI(CCINIClass* pINI);
	void LoadFromStream(PhobosStreamReader& Stm);
	void SaveToStream(PhobosStreamWriter& Stm);

private:
	template <typename T>
	void Serialize(T& Stm);
};
