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
#include <unistd.h>

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

// Nested shape: Outer owns Inner by COMPOSITION (`Inner b;`, no `@`), so restoring
// Outer recurses into Inner through PopulateTable (the non-handle script-object
// member branch) — the ONLY loop over m_properties that a top-level global does not
// exercise. `sentinel` (`z`) is declared after `y` so a removed-`y` drift proves the
// PopulateTable loop continued past the gap.
const char * kNestedSaveSource =
	"class Inner { int x; int y; int z; }\n"
	"class Outer { Inner b; }\n"
	"Outer@ o;\n"
	"void setup() {\n"
	"    @o = Outer();\n"
	"    o.b.x = 11;\n"
	"    o.b.y = 22;\n"
	"    o.b.z = 33;\n"
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

std::vector<char> SaveNestedImage()
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kNestedSaveSource), 0);
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
}

// Property REMOVED on a NESTED, composed type (Outer owns `Inner b`): the restore
// reaches Inner's removed-property slot through PopulateTable's non-handle-member
// walk, NOT the two RestoreScriptObjectContents loops a top-level global uses. This
// is the loop the top-level RemovedPropertySkipped test does not cover. Pre-fix the
// PopulateTable loop hit `assert(writeType == GetPropertyTypeId(~0u))` and ABORTED
// on legitimate nested drift; post-fix it drift-skips and loads. Run in a death test
// so a regression's abort is contained (ExitedWithCode(0) == GREEN, a SIGABRT == RED).
TEST(SchemaDrift, NestedRemovedPropertySkipped)
{
	std::vector<char> image = SaveNestedImage();
	ASSERT_FALSE(image.empty());

	// Inner drops the middle 'y'; Outer's composition of Inner is unchanged.
	const char * drift =
		"class Inner { int x; int z; }\n"
		"class Outer { Inner b; }\n"
		"Outer@ o;\n";

	EXPECT_EXIT({
		TestEngine engine;
		asIScriptModule * mod = nullptr;
		Code rc = LoadIntoDrifted(drift, image, engine, mod);
		if(rc != zE_Success || mod == nullptr) _exit(1);

		int oidx = mod->GetGlobalVarIndexByName("o");
		if(oidx < 0) _exit(2);
		asIScriptObject * outer = ReadHandle(mod->GetAddressOfGlobalVar(oidx));
		if(outer == nullptr) _exit(3);

		// `b` is a composed (non-handle) Inner member: GetAddressOfProperty yields the
		// object pointer directly; a handle member would need a deref (ReadHandle).
		asIScriptObject * inner = nullptr;
		for(asUINT i = 0; i < outer->GetPropertyCount(); ++i)
		{
			const char * pn = outer->GetPropertyName(i);
			if(pn && std::strcmp(pn, "b") == 0)
			{
				void * addr = outer->GetAddressOfProperty(i);
				inner = (outer->GetPropertyTypeId(i) & asTYPEID_OBJHANDLE)
				        ? ReadHandle(addr) : (asIScriptObject*)addr;
				break;
			}
		}
		if(inner == nullptr) _exit(4);

		if(PropInt(inner, "x") != 11) _exit(5);   // survives, before the gap
		if(PropInt(inner, "z") != 33) _exit(6);   // survives, after the gap
		if(PropInt(inner, "y") != -0x0BADBEEF) _exit(7);   // removed -> absent
		_exit(0);
	}, ::testing::ExitedWithCode(0), ".*")
		<< "nested removed-property drift must skip and load, not abort";
}
