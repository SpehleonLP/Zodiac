#include "addons.h"

#include <angelscript.h>

// Add-on engine-registration headers. These compile & link cleanly against the
// as_addons static lib.
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

#include "zodiac.h"

// -----------------------------------------------------------------------------
// NOTE (Phase-0 finding): include/zodiac_addon.hpp does NOT compile against the
// pinned SDK (/mnt/Passport/Libraries/svn/angelscript-code/sdk). That SDK ships
// the *stock* 2.38.0 add-ons; zodiac_addon.hpp was written against an older /
// patched tree and references APIs that no longer exist or are inaccessible:
//   * CContextMgr::m_threads / m_getTimeFunc are `protected` (not patched) and
//     SContextInfo is only forward-declared -> the ContextMgr save/load block
//     (lines 33-116) cannot see them.
//   * CScriptDictionary::Insert removed; CScriptDictValue::m_typeId/m_valueObj
//     are protected; CIterator::GetValue() signature changed.
//   * CScriptWeakRef::GetObjectType renamed to GetRefType.
//   * CScriptHandle::GetRef is non-const.
// None of that glue is needed for the Phase-0 (empty-module) round-trip tests,
// so instead of including the whole broken header we register only the
// self-contained std::string value type here. The full zodiac_addon.hpp needs a
// library-compat pass (patch the SDK add-ons or the header) before the
// object-graph / context tests (spec tests 8, 4, 5, 11) can be stood up.
// -----------------------------------------------------------------------------

namespace Zodiac
{
// Self-contained string save/load, matching zodiac_addon.hpp's std::string
// specialisation. Defined in this single TU only (no ODR hazard: the broken
// header is not included anywhere in the test binary).
static void ZodiacSaveString(zIZodiacWriter * writer, std::string const* string, int&)
{
	int string_id = writer->SaveString(string->c_str());
	writer->GetFile()->Write(&string_id);
}

static void ZodiacLoadString(zIZodiacReader * reader, std::string* string, int&, bool)
{
	int string_id;
	reader->GetFile()->Read(&string_id);
	new(string) std::string(reader->LoadString(string_id));
}
}

namespace zodiac_test
{

void RegisterEngineAddons(asIScriptEngine * engine)
{
	// Only register engine app-types that have a matching Zodiac save/load entry
	// (see RegisterZodiacAddons). Zodiac's SaveModules requires EVERY registered
	// application object type to have a Zodiac entry, so registering e.g. the
	// array add-on without its (currently uncompilable) Zodiac glue makes even an
	// int-only module fail with "Missing entry for loading/saving ::array".
	// Until zodiac_addon.hpp is made compilable, string is the only paired type.
	RegisterStdString(engine);
}

void RegisterZodiacAddons(Zodiac::zIZodiac * zodiac)
{
	// Only std::string for now (see note above re: zodiac_addon.hpp).
	zodiac->RegisterValueType<std::string>(
		Zodiac::zIZodiac::GetTypeId<std::string>(),
		sizeof(std::string), "string",
		Zodiac::ZodiacSaveString, Zodiac::ZodiacLoadString, nullptr);
}

}
