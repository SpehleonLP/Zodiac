// Part A acceptance: weakref<Node>@ (CScriptWeakRef) round-trips through Zodiac.
// A weakref does not keep its target alive; whether Get() resolves depends on
// whether some *strong* reference survives. Two cases:
//   * LIVE:    the target is also held by a strong global handle, so it survives
//              the round-trip and Get() resolves to that same object (identity).
//   * EXPIRED: the only strong reference is dropped before save. Get() reads null
//              pre-save (asserted, so the test is meaningful) and must stay null
//              after the round-trip.
//
// Mirrors test_container_array.cpp: the weakref global is an `@` handle assigned
// in setup(); save into a zCMemoryFile, reload into a fresh engine, read back.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/weakref/weakref.h"

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

// A script-object handle global (`Node@`) stores a POINTER: deref one level.
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

// CScriptWeakRef (`weakref<T>`) is an asOBJ_VALUE + asOBJ_ASHANDLE type: the global
// slot holds the CScriptWeakRef OBJECT inline, so the global's address IS the object.
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

TEST(ContainerWeakRef, LiveTargetIdentityPreserved)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"Node@ strong;\n"
			"weakref<Node>@ wr;\n"
			"void setup(){\n"
			"  Node@ n = Node(); n.v = 88;\n"
			"  @strong = n;\n"          // strong ref keeps it alive
			"  weakref<Node> w(n);\n"
			"  @wr = w;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);

	auto strong = GlobalObject<asIScriptObject>(engine.get(), "strong");
	ASSERT_NE(strong, nullptr);
	EXPECT_EQ(*static_cast<int*>(strong->GetAddressOfProperty(0)), 88);

	auto weak = GlobalValue<CScriptWeakRef>(engine.get(), "wr");
	ASSERT_NE(weak, nullptr);
	// Get() returns the referenced object with an added reference, or null.
	void * got = weak->Get();
	ASSERT_NE(got, nullptr) << "weakref to a still-live object must resolve";
	EXPECT_EQ(got, strong) << "weakref must resolve to the same object as the strong global";
	engine->ReleaseScriptObject(got, weak->GetRefType());
}

TEST(ContainerWeakRef, ExpiredTargetReadsNull)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"weakref<Node>@ wr;\n"
			"class Node { int v; }\n"
			"void setup(){\n"
			"  Node@ n = Node();\n"
			"  weakref<Node> w(n);\n"
			"  @n = null;\n"            // drop the only strong ref -> target destroyed
			"  @wr = w;\n"
			"}\n");

		// Pre-save: the weakref must already read null, otherwise the test would
		// prove nothing about the expired path.
		auto weak = GlobalValue<CScriptWeakRef>(engine.get(), "wr");
		ASSERT_NE(weak, nullptr);
		void * preGot = weak->Get();
		ASSERT_EQ(preGot, nullptr) << "target should have been destroyed before save";

		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto weak = GlobalValue<CScriptWeakRef>(engine.get(), "wr");
	ASSERT_NE(weak, nullptr);
	EXPECT_EQ(weak->Get(), nullptr) << "an expired weakref must round-trip as null";
}
