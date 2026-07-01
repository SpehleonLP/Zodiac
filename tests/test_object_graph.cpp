// Spec test 8: object-graph smoke. A graph of SCRIPT-declared objects with
// reference aliasing, a self-reference, and a null handle must round-trip
// through Zodiac with its graph shape preserved.
//
// CRITICAL: only script-declared classes + primitives + handles are used — NO
// string/array/dictionary/engine-registered stdlib type in the script, because
// ProcessTypes throws "Missing entry ..." for any non-POD engine-registered
// type without a Zodiac entry (those addon entries don't exist yet). Script
// module classes need no entry.
//
// The return-code EXPECT may stay RED on the known Part-D bug (SaveToFile
// returns uninitialized error_code); the CONTENT/graph assertions are the point
// of this test and must execute and pass regardless.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// head -> A;  A.next -> B;  B.next -> null;  A.alias -> B (aliases A.next);
// B.alias -> B (self-reference).  Distinct value ints identify the nodes.
const char * kScript =
	"class Node { int value; Node@ next; Node@ alias; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"    Node@ a = Node();\n"
	"    Node@ b = Node();\n"
	"    a.value = 111;\n"
	"    b.value = 222;\n"
	"    @a.next  = b;\n"
	"    @b.next  = null;\n"
	"    @a.alias = b;\n"
	"    @b.alias = b;\n"
	"    @head = a;\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

// Reach into a script object by property name; returns the address of that
// property's storage, or nullptr if not found.
void * PropAddr(asIScriptObject * obj, const char * name)
{
	if(obj == nullptr) return nullptr;
	for(asUINT i = 0; i < obj->GetPropertyCount(); ++i)
	{
		const char * pn = obj->GetPropertyName(i);
		if(pn != nullptr && std::string(pn) == name)
			return obj->GetAddressOfProperty(i);
	}
	return nullptr;
}

// AngelScript may hand back a 4-aligned address for a handle slot; read the
// pointer via memcpy so the (correct) load stays UBSan-clean.
asIScriptObject * ReadHandle(void * addr)
{
	if(addr == nullptr) return nullptr;
	asIScriptObject * p = nullptr;
	std::memcpy(&p, addr, sizeof(p));
	return p;
}

int NodeValue(asIScriptObject * obj)
{
	void * a = PropAddr(obj, "value");
	if(a == nullptr) return -0x0BADBEEF;
	int v = 0;
	std::memcpy(&v, a, sizeof(v));
	return v;
}

asIScriptObject * NodeHandle(asIScriptObject * obj, const char * name)
{
	return ReadHandle(PropAddr(obj, name));
}
}

TEST(ObjectGraph, RefAliasingAndNullRoundTrip)
{
	std::vector<char> image;

	// --- save side: build the graph via setup(), then snapshot the engine ---
	{
		TestEngine engine; // constructor already registers the string add-on

		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_NE(mod, nullptr);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
		ASSERT_NE(setup, nullptr);
		asIScriptContext * ctx = engine->CreateContext();
		ASSERT_NE(ctx, nullptr);
		ASSERT_GE(ctx->Prepare(setup), 0);
		ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
		ctx->Release();

		// sanity: graph is correct BEFORE the save (isolates round-trip failures
		// from graph-construction failures).
		int gidx = mod->GetGlobalVarIndexByName("head");
		ASSERT_GE(gidx, 0);
		asIScriptObject * head = ReadHandle(mod->GetAddressOfGlobalVar(gidx));
		ASSERT_NE(head, nullptr);
		EXPECT_EQ(NodeValue(head), 111);
		asIScriptObject * next = NodeHandle(head, "next");
		ASSERT_NE(next, nullptr);
		EXPECT_EQ(NodeValue(next), 222);
		EXPECT_EQ(NodeHandle(next, "next"), nullptr);
		EXPECT_EQ(NodeHandle(head, "alias"), next);       // pre-save aliasing
		EXPECT_EQ(NodeHandle(next, "alias"), next);       // pre-save self-ref

		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file;
		Code rc = zodiac->SaveToFile(&file);
		EXPECT_EQ(rc, zE_Success)
			<< "SaveToFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";

		image = file.bytes();
		ASSERT_FALSE(image.empty());
	}

	// --- load side: fresh engine, restore from the in-memory image ---
	{
		TestEngine engine; // constructor already registers the string add-on
		auto zodiac = MakeZodiac(engine.get());

		zCMemoryFile file(image);
		Code rc = zodiac->LoadFromFile(&file);
		EXPECT_EQ(rc, zE_Success)
			<< "LoadFromFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";

		asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
		ASSERT_NE(mod, nullptr) << "restored module 'm' missing";

		int gidx = mod->GetGlobalVarIndexByName("head");
		ASSERT_GE(gidx, 0) << "restored global 'head' missing";
		void * gaddr = mod->GetAddressOfGlobalVar(gidx);
		ASSERT_NE(gaddr, nullptr);
		asIScriptObject * head = ReadHandle(gaddr);
		ASSERT_NE(head, nullptr) << "restored head is null";

		// Group 1: chain values + terminal null.
		EXPECT_EQ(NodeValue(head), 111) << "head.value not restored";
		asIScriptObject * next = NodeHandle(head, "next");
		ASSERT_NE(next, nullptr) << "head.next lost across round-trip";
		EXPECT_EQ(NodeValue(next), 222) << "head.next.value not restored";
		EXPECT_EQ(NodeHandle(next, "next"), nullptr)
			<< "head.next.next should be a restored null handle";

		// Group 2: aliasing preserved — two handles that pointed at the SAME
		// object pre-save must point at the SAME restored object (equal ptrs,
		// not two copies).
		EXPECT_EQ(NodeHandle(head, "alias"), next)
			<< "aliasing not preserved: head.alias and head.next are distinct restored objects";

		// Group 3: self-reference preserved.
		EXPECT_EQ(NodeHandle(next, "alias"), next)
			<< "self-reference not preserved: B.alias should point at B itself";
	}
}
