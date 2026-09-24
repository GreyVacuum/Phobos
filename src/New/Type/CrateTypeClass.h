#pragma once

#include <Utilities/Enumerable.h>
#include <Utilities/Enum.h>
#include <Utilities/TemplateDef.h>

class AnimTypeClass;
class BuildingTypeClass;
class HouseClass;
class HouseTypeClass;
class SuperWeaponTypeClass;
class TechnoTypeClass;
class TriggerTypeClass;
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

	// The super weapon(s) handed over. Listing more than one draws a single one of them when the
	// crate is collected, so one crate can offer a whole set instead of needing one crate type per
	// weapon. Listing the same weapon twice makes it equally likely to be drawn twice as often,
	// since every entry takes an equal share of the draw.
	ValueableVector<SuperWeaponTypeClass*> SuperWeapon;
	Valueable<int> SuperWeaponAction;

	// Whether a handed over weapon is ready at once. Clearing this makes the player wait for it to
	// recharge instead, which turns OneTime into a single use that still has to be earned.
	Valueable<bool> SuperWeaponStartsReady;

	Valueable<WeaponTypeClass*> Weapon;

	// Vehicles, infantry or aircraft, spawned next to the crate. The entry's type decides how it
	// is spawned: aircraft come out at cruise height.
	ValueableVector<TechnoTypeClass*> Units;

	// How many of them to spawn. A single value draws that many entries at random; a list of
	// exactly the Units' length spawns each entry that exact number of times instead.
	ValueableVector<int> UnitsCount;

	// The weighted rolls system, the way super weapon LimboDelivery uses it. RollChances holds one
	// independent 0-1 roll per draw - a roll above the chance is skipped - and each surviving draw
	// picks its entry with the matching weights group: RandomWeights for every draw, or
	// RandomWeights0 upwards, one group per draw, falling back to the last group that is present.
	// Without RollChances, the Units.Count draws all use the first group. When Units.Count lists
	// one count per entry, none of this applies.
	ValueableVector<float> UnitsRollChances;
	std::vector<ValueableVector<int>> UnitsRandomWeightsData;

	Valueable<AffectedHouse> HealTargets;
	Valueable<WarheadTypeClass*> HealWarhead;

	// Makes technos the collecting house is allowed to affect take no damage for a while.
	Valueable<AffectedHouse> InvulnerabilityTargets;
	Valueable<int> InvulnerabilityDuration;

	// Freezes technos the collecting house is allowed to affect, the way an EMP does.
	Valueable<AffectedHouse> EMPTargets;
	Valueable<int> EMPDuration;

	// Promotes - or demotes - technos the collecting house is allowed to affect. Level 1 is a
	// veteran, 2 an elite. A negative level demotes instead, -1 to veteran and -2 to rookie.
	// Technos already at or past the level are left alone in either direction, so a promotion
	// never demotes and a demotion never promotes. Types that cannot gain experience at all are
	// never touched.
	Valueable<AffectedHouse> VeterancyTargets;
	Valueable<int> VeterancyLevel;

	// When set, collecting the crate again adds to the experience the affected technos already
	// have instead of each promotion being capped at the level, so two Level=1 collections make an
	// elite. A negative level subtracts that much experience instead, down to a floor of a rookie.
	// The sum is clamped by [General] -> VeteranCap as usual.
	Valueable<bool> VeterancyStack;

	// Limits the audience of the matching filtered effect to this many cells around the crate's
	// cell. Unset means the crate follows the [General] -> CrateRadius default; an explicit 0
	// means the effect is not limited at all.
	Nullable<int> HealRadius;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> HealAllowTypes;
	ValueableVector<TechnoTypeClass*> HealDisallowTypes;
	Nullable<int> InvulnerabilityRadius;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> InvulnerabilityAllowTypes;
	ValueableVector<TechnoTypeClass*> InvulnerabilityDisallowTypes;
	Nullable<int> EMPRadius;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> EMPAllowTypes;
	ValueableVector<TechnoTypeClass*> EMPDisallowTypes;
	Nullable<int> VeterancyRadius;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> VeterancyAllowTypes;
	ValueableVector<TechnoTypeClass*> VeterancyDisallowTypes;

	// The map trigger whose actions run when the crate is collected. It is named by its id as the
	// map references it, and its actions fire unconditionally - its events are not consulted.
	Valueable<TriggerTypeClass*> Trigger;

	// Removes the shroud for the collecting house, the way the vanilla Reveal crate does.
	Valueable<bool> Reveal;

	// Spawns Crate.Units around the collecting techno instead of around the crate's cell.
	Valueable<bool> SpawnAtCollector;

	// The rank Crate.Units come out with. 0 rookie, 1 veteran, 2 elite.
	Valueable<int> UnitsLevel;

	// How far from the base cell the entries of Crate.Units are placed, in cells, searched near to
	// far - the same options Crate.Building uses. Every entry gets a cell of its own. The nearest
	// cell searched is one away by default, so nothing lands on the crate or the collector.
	Valueable<int> UnitsMinDist;
	Valueable<int> UnitsMaxDist;

	// Narrows the Crate.Units placement search to a sector relative to the base cell, stored as
	// degrees from north, clockwise; -1 means any direction, -3 means each ring is walked in a
	// shuffled order so the entries land in random directions. Parsed from Crate.Units.Direction.
	// The default is -3, so entries spread into random directions unless a sector is asked for.
	Valueable<int> UnitsDirection;

	// The width of the accepted Crate.Units sector, in degrees. Only meaningful with a direction.
	Nullable<int> UnitsArc;

	// Cloaks technos the collecting house is allowed to affect. Types that cannot cloak are left
	// as they are.
	Valueable<AffectedHouse> CloakTargets;
	Nullable<int> CloakRadius;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> CloakAllowTypes;
	ValueableVector<TechnoTypeClass*> CloakDisallowTypes;

	// The three upgrade crates: armor, firepower and speed of the affected technos are multiplied
	// by the matching multiplier. An unset multiplier follows the [Powerups] parameter of the
	// matching vanilla crate type. The speed upgrade skips aircraft, whose speed the flight logic
	// owns, the way the vanilla crate does.
	//
	// By default a techno whose stat has already been multiplied is left alone, which is what the
	// vanilla crates do - a crate can never hand the same upgrade out twice, no matter how often
	// it is collected. AllowStack lifts that: every collection multiplies again, and MaxMultiplier
	// caps the result when set, so repeated collections cannot run away.
	Valueable<AffectedHouse> ArmorTargets;
	Nullable<double> ArmorMultiplier;
	Nullable<int> ArmorRadius;
	Valueable<bool> ArmorAllowStack;
	Nullable<double> ArmorMaxMultiplier;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> ArmorAllowTypes;
	ValueableVector<TechnoTypeClass*> ArmorDisallowTypes;

	Valueable<AffectedHouse> FirepowerTargets;
	Nullable<double> FirepowerMultiplier;
	Nullable<int> FirepowerRadius;
	Valueable<bool> FirepowerAllowStack;
	Nullable<double> FirepowerMaxMultiplier;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> FirepowerAllowTypes;
	ValueableVector<TechnoTypeClass*> FirepowerDisallowTypes;

	Valueable<AffectedHouse> SpeedTargets;
	Nullable<double> SpeedMultiplier;
	Nullable<int> SpeedRadius;
	Valueable<bool> SpeedAllowStack;
	Nullable<double> SpeedMaxMultiplier;
	// Which techno types the effect may touch. An empty AllowTypes admits every type;
	// DisallowTypes always wins over it.
	ValueableVector<TechnoTypeClass*> SpeedAllowTypes;
	ValueableVector<TechnoTypeClass*> SpeedDisallowTypes;

	// A building spawned next to the crate for the collecting house.
	Valueable<BuildingTypeClass*> Building;

	// When set, the building plays its buildup sequence and finishes constructing normally
	// instead of appearing completed at once.
	Valueable<bool> BuildingBuildup;

	// How far from the base cell the building may be placed, in cells. The nearest cell that
	// passes every placement check within the range wins. The nearest cell searched is one away
	// by default, so the building never lands on the crate's own cell.
	Valueable<int> BuildingMinDist;
	Valueable<int> BuildingMaxDist;

	// Narrows the placement search to a sector relative to the base cell. Stored as degrees from
	// north, clockwise (N=0, E=90, S=180, W=270); -1 means any direction, and -3 - the default -
	// walks each ring in a shuffled order, so the building lands in a random direction. The names
	// are parsed from Crate.Building.Direction.
	Valueable<int> BuildingDirection;

	// The width of the accepted direction sector, in degrees. Only meaningful with a direction.
	Nullable<int> BuildingArc;

	Valueable<bool> Reshroud;

	// Tiberium (ore): grows the named type on the cells around the crate, or clears what is already
	// there. The type is named the way the [Tiberiums] list names it. Count is how many cells to
	// touch - 0 means every cell in range that can take it. Stage is the density to grow to, -1
	// being the fullest the type supports, and clearing takes an amount per cell instead, 0 being
	// the whole cell. Unlike the other radius keys, 0 here means the crate's own cell only, since
	// covering the whole map in ore would be a performance problem rather than a feature.
	Valueable<int> Tiberium;
	Valueable<int> TiberiumCount;
	Valueable<int> TiberiumStage;
	Nullable<int> TiberiumRadius;
	Valueable<bool> TiberiumClear;
	Valueable<int> TiberiumClearAmount;

	// Feedback for the collecting player.
	Valueable<AnimTypeClass*> Anim;
	ValueableIdx<VocClass> Sound;
	ValueableIdx<VoxClass> EVA;

	// Names one of the vanilla crate types, the way the [Powerups] list names them - Unit, Money,
	// Heal, Armor, Speed, FirePower, Veteran, ICBM and so on. The crate then plays that type's
	// pickup animation, and for the three upgrade crates its EVA line, for anything the explicit
	// keys above leave unset. Empty by default.
	Valueable<int> DefaultRemindType;

	// Where the crate may appear on its own.
	Valueable<double> Chance;
	Valueable<bool> CollectOnWater;

	// When set, only units of these house types can collect the crate; everyone else walks past
	// it and the crate stays where it is. An empty list lets every house collect it.
	ValueableVector<HouseTypeClass*> AllowedHouses;

	CrateTypeClass(const char* const pTitle) : Enumerable<CrateTypeClass>(pTitle)
		, TypeID { }
		, MoneyMin { }
		, MoneyMax { }
		, SuperWeapon { }
		, SuperWeaponAction { CrateSuperWeaponAction::Charge }
		, SuperWeaponStartsReady { true }
		, Weapon { nullptr }
		, Units { }
		, UnitsCount { }
		, UnitsRollChances { }
		, UnitsRandomWeightsData { }
		, HealTargets { AffectedHouse::None }
		, HealWarhead { nullptr }
		, InvulnerabilityTargets { AffectedHouse::None }
		, InvulnerabilityDuration { 0 }
		, EMPTargets { AffectedHouse::None }
		, EMPDuration { 0 }
		, VeterancyTargets { AffectedHouse::None }
		, VeterancyLevel { 0 }
		, VeterancyStack { false }
		, HealRadius { }
		, HealAllowTypes { }
		, HealDisallowTypes { }
		, InvulnerabilityRadius { }
		, InvulnerabilityAllowTypes { }
		, InvulnerabilityDisallowTypes { }
		, EMPRadius { }
		, EMPAllowTypes { }
		, EMPDisallowTypes { }
		, VeterancyRadius { }
		, VeterancyAllowTypes { }
		, VeterancyDisallowTypes { }
		, Trigger { nullptr }
		, Reveal { false }
		, SpawnAtCollector { false }
		, UnitsLevel { 0 }
		, UnitsMinDist { 1 }
		, UnitsMaxDist { 10 }
		, UnitsDirection { -3 }
		, UnitsArc { }
		, CloakTargets { AffectedHouse::None }
		, CloakRadius { }
		, CloakAllowTypes { }
		, CloakDisallowTypes { }
		, ArmorTargets { AffectedHouse::None }
		, ArmorMultiplier { }
		, ArmorRadius { }
		, ArmorAllowStack { false }
		, ArmorMaxMultiplier { }
		, ArmorAllowTypes { }
		, ArmorDisallowTypes { }
		, FirepowerTargets { AffectedHouse::None }
		, FirepowerMultiplier { }
		, FirepowerRadius { }
		, FirepowerAllowStack { false }
		, FirepowerMaxMultiplier { }
		, FirepowerAllowTypes { }
		, FirepowerDisallowTypes { }
		, SpeedTargets { AffectedHouse::None }
		, SpeedMultiplier { }
		, SpeedRadius { }
		, SpeedAllowStack { false }
		, SpeedMaxMultiplier { }
		, SpeedAllowTypes { }
		, SpeedDisallowTypes { }
		, Building { nullptr }
		, BuildingBuildup { false }
		, BuildingMinDist { 1 }
		, BuildingMaxDist { 12 }
		, BuildingDirection { -3 }
		, BuildingArc { }
		, Reshroud { false }
		, Tiberium { -1 }
		, TiberiumCount { 1 }
		, TiberiumStage { -1 }
		, TiberiumRadius { }
		, TiberiumClear { false }
		, TiberiumClearAmount { 0 }
		, Anim { nullptr }
		, Sound { -1 }
		, EVA { -1 }
		, DefaultRemindType { -1 }
		, Chance { 0.0 }
		, CollectOnWater { true }
		, AllowedHouses { }
	{ }

	bool GivesMoney() const { return this->MoneyMin.isset(); }
	bool GivesSuperWeapon() const { return !this->SuperWeapon.empty(); }

	// True when this house may collect the crate: no AllowedHouses restriction set, or the
	// house's type is listed in it.
	bool CanBeCollectedBy(HouseClass* pHouse) const;
	bool Heals() const { return this->HealTargets.Get() != AffectedHouse::None; }
	bool Protects() const { return this->InvulnerabilityTargets.Get() != AffectedHouse::None; }
	bool Freezes() const { return this->EMPTargets.Get() != AffectedHouse::None; }
	bool Promotes() const { return this->VeterancyTargets.Get() != AffectedHouse::None; }
	bool Cloaks() const { return this->CloakTargets.Get() != AffectedHouse::None; }
	bool UpgradesArmor() const { return this->ArmorTargets.Get() != AffectedHouse::None; }
	bool UpgradesFirepower() const { return this->FirepowerTargets.Get() != AffectedHouse::None; }
	bool UpgradesSpeed() const { return this->SpeedTargets.Get() != AffectedHouse::None; }
	bool FiresTrigger() const { return this->Trigger.Get() != nullptr; }
	bool SpawnsTiberium() const { return this->Tiberium.Get() >= 0 && !this->TiberiumClear.Get(); }
	bool ClearsTiberium() const { return this->Tiberium.Get() >= 0 && this->TiberiumClear.Get(); }

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
