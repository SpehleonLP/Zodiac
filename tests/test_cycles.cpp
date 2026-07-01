// Red-hunt: reference-graph topologies beyond the existing object_graph test
// (which covers aliasing + self-ref + a terminal null, but no back-edge cycle
// and no deep chain). Only script-declared classes + handles + primitives are
// used, so no addon glue is needed.
//   * TwoCycle   — a<->b mutual references (a.next=b, b.next=a).
//   * Ring       — a->b->c->a.
//   * DeepChain  — a long linked list; probes recursion depth in the
//                  Save/LoadScriptObject walk (stack overflow => crash/red).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

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

void * PropAddr(asIScriptObject * obj, const char * name)
{
	if(!obj) return nullptr;
	for(asUINT i = 0; i < obj->GetPropertyCount(); ++i)
		if(obj->GetPropertyName(i) && std::string(obj->GetPropertyName(i)) == name)
			return obj->GetAddressOfProperty(i);
	return nullptr;
}
asIScriptObject * ReadHandle(void * a)
{
	asIScriptObject * p = nullptr;
	if(a) std::memcpy(&p, a, sizeof(p));
	return p;
}
int NodeValue(asIScriptObject * o)
{
	void * a = PropAddr(o, "value");
	int v = -1; if(a) std::memcpy(&v, a, sizeof(v));
	return v;
}

std::vector<char> Save(asIScriptEngine * engine, const char * script)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m", script), 0);
	EXPECT_GE(mod->Build(), 0);
	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	EXPECT_NE(setup, nullptr);
	asIScriptContext * ctx = engine->CreateContext();
	ctx->Prepare(setup);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

	auto zodiac = MakeZodiac(engine);
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	return file.bytes();
}

asIScriptObject * LoadHead(asIScriptEngine * engine, const std::vector<char> & image, const char * global = "head")
{
	auto zodiac = MakeZodiac(engine);
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success);
	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(mod, nullptr);
	int gidx = mod->GetGlobalVarIndexByName(global);
	EXPECT_GE(gidx, 0);
	return ReadHandle(mod->GetAddressOfGlobalVar(gidx));
}
}

TEST(Cycles, TwoCycle)
{
	const char * script =
		"class Node { int value; Node@ next; }\n"
		"Node@ head;\n"
		"void setup() {\n"
		"  Node@ a = Node(); Node@ b = Node();\n"
		"  a.value = 1; b.value = 2;\n"
		"  @a.next = b; @b.next = a;\n"   // mutual cycle
		"  @head = a; }\n";

	std::vector<char> image;
	{ TestEngine e; image = Save(e.get(), script); }
	ASSERT_FALSE(image.empty());

	TestEngine e;
	asIScriptObject * a = LoadHead(e.get(), image);
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(NodeValue(a), 1);
	asIScriptObject * b = ReadHandle(PropAddr(a, "next"));
	ASSERT_NE(b, nullptr) << "a.next lost";
	EXPECT_EQ(NodeValue(b), 2);
	// the cycle must close on the SAME object, not a fresh copy of a.
	EXPECT_EQ(ReadHandle(PropAddr(b, "next")), a)
		<< "cycle not closed: b.next should be the identical restored 'a'";
}

TEST(Cycles, Ring)
{
	const char * script =
		"class Node { int value; Node@ next; }\n"
		"Node@ head;\n"
		"void setup() {\n"
		"  Node@ a = Node(); Node@ b = Node(); Node@ c = Node();\n"
		"  a.value=1; b.value=2; c.value=3;\n"
		"  @a.next=b; @b.next=c; @c.next=a;\n"
		"  @head=a; }\n";

	std::vector<char> image;
	{ TestEngine e; image = Save(e.get(), script); }
	ASSERT_FALSE(image.empty());

	TestEngine e;
	asIScriptObject * a = LoadHead(e.get(), image);
	ASSERT_NE(a, nullptr);
	asIScriptObject * b = ReadHandle(PropAddr(a, "next"));
	asIScriptObject * c = ReadHandle(PropAddr(b, "next"));
	ASSERT_NE(c, nullptr);
	EXPECT_EQ(NodeValue(c), 3);
	EXPECT_EQ(ReadHandle(PropAddr(c, "next")), a) << "ring must close back on 'a'";
}

TEST(Cycles, DeepChain)
{
	// Build a long chain iteratively in script, then save. If the serializer
	// walks the graph recursively, a long-enough chain blows the stack.
	const char * script =
		"class Node { int value; Node@ next; }\n"
		"Node@ head;\n"
		"void setup() {\n"
		"  Node@ prev = null;\n"
		"  for(int i = 0; i < 50000; i++) {\n"
		"    Node@ n = Node(); n.value = i; @n.next = prev; @prev = n; }\n"
		"  @head = prev; }\n";

	std::vector<char> image;
	{ TestEngine e; image = Save(e.get(), script); }
	ASSERT_FALSE(image.empty());

	TestEngine e;
	asIScriptObject * n = LoadHead(e.get(), image);
	ASSERT_NE(n, nullptr);
	int count = 0;
	while(n && count < 60000) { n = ReadHandle(PropAddr(n, "next")); ++count; }
	EXPECT_EQ(count, 50000) << "deep chain length not preserved";
}
