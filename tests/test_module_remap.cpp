// Task 2 (Arc-1 hot reload): load-side module remap + no-bytecode scoped load.
//
// The hot-reload load path. A scoped save taken with zZP_SAVE_BYTECODE off
// records a module's globals/types/graph but NO bytecode (the code is expected
// to already exist in the live engine after a recompile). SetModuleRemap lets
// that save restore into a DIFFERENTLY named, freshly compiled module:
//   serialize pkg#1 (no bytecode) -> compile pkg#2 from new source -> remap ->
//   load -> pkg#2's globals carry pkg#1's saved state.
//
// Uses the two-file image pattern (save into one zCMemoryFile, load from a fresh
// one seeded with those bytes) because zCMemoryFile has no rewind(). Object
// globals are read via memcpy off the pointer slot (UBSan-clean), mirroring
// test_object_graph.cpp's ReadHandle.
#include <gtest/gtest.h>
#include "test_engine.h"
#include "memory_file.h"
#include "addons.h"
#include <zodiac.h>

#include <angelscript.h>
#include <memory>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace {

constexpr char kSrc[] = R"(
	class Ball { int hits = 0; float elasticity = 0.2f; }
	Ball g_ball;
)";

void Compile(asIScriptEngine * engine, const char * name, const char * src)
{
	asIScriptModule * mod = engine->GetModule(name, asGM_ALWAYS_CREATE);
	ASSERT_NE(mod, nullptr);
	ASSERT_EQ(mod->AddScriptSection(name, src), 0);
	ASSERT_GE(mod->Build(), 0);
}

// For a non-handle script-object global (`Ball g_ball;`), GetAddressOfGlobalVar
// returns the object pointer itself (not a handle slot), so cast directly. (A
// handle global `Ball@ g;` would instead need a memcpy off the pointer slot.)
asIScriptObject * ReadObjectGlobal(asIScriptModule * mod, const char * name)
{
	int idx = mod->GetGlobalVarIndexByName(name);
	if(idx < 0) return nullptr;
	return (asIScriptObject*)mod->GetAddressOfGlobalVar(idx);
}

} // namespace

TEST(ModuleRemap, RestoreAgainstRecompiledModule)
{
	TestEngine engine;
	Compile(engine.get(), "pkg#1", kSrc);

	// Mutate live state so we can prove it round-trips.
	asIScriptModule * m1 = engine.get()->GetModule("pkg#1", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m1, nullptr);
	asIScriptObject * ball1 = ReadObjectGlobal(m1, "g_ball");
	ASSERT_NE(ball1, nullptr);
	*(int*)ball1->GetAddressOfProperty(0) = 5;    // hits

	std::vector<char> image;
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetProperty(zZP_SAVE_BYTECODE, false);   // hot-reload mode: code comes from compile
		z->SetSaveScope("pkg#1");
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	Compile(engine.get(), "pkg#2", kSrc);           // "recompiled" module, same source

	{
		auto z = zCreateZodiac(engine.get());       // fresh instance (zE_DoubleLoad guard)
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
	}

	asIScriptModule * m2 = engine.get()->GetModule("pkg#2", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m2, nullptr);
	asIScriptObject * ball2 = ReadObjectGlobal(m2, "g_ball");
	ASSERT_NE(ball2, nullptr);
	EXPECT_EQ(ball2->GetObjectType()->GetModule(), m2);   // restored against the NEW module
	EXPECT_EQ(*(int*)ball2->GetAddressOfProperty(0), 5);  // state survived
}

TEST(ModuleRemap, MissingTargetModuleFailsCleanly)
{
	TestEngine engine;
	Compile(engine.get(), "pkg#1", kSrc);

	std::vector<char> image;
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetProperty(zZP_SAVE_BYTECODE, false);
		z->SetSaveScope("pkg#1");
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "does-not-exist");
		zCMemoryFile file(image);
		EXPECT_EQ(z->LoadFromFile(&file), zE_ModuleDoesNotExist);
	}
}
