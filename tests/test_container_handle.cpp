// Part A acceptance: ref@ (CScriptHandle) round-trips through Zodiac. A global
// `ref@` pointing at a script object (Node with a known property) must, after
// round-trip, still resolve (GetRef() non-null) to an object with that value.
//
// Mirrors test_container_array.cpp: the ref global is an `@` handle assigned in
// setup(); save into a zCMemoryFile, reload into a fresh engine, read back.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scripthandle/scripthandle.h"

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

// CScriptHandle (`ref`) is an asOBJ_VALUE + asOBJ_ASHANDLE type: even declared as
// `ref@`, the global slot stores the CScriptHandle OBJECT inline, not a pointer to
// one. So the global's address IS the object -- read it directly, do NOT deref an
// extra level the way the ref-type container tests (array/dictionary) do.
template<typename T>
T * GlobalValue(asIScriptEngine * engine, const char * name)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(mod, nullptr);
	if(!mod) return nullptr;
	int gidx = mod->GetGlobalVarIndexByName(name);
	EXPECT_GE(gidx, 0);
	return reinterpret_cast<T *>(mod->GetAddressOfGlobalVar(gidx));
}
}

TEST(ContainerHandle, ResolvesToScriptObject)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"ref@ r;\n"
			"void setup(){\n"
			"  Node@ n = Node(); n.v = 99;\n"
			"  ref h(@n);\n"
			"  @r = h;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto handle = GlobalValue<CScriptHandle>(engine.get(), "r");
	ASSERT_NE(handle, nullptr);

	void * ref = handle->GetRef();
	ASSERT_NE(ref, nullptr);
	auto node = static_cast<asIScriptObject*>(ref);
	EXPECT_EQ(*static_cast<int*>(node->GetAddressOfProperty(0)), 99);
}

// A `ref` that holds nothing (GetType()==null) round-trips as empty. This is
// green regression coverage for a genuine footgun: `ref` is asOBJ_ASHANDLE, so the
// global slot stores the CScriptHandle INLINE -- read it via GlobalValue (no extra
// deref). The save side writes typeId 0 for a null handle and the load side must
// restore an empty handle, NOT crash or fabricate a dangling reference.
TEST(ContainerHandle, EmptyRefRoundTrips)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"ref@ r;\n"
			"void setup(){\n"
			"  ref x;\n"        // never assigned an object
			"  @r = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto handle = GlobalValue<CScriptHandle>(engine.get(), "r");  // ASHANDLE: inline
	ASSERT_NE(handle, nullptr);
	EXPECT_EQ(handle->GetType(), nullptr) << "empty ref must round-trip empty";
	EXPECT_EQ(handle->GetRef(), nullptr);
}
