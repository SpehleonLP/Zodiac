// Facade exception-boundary hardening (spec Group P, docs/specs/2026-07-02-
// io-error-checking-and-propagation.md).
//
// The facade (SaveToFile/LoadFromFile in src/z_zodiac.cpp) is a C-style,
// Code-returning API: nothing may propagate out of it except a returned
// Zodiac::Code. Two gaps this file covers:
//
//   P3 - LoadFromFile's first try-block used to RETHROW a bare Code out of the
//        function when changedEngineState was true (a load that already
//        mutated the target engine before failing). That escaped the
//        contract. It now reports zE_EngineCorrupted as a RETURNED code.
//        `Facade.PartialRestoreFailureReportsEngineCorrupted` is a real
//        corrupt-image regression test for this: it builds a valid image,
//        corrupts a zCProperty::typeId field for a script-object-handle
//        member to a value that (a) passes Verify()'s bounds check (which
//        only fires when asTYPEID_MASK_OBJECT is set - handle members are
//        stored as a bare small index with no mask bits, see the comment in
//        tests/test_malformed_tables.cpp's PropertyObjectTypeIdOutOfRange)
//        and (b) is out of range for zCZodiacReader::LoadTypeId, which is
//        only reached from ProcessModules AFTER LoadByteCode has already
//        created the module and loaded its bytecode into the target engine.
//        That ordering is exactly what flips changedEngineState to true.
//
//   P1 - The catch blocks used to catch only Exception& and Code& - a stray
//        std::exception (or anything else) would escape unhandled. This is
//        now a total catch (see Change A/C in the task brief). A
//        deterministic, portable trigger for a RAW std::exception (not
//        Zodiac::Exception/Code) crossing the boundary was not found:
//        std::bad_alloc is not deterministic/portable to trigger safely, and
//        every other internal failure path already throws Zodiac::Exception
//        or Zodiac::Code. Per the task brief this backstop is SKIPPED - the
//        total catch is a code-review-only backstop with no cheap,
//        deterministic trigger available. (Left undone intentionally; do not
//        fake a trigger.)
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"
#include "z_zodiacstate.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// Same shape of graph as test_malformed_tables.cpp: a script class with a
// primitive member and a handle member, so the properties table has both a
// primitive-typed property (typeId <= asTYPEID_DOUBLE) and a script-object
// handle property (typeId stored as a bare small index, no mask bits).
const char * kScript =
	"class Node { int value; Node@ next; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"    Node@ a = Node();\n"
	"    Node@ b = Node();\n"
	"    a.value = 111; b.value = 222;\n"
	"    @a.next = b; @head = a;\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

std::vector<char> BuildValidImage()
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kScript), 0);
	EXPECT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	EXPECT_NE(setup, nullptr);
	asIScriptContext * ctx = engine->CreateContext();
	EXPECT_GE(ctx->Prepare(setup), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	return file.bytes();
}

zCHeader * HeaderOf(std::vector<char> & image)
{
	return reinterpret_cast<zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
}

zCProperty * Properties(std::vector<char> & i)
{
	return reinterpret_cast<zCProperty *>(i.data() + HeaderOf(i)->propertiesOffset);
}
}

// Sanity - the uncorrupted image loads. Guards against a false-green where
// the corrupted test below is rejected for the wrong reason.
TEST(Facade, ValidImageLoads)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());
	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "baseline should load: " << zodiac->GetErrorString();
	EXPECT_NE(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr);
}

// P3 - the key test. Corrupt a script-object-handle property's typeId to a
// value that slips past Verify() (which only range-checks typeId when
// asTYPEID_MASK_OBJECT is set) but is out of range when
// zCZodiacReader::LoadTypeId is later called from ProcessModules - which
// runs AFTER LoadByteCode(m_engine) has already created module "m" and
// loaded its bytecode into the target engine. That failure-after-mutation
// must be reported as a RETURNED zE_EngineCorrupted, never rethrown/escaped.
TEST(Facade, PartialRestoreFailureReportsEngineCorrupted)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->propertiesLength, 0u);

	zCProperty * p = Properties(image);
	bool corrupted = false;
	for(uint32_t i = 0; i < h->propertiesLength; ++i)
	{
		// The handle member (Node@ next) is stored as a bare small index (no
		// asTYPEID_MASK_OBJECT bits) once it exceeds the primitive range; the
		// primitive member (int value) has typeId <= asTYPEID_DOUBLE and is
		// left alone.
		if(p[i].typeId > asTYPEID_DOUBLE && !(p[i].typeId & asTYPEID_MASK_OBJECT))
		{
			// Out of range for LoadTypeId's typeTableLength() bound, but with
			// no mask bits so Verify()'s check at z_zodiacreader.cpp:267-269
			// never evaluates the bound at all.
			p[i].typeId = h->typeInfoLength + h->templatesLength + 1000;
			corrupted = true;
			break;
		}
	}
	ASSERT_TRUE(corrupted) << "no script-object-handle property found to corrupt";

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);

	Code rc = zE_Success;
	EXPECT_NO_THROW({ rc = zodiac->LoadFromFile(&file); })
		<< "a post-mutation failure must be reported as a Code, never thrown "
		   "out of the C-style API";
	EXPECT_EQ(rc, zE_EngineCorrupted)
		<< "a failure after LoadByteCode already mutated the target engine "
		   "must report zE_EngineCorrupted, not the underlying Code, and "
		   "must not rethrow (" << zodiac->GetErrorString() << ")";
	EXPECT_NE(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr)
		<< "sanity: LoadByteCode should have already created the module "
		   "before ProcessModules threw - this is what changedEngineState "
		   "reflects";
}

// P1 backstop - see the file-level comment: no cheap, deterministic trigger
// of a raw std::exception crossing the boundary was found, so it is
// intentionally not tested here. The total catch added to SaveToFile/
// LoadFromFile (src/z_zodiac.cpp) is a code-review-only backstop.
