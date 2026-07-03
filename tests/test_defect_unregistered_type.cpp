// Defect regression: a std::logic_error escaping the Code-returning facade API
// (src/z_zodiac.cpp:315-321, SortTypeList).
//
// When the engine has a non-POD application object type that was never given
// Zodiac save/load glue, SortTypeList throws std::logic_error. But SaveToFile()
// and LoadFromFile() catch ONLY `Zodiac::Exception&` and `Zodiac::Code&`
// (z_zodiac.cpp:71-80, :117-136), so the std::logic_error propagates OUT of the
// C-style, Code-returning API instead of being reported as a Zodiac::Code. For
// "just works" infra a missing-registration mistake must become a returned error
// code, never an unhandled exception crossing the boundary.
//
// This is exactly the "save an engine that is missing a registered type" case.
// Pre-fix: SaveToFile throws (RED — EXPECT_NO_THROW fails). Post-fix: it returns
// a non-success Code (GREEN). Fix is to widen the catch to std::exception&.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}
}

TEST(UnregisteredType, SaveWithUngluedNonPodTypeReturnsCodeNotThrow)
{
	TestEngine engine;
	// A non-POD application ref type registered with AngelScript but NOT paired
	// with Zodiac glue. SortTypeList iterates every app object type; this one has
	// no matching entry and is not POD, so it hits the logic_error throw.
	ASSERT_GE(engine->RegisterObjectType("Orphan", 0, asOBJ_REF | asOBJ_NOCOUNT), 0);

	// A trivial module so there is something to save.
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("m", "int g = 1;\n"), 0);
	ASSERT_GE(mod->Build(), 0);

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;

	Code rc = zE_Success;
	EXPECT_NO_THROW({ rc = zodiac->SaveToFile(&file); })
		<< "a missing Zodiac registration must be reported as a Code, not thrown "
		   "out of the C-style API as an unhandled std::logic_error";
	EXPECT_EQ(rc, zE_ObjectUnserializable)
		<< "a missing Zodiac registration for a non-POD type must return zE_ObjectUnserializable";
}
