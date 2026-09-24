#pragma once

class CrateTypeClass;

// Values of the TechnoType.CrateType key, which names the crate a techno type places. The key takes
// a [CrateTypes] entry (by name or by its Crate.TypeID), a vanilla [Powerups] crate name, or
// "Random" for a crate whose type is rolled when it is collected.
namespace CrateSource
{
	// Keep the game's own behaviour for this techno type.
	constexpr int Default = -1;
	// A crate whose type is rolled when it is collected. This is the game's own value for it, and
	// also what a map trigger passes to ask for the random crate.
	constexpr int Random = 20;
	// Reported when a key could not be resolved; never stored.
	constexpr int Invalid = -2;
	// Crate.TypeID values start above the vanilla crate indices, so a crate type id can never be
	// taken for a vanilla crate.
	constexpr int FirstTypeID = 21;

	constexpr bool IsCrateTypeID(int source) { return source >= FirstTypeID; }

	// Resolves the value of a CrateType key. A blank value keeps the default.
	int Parse(const char* pValue);

	// The crate type carrying this Crate.TypeID, or nullptr when no loaded crate type does.
	CrateTypeClass* GetByTypeID(int typeID);
}
