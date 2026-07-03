// Task 1 (Arc-1 hot reload): module-scoped save via zIZodiac::SetSaveScope.
//
// A scoped save writes ONLY the named module (bytecode/types/globals + the graph
// reachable from them); other modules and engine-level global properties are
// excluded. SetSaveScope(nullptr)/unset preserves whole-engine behavior.
//
// The round-trip uses the two-file pattern the other tests use: save into one
// zCMemoryFile, then load from a fresh zCMemoryFile seeded with those bytes
// (zCMemoryFile has no rewind()).
#include <gtest/gtest.h>
#include "test_engine.h"
#include "memory_file.h"
#include "addons.h"
#include <zodiac.h>

#include <angelscript.h>
#include <memory>

using namespace Zodiac;
using namespace zodiac_test;

namespace {

constexpr char kSrcA[] = R"(
	int aGlobal = 7;
	class AThing { int x = 3; }
	AThing g_a;
)";
constexpr char kSrcB[] = R"(
	int bGlobal = 9;
)";

void Compile(asIScriptEngine * engine, const char * name, const char * src)
{
	asIScriptModule * mod = engine->GetModule(name, asGM_ALWAYS_CREATE);
	ASSERT_NE(mod, nullptr);
	ASSERT_EQ(mod->AddScriptSection(name, src), 0);
	ASSERT_GE(mod->Build(), 0);
}

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto z = zCreateZodiac(engine);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}

} // namespace

TEST(ModuleScope, ScopedSaveExcludesOtherModules)
{
	std::vector<char> image;
	{
		TestEngine engine;
		Compile(engine.get(), "A", kSrcA);
		Compile(engine.get(), "B", kSrcB);
		auto z = MakeZodiac(engine.get());
		z->SetSaveScope("A");
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());
	{
		TestEngine engine;   // fresh engine, no modules
		auto z = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();

		// A restored (bytecode was saved), B never existed in the file.
		ASSERT_NE(engine.get()->GetModule("A", asGM_ONLY_IF_EXISTS), nullptr);
		EXPECT_EQ(engine.get()->GetModule("B", asGM_ONLY_IF_EXISTS), nullptr);

		auto * a = engine.get()->GetModule("A", asGM_ONLY_IF_EXISTS);
		ASSERT_NE(a, nullptr);
		int * aGlobal = (int*)a->GetAddressOfGlobalVar(a->GetGlobalVarIndexByName("aGlobal"));
		ASSERT_NE(aGlobal, nullptr);
		EXPECT_EQ(*aGlobal, 7);
	}
}

TEST(ModuleScope, ScopeOfMissingModuleFails)
{
	TestEngine engine;
	Compile(engine.get(), "A", kSrcA);
	auto z = MakeZodiac(engine.get());
	z->SetSaveScope("DoesNotExist");
	zCMemoryFile file;
	EXPECT_EQ(z->SaveToFile(&file), zE_ModuleDoesNotExist);
}

TEST(ModuleScope, UnsetScopeStillSavesEverything)
{
	std::vector<char> image;
	{
		TestEngine engine;
		Compile(engine.get(), "A", kSrcA);
		Compile(engine.get(), "B", kSrcB);
		auto z = MakeZodiac(engine.get());
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());
	{
		TestEngine engine;
		auto z = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
		EXPECT_NE(engine.get()->GetModule("A", asGM_ONLY_IF_EXISTS), nullptr);
		EXPECT_NE(engine.get()->GetModule("B", asGM_ONLY_IF_EXISTS), nullptr);
	}
}
