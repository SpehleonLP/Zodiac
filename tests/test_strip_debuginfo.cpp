// Red-hunt: STRIP_DEBUGINFO is a realistic ship config (smaller saves). If the
// load path resolves properties/globals by debug-only names, stripping breaks
// restore. Save with zZP_STRIP_DEBUGINFO on, then round-trip an object graph.
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
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e, bool strip)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	z->SetProperty(zZP_STRIP_DEBUGINFO, strip);
	return z;
}
asIScriptObject * ReadHandle(void * a)
{ asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p; }
int PropInt(asIScriptObject * o, const char * n)
{
	if(!o) return -0xBEEF;
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == n)
		{ int v=-1; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return -0xBEEF;
}
}

TEST(StripDebugInfo, RoundTripWithStrippedBytecode)
{
	const char * src =
		"class Node { int value; Node@ next; }\n"
		"Node@ head;\n"
		"void setup() { Node@ a = Node(); Node@ b = Node(); a.value=1; b.value=2; @a.next=b; @head=a; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", src), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		auto z = MakeZodiac(e.get(), /*strip*/true);
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto z = MakeZodiac(e.get(), true);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asIScriptObject * head = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("head")));
	ASSERT_NE(head, nullptr) << "head null after stripped-bytecode restore";
	EXPECT_EQ(PropInt(head, "value"), 1);
	asIScriptObject * next = ReadHandle([&]{ for(asUINT i=0;i<head->GetPropertyCount();++i) if(std::string(head->GetPropertyName(i))=="next") return head->GetAddressOfProperty(i); return (void*)nullptr; }());
	ASSERT_NE(next, nullptr) << "head.next lost with stripped debug info";
	EXPECT_EQ(PropInt(next, "value"), 2);
}
