#ifndef ZODIAC_TESTS_SUPPORT_ADDONS_H
#define ZODIAC_TESTS_SUPPORT_ADDONS_H

// Thin wrappers around the add-on registration. `zodiac_addon.hpp` defines
// NON-inline free functions (the ContextMgr save/load helpers), so it must be
// #included in EXACTLY ONE translation unit: tests/support/addons.cpp. Every
// other TU talks to the add-ons only through these wrappers.

class asIScriptEngine;

namespace Zodiac { class zIZodiac; }

namespace zodiac_test
{

// Registers the stock AngelScript add-ons (string, array, dictionary, any,
// handle, grid, file, weakref, datetime, math) onto a bare engine.
void RegisterEngineAddons(asIScriptEngine * engine);

// Registers the Zodiac save/load type callbacks for those same add-ons.
void RegisterZodiacAddons(Zodiac::zIZodiac * zodiac);

}

#endif // ZODIAC_TESTS_SUPPORT_ADDONS_H
