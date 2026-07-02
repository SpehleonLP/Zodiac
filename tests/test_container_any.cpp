// Part A acceptance: any@ (CScriptAny) round-trips through Zodiac. `any` is a
// tagged union like the dictionary value; here we cover the two representative
// payloads: a primitive (int64) and a script-object handle.
//
// Mirrors test_container_array.cpp: the any global is an `@` handle assigned in
// setup(); save into a zCMemoryFile, reload into a fresh engine, read back.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptany/scriptany.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
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

TEST(ContainerAny, PrimitiveInt64)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"any@ a;\n"
			"void setup(){\n"
			"  any x;\n"
			"  int64 v = 1234567890123;\n"
			"  x.store(v);\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto any = GlobalObject<CScriptAny>(engine.get(), "a");
	ASSERT_NE(any, nullptr);

	asINT64 v = 0;
	ASSERT_TRUE(any->Retrieve(v));
	EXPECT_EQ(v, 1234567890123LL);
}

TEST(ContainerAny, HandleIdentity)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"any@ a;\n"
			"void setup(){\n"
			"  Node@ n = Node(); n.v = 55;\n"
			"  any x;\n"
			"  x.store(@n);\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto any = GlobalObject<CScriptAny>(engine.get(), "a");
	ASSERT_NE(any, nullptr);

	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asITypeInfo * nodeType = mod->GetTypeInfoByName("Node");
	ASSERT_NE(nodeType, nullptr);
	int nodeHandleTypeId = nodeType->GetTypeId() | asTYPEID_OBJHANDLE;

	// Retrieve on a handle-typed slot AddRef's the returned reference.
	asIScriptObject * node = nullptr;
	ASSERT_TRUE(any->Retrieve(&node, nodeHandleTypeId));
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(*static_cast<int*>(node->GetAddressOfProperty(0)), 55);
	node->Release();
}
