// Part A acceptance: array<T> round-trips through Zodiac now that the container
// add-on glue in zodiac_addon.hpp compiles and is wired into the harness.
//   * array<int>       — POD element buffer.
//   * empty array vs null array@ — size 0 vs a null handle.
//   * array<string>    — nested heap (each element is its own value-type object).
//   * array<Node@>      — handle elements; identity preserved when a global aliases
//                         one of the array's elements.
//
// Container globals are declared as handles (`array<T>@`) and assigned in setup():
// Zodiac restores a handle-typed global by (re)creating the object (the isHandle
// path in ZodiacLoad), whereas a by-value global would require the slot to be
// pre-constructed before restore. Handles mirror the working `Box@` string test.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptstdstring/scriptstdstring.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

void BuildAndSetup(asIScriptEngine * engine, const char * src)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("m", src), 0);
	ASSERT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	ASSERT_NE(setup, nullptr);
	asIScriptContext * ctx = engine->CreateContext();
	ctx->Prepare(setup);
	ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();
}

std::vector<char> SaveEngine(asIScriptEngine * engine)
{
	auto zodiac = MakeZodiac(engine);
	zCMemoryFile file;
	auto rc = zodiac->SaveToFile(&file);
	if(rc != zE_Success) fprintf(stderr, "SAVE ERR %d: %s\n", rc, zodiac->GetErrorString());
	EXPECT_EQ(rc, zE_Success);
	return file.bytes();
}

void LoadEngine(asIScriptEngine * engine, const std::vector<char> & image)
{
	auto zodiac = MakeZodiac(engine);
	zCMemoryFile file(image);
	auto rc = zodiac->LoadFromFile(&file);
	if(rc != zE_Success) fprintf(stderr, "LOAD ERR %d: %s\n", rc, zodiac->GetErrorString());
	ASSERT_EQ(rc, zE_Success);
}

template<typename T>
T * GlobalObject(asIScriptEngine * engine, const char * name)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(mod, nullptr);
	if(!mod) return nullptr;
	int gidx = mod->GetGlobalVarIndexByName(name);
	EXPECT_GE(gidx, 0);
	return *reinterpret_cast<T **>(mod->GetAddressOfGlobalVar(gidx));
}
}

TEST(ContainerArray, IntValues)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"array<int>@ nums;\n"
			"void setup(){ array<int> a = {10,20,30}; @nums = a; }\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto arr = GlobalObject<CScriptArray>(engine.get(), "nums");
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(arr->GetSize(), 3u);
	EXPECT_EQ(*static_cast<int*>(arr->At(0)), 10);
	EXPECT_EQ(*static_cast<int*>(arr->At(1)), 20);
	EXPECT_EQ(*static_cast<int*>(arr->At(2)), 30);
}

TEST(ContainerArray, EmptyArrayVsNullHandle)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"array<int>@ empty;\n"
			"array<int>@ nil;\n"
			"void setup(){ array<int> a; @empty = a; /* nil stays null */ }\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto empty = GlobalObject<CScriptArray>(engine.get(), "empty");
	ASSERT_NE(empty, nullptr);
	EXPECT_EQ(empty->GetSize(), 0u);

	auto nil = GlobalObject<CScriptArray>(engine.get(), "nil");
	EXPECT_EQ(nil, nullptr);
}

TEST(ContainerArray, StringValues)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"array<string>@ words;\n"
			"void setup(){ array<string> a = {'alpha', '', 'a longer string value'}; @words = a; }\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto arr = GlobalObject<CScriptArray>(engine.get(), "words");
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(arr->GetSize(), 3u);
	EXPECT_EQ(*static_cast<std::string*>(arr->At(0)), "alpha");
	EXPECT_EQ(*static_cast<std::string*>(arr->At(1)), "");
	EXPECT_EQ(*static_cast<std::string*>(arr->At(2)), "a longer string value");
}

TEST(ContainerArray, HandleElementIdentityPreserved)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"array<Node@>@ nodes;\n"
			"Node@ alias;\n"
			"void setup(){\n"
			"  Node@ a = Node(); a.v = 1;\n"
			"  Node@ b = Node(); b.v = 2;\n"
			"  array<Node@> arr = {a, b};\n"
			"  @nodes = arr;\n"
			"  @alias = arr[0];\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto nodes = GlobalObject<CScriptArray>(engine.get(), "nodes");
	ASSERT_NE(nodes, nullptr);
	ASSERT_EQ(nodes->GetSize(), 2u);

	auto elem0 = *static_cast<asIScriptObject**>(nodes->At(0));
	auto elem1 = *static_cast<asIScriptObject**>(nodes->At(1));
	ASSERT_NE(elem0, nullptr);
	ASSERT_NE(elem1, nullptr);
	EXPECT_NE(elem0, elem1);
	EXPECT_EQ(*static_cast<int*>(elem0->GetAddressOfProperty(0)), 1);
	EXPECT_EQ(*static_cast<int*>(elem1->GetAddressOfProperty(0)), 2);

	auto alias = GlobalObject<asIScriptObject>(engine.get(), "alias");
	EXPECT_EQ(alias, elem0) << "global alias must resolve to the same object as nodes[0]";
}
