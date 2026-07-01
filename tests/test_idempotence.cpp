// Red-hunt: round-trip STABILITY. Save an engine, load it into a fresh engine,
// then save THAT. A faithful serializer should produce a structurally identical
// image the second time. Byte-for-byte equality may be too strict (table
// ordering, transient ids), so the primary assertion is that the re-saved image
// reloads into a correct graph; image-size equality is a softer signal.
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
asIScriptObject * ReadHandle(void * a)
{
	asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p;
}
int NodeValue(asIScriptObject * o)
{
	if(!o) return -1;
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == "value")
		{ int v=-1; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return -1;
}

const char * kScript =
	"class Node { int value; Node@ next; Node@ alias; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"  Node@ a = Node(); Node@ b = Node();\n"
	"  a.value=111; b.value=222; @a.next=b; @a.alias=b; @b.alias=b; @head=a; }\n";

// Load `image`, then immediately re-save the loaded engine and return the new image.
std::vector<char> LoadThenResave(const std::vector<char> & image)
{
	TestEngine e;
	auto zodiac = MakeZodiac(e.get());
	zCMemoryFile in(image);
	EXPECT_EQ(zodiac->LoadFromFile(&in), zE_Success) << zodiac->GetErrorString();

	auto zodiac2 = MakeZodiac(e.get());
	zCMemoryFile out;
	EXPECT_EQ(zodiac2->SaveToFile(&out), zE_Success) << zodiac2->GetErrorString();
	return out.bytes();
}
}

TEST(Idempotence, SaveLoadSaveIsStable)
{
	std::vector<char> image1;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		auto zodiac = MakeZodiac(e.get());
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success);
		image1 = file.bytes();
	}
	ASSERT_FALSE(image1.empty());

	std::vector<char> image2 = LoadThenResave(image1);
	ASSERT_FALSE(image2.empty());

	// Softer signal: a stable serializer re-emits the same size.
	EXPECT_EQ(image2.size(), image1.size())
		<< "re-saved image differs in size — save/load is not stable";

	// Primary: image2 must still reload into the correct graph.
	TestEngine e;
	auto zodiac = MakeZodiac(e.get());
	zCMemoryFile in(image2);
	ASSERT_EQ(zodiac->LoadFromFile(&in), zE_Success) << zodiac->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asIScriptObject * head = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("head")));
	ASSERT_NE(head, nullptr) << "head lost after load->save->load";
	EXPECT_EQ(NodeValue(head), 111);
}
