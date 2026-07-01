// Red-hunt: two top-level GLOBALS that alias one object. Existing aliasing
// coverage is object-member -> object; here two module globals point at the same
// instance. Identity + value must survive.
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
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
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

TEST(GlobalAliasing, TwoGlobalsAliasOneObject)
{
	const char * src =
		"class Obj { int v; }\n"
		"Obj@ g1; Obj@ g2;\n"
		"void setup() { @g1 = Obj(); g1.v = 7; @g2 = g1; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", src), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		auto z = MakeZodiac(e.get());
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto z = MakeZodiac(e.get());
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asIScriptObject * g1 = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("g1")));
	asIScriptObject * g2 = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("g2")));
	ASSERT_NE(g1, nullptr); ASSERT_NE(g2, nullptr);
	EXPECT_EQ(PropInt(g1, "v"), 7);
	EXPECT_EQ(g1, g2) << "two globals aliasing one object became two distinct restored objects";
}
