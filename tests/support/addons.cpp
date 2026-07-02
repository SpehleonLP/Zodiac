#include "addons.h"

#include <angelscript.h>

// Add-on engine-registration headers.
#include "add_on/scriptstdstring/scriptstdstring.h"
#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptany/scriptany.h"
#include "add_on/scripthandle/scripthandle.h"
#include "add_on/scriptdictionary/scriptdictionary.h"
#include "add_on/scriptgrid/scriptgrid.h"
#include "add_on/scriptfile/scriptfile.h"
#include "add_on/weakref/weakref.h"
#include "add_on/datetime/datetime.h"
#include "add_on/scriptmath/scriptmath.h"

// -----------------------------------------------------------------------------
// Part A: zodiac_addon.hpp now compiles against the pinned SDK. It defines
// NON-inline free functions historically, so it must be included in EXACTLY ONE
// TU to avoid ODR violations -- this file is that TU. Every other TU talks to
// the add-ons through the thin wrappers declared in addons.h.
//
// Type coverage wired here: string, array, dictionary(+dictionaryValue), any,
// ref(CScriptHandle), grid, file, weakref/const_weakref. datetime is NOT
// registered on the engine: it has no Zodiac save/load entry (it is a POD value
// type upstream) and Zodiac's SaveModules requires every registered application
// object type to have a matching entry, so registering it would break otherwise
// unrelated modules. scriptmath registers only free functions (no app types).
// -----------------------------------------------------------------------------
#include "zodiac_addon.hpp"

namespace zodiac_test
{

void RegisterEngineAddons(asIScriptEngine * engine)
{
	// Order matters: dictionary depends on string + array.
	RegisterStdString(engine);
	RegisterScriptArray(engine, /*defaultArray*/ true);
	RegisterScriptDictionary(engine);
	RegisterScriptAny(engine);
	RegisterScriptHandle(engine);
	RegisterScriptGrid(engine);
	RegisterScriptFile(engine);
	RegisterScriptWeakRef(engine);
	RegisterScriptMath(engine);
}

void RegisterZodiacAddons(Zodiac::zIZodiac * zodiac)
{
	Zodiac::ZodiacRegisterAddons(zodiac);
}

}
