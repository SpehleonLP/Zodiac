// Red-hunt: interface-typed handles. A global typed as a script interface holds
// a concrete implementing class; after a round-trip it must still be the impl
// with its state intact, reachable through the interface slot.
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

TEST(Interface, InterfaceHandleHoldsImpl)
{
	const char * script =
		"interface I { int val(); }\n"
		"class C : I { int x; int val() { return x; } }\n"
		"I@ h;\n"
		"void setup() { C@ c = C(); c.x = 55; @h = c; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", script), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		auto zodiac = MakeZodiac(e.get());
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success) << zodiac->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto zodiac = MakeZodiac(e.get());
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success) << zodiac->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asIScriptObject * obj = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("h")));
	ASSERT_NE(obj, nullptr) << "interface global h is null after restore";
	asITypeInfo * ti = obj->GetObjectType();
	ASSERT_NE(ti, nullptr);
	EXPECT_STREQ(ti->GetName(), "C") << "restored object lost its concrete type";
	EXPECT_EQ(PropInt(obj, "x"), 55) << "impl state x not restored";
}
