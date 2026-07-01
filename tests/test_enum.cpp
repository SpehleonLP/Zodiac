// Red-hunt: enums as members and as globals. Enum-typed storage is int-sized but
// carries a script enum type; probes whether the type tables and property/global
// value paths handle enum typeIds.
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
int PropInt(asIScriptObject * o, const char * n)
{
	if(!o) return -0xBEEF;
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == n)
		{ int v=-1; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return -0xBEEF;
}
}

TEST(Enum, MemberAndGlobalRoundTrip)
{
	const char * script =
		"enum Color { Red, Green = 5, Blue }\n"   // Blue == 6
		"class E { Color c; }\n"
		"E@ box;\n"
		"Color gc = Blue;\n"
		"void setup() { @box = E(); box.c = Green; }\n";

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
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success);
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto zodiac = MakeZodiac(e.get());
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "load failed: " << zodiac->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);

	int gc = mod->GetGlobalVarIndexByName("gc");
	ASSERT_GE(gc, 0);
	int gcv = 0; std::memcpy(&gcv, mod->GetAddressOfGlobalVar(gc), sizeof(gcv));
	EXPECT_EQ(gcv, 6) << "enum global gc(Blue) not restored";

	int bi = mod->GetGlobalVarIndexByName("box");
	asIScriptObject * box = ReadHandle(mod->GetAddressOfGlobalVar(bi));
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(PropInt(box, "c"), 5) << "enum member c(Green) not restored";
}
