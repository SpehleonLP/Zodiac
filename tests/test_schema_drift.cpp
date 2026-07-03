// Spec test 11: schema-drift behavior, pinned as the executable spec for the
// FUTURE hot-reload arc. This test documents the CURRENT (pre-hot-reload)
// outcomes when a saved type's shape no longer matches the live type:
//
//   * property REMOVED  (save {a,b,s} → load into {a,s})    : succeeds (Task 4
//     drift tolerance); the removed 'b' is dropped and the trailing sentinel 's'
//     still restores, proving the restore loop continued past the gap.
//   * property ADDED    (save {a,b,s} → load into {a,b,s,c}): succeeds; the stored
//     fields restore, the new 'c' is left at its default (never visited — the
//     restore loop walks the STORED type's properties).
//
// Task 4 FLIPPED the removed-property case from a hard reject
// (zE_UnableToRestoreProperty) to drift-skip + zE_Success. This test is the
// tripwire that proves that behavior change was intentional, not an accident.
//
// Mechanism: bytecode is saved, but the load engine PRE-COMPILES module "m" from
// the drifted source, so LoadByteCode sees the module already exists and keeps
// the drifted type; unification then runs stored-vs-drifted by property name.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// Saved shape: class C { int a; int b; int sentinel; } with a live instance
// a=111, b=222, sentinel=333. 'sentinel' is declared AFTER 'b' so a removed-'b'
// drift can prove the restore loop continued past the gap and still placed 's'.
const char * kSaveSource =
	"class C { int a; int b; int sentinel; }\n"
	"C@ obj;\n"
	"void setup() {\n"
	"    @obj = C();\n"
	"    obj.a = 111;\n"
	"    obj.b = 222;\n"
	"    obj.sentinel = 333;\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

int PropInt(asIScriptObject * obj, const char * name)
{
	if(obj == nullptr) return -0x0BADBEEF;
	for(asUINT i = 0; i < obj->GetPropertyCount(); ++i)
	{
		const char * pn = obj->GetPropertyName(i);
		if(pn && std::strcmp(pn, name) == 0)
		{
			int v = 0;
			std::memcpy(&v, obj->GetAddressOfProperty(i), sizeof(v));
			return v;
		}
	}
	return -0x0BADBEEF;
}

asIScriptObject * ReadHandle(void * addr)
{
	if(addr == nullptr) return nullptr;
	asIScriptObject * p = nullptr;
	std::memcpy(&p, addr, sizeof(p));
	return p;
}

std::vector<char> SaveImage()
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kSaveSource), 0);
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

// Pre-compile module "m" from `driftSource` in a fresh engine, then load the
// saved image over it. Returns the load Code and (out) the restored module.
Code LoadIntoDrifted(const char * driftSource, std::vector<char> & image, TestEngine & engine, asIScriptModule *& outMod)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", driftSource), 0);
	EXPECT_GE(mod->Build(), 0);

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	Code rc = zodiac->LoadFromFile(&file);
	outMod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	return rc;
}
}

// Property REMOVED: stored 'b' has nowhere to go → Task 4 drift-skips it and the
// load SUCCEEDS. 'a' (before the gap) and 'sentinel' (after it) both restore,
// proving the restore loop continued past the dropped property; 'b' is absent.
TEST(SchemaDrift, RemovedPropertySkipped)
{
	std::vector<char> image = SaveImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	asIScriptModule * mod = nullptr;
	Code rc = LoadIntoDrifted("class C { int a; int sentinel; }\nC@ obj;\n", image, engine, mod);

	ASSERT_EQ(rc, zE_Success)
		<< "removed-property drift should now skip the missing property and load; got "
		<< (int)rc;
	ASSERT_NE(mod, nullptr);

	int gidx = mod->GetGlobalVarIndexByName("obj");
	ASSERT_GE(gidx, 0);
	asIScriptObject * obj = ReadHandle(mod->GetAddressOfGlobalVar(gidx));
	ASSERT_NE(obj, nullptr) << "restored 'obj' is null";

	EXPECT_EQ(PropInt(obj, "a"), 111) << "property before the gap not restored";
	EXPECT_EQ(PropInt(obj, "sentinel"), 333) << "property after the gap not restored (loop stopped early)";
	// 'b' no longer exists on the live type; it was dropped, not misplaced.
	EXPECT_EQ(PropInt(obj, "b"), -0x0BADBEEF) << "removed 'b' should be absent";
}

// Property ADDED: stored {a,b} both place; the new 'c' is never visited and
// keeps its default. Load succeeds (current behavior).
TEST(SchemaDrift, AddedPropertyLoadsLeavingNewDefault)
{
	std::vector<char> image = SaveImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	asIScriptModule * mod = nullptr;
	Code rc = LoadIntoDrifted("class C { int a; int b; int sentinel; int c; }\nC@ obj;\n", image, engine, mod);

	ASSERT_EQ(rc, zE_Success) << "added-property drift should currently load";
	ASSERT_NE(mod, nullptr);

	int gidx = mod->GetGlobalVarIndexByName("obj");
	ASSERT_GE(gidx, 0);
	asIScriptObject * obj = ReadHandle(mod->GetAddressOfGlobalVar(gidx));
	ASSERT_NE(obj, nullptr) << "restored 'obj' is null";

	EXPECT_EQ(PropInt(obj, "a"), 111) << "stored 'a' not restored";
	EXPECT_EQ(PropInt(obj, "b"), 222) << "stored 'b' not restored";
	EXPECT_EQ(PropInt(obj, "sentinel"), 333) << "stored 'sentinel' not restored";
	// 'c' is intentionally NOT asserted to any value — it is left at whatever the
	// uninitialized script object carries; the point is only that load succeeded.
}
