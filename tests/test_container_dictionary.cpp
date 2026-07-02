// Part A acceptance: dictionary@ round-trips through Zodiac. The dictionary is
// a tagged-union value store, so this exercises all three value paths in one
// object graph:
//   * a primitive (int -> stored as int64),
//   * a string value-type,
//   * a script-object handle (Node@) whose identity/property must survive.
// A second test covers the degenerate empty dictionary (GetSize()==0).
//
// Mirrors test_container_array.cpp: container globals are `@` handles assigned
// in setup(); save into a zCMemoryFile, reload into a fresh engine, read back.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptdictionary/scriptdictionary.h"
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

TEST(ContainerDictionary, MixedValueTypes)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"dictionary@ d;\n"
			"void setup(){\n"
			"  dictionary dd;\n"
			"  dd.set('i', 42);\n"
			"  dd.set('s', 'hello');\n"
			"  Node@ n = Node(); n.v = 7;\n"
			"  dd.set('n', @n);\n"
			"  @d = dd;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto dict = GlobalObject<CScriptDictionary>(engine.get(), "d");
	ASSERT_NE(dict, nullptr);
	ASSERT_EQ(dict->GetSize(), 3u);

	// int -> stored as int64
	asINT64 i = 0;
	ASSERT_TRUE(dict->Get("i", i));
	EXPECT_EQ(i, 42);

	// string value-type
	std::string s;
	int strTypeId = engine->GetTypeIdByDecl("string");
	ASSERT_GE(strTypeId, 0);
	ASSERT_TRUE(dict->Get("s", &s, strTypeId));
	EXPECT_EQ(s, "hello");

	// script-object handle: identity + property survive.
	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asITypeInfo * nodeType = mod->GetTypeInfoByName("Node");
	ASSERT_NE(nodeType, nullptr);
	int nodeHandleTypeId = nodeType->GetTypeId() | asTYPEID_OBJHANDLE;

	asIScriptObject * node = nullptr;
	// Get() on a handle-typed slot AddRef's the returned reference.
	ASSERT_TRUE(dict->Get("n", &node, nodeHandleTypeId));
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(*static_cast<int*>(node->GetAddressOfProperty(0)), 7);
	node->Release();
}

TEST(ContainerDictionary, EmptyDictionary)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"dictionary@ d;\n"
			"void setup(){ dictionary dd; @d = dd; }\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto dict = GlobalObject<CScriptDictionary>(engine.get(), "d");
	ASSERT_NE(dict, nullptr);
	EXPECT_EQ(dict->GetSize(), 0u);
}
