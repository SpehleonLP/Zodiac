// Red-hunt: multiple modules. Every existing round-trip test uses a single
// module named "m". Zodiac snapshots the whole engine, so two modules should
// both round-trip. Probes module-index handling in the type/global tables.
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
asIScriptModule * BuildMod(asIScriptEngine * e, const char * name, const char * src)
{
	asIScriptModule * mod = e->GetModule(name, asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection(name, src), 0);
	EXPECT_GE(mod->Build(), 0);
	return mod;
}
void RunSetup(asIScriptEngine * e, asIScriptModule * mod)
{
	asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
	EXPECT_NE(s, nullptr);
	asIScriptContext * ctx = e->CreateContext();
	ctx->Prepare(s); EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
}
}

TEST(MultiModule, TwoModulesRoundTrip)
{
	const char * srcA =
		"class A { int va; }\n A@ ga;\n void setup() { @ga = A(); ga.va = 100; }\n";
	const char * srcB =
		"class B { int vb; }\n B@ gb;\n void setup() { @gb = B(); gb.vb = 200; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * a = BuildMod(e.get(), "a", srcA);
		asIScriptModule * b = BuildMod(e.get(), "b", srcB);
		RunSetup(e.get(), a);
		RunSetup(e.get(), b);
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

	asIScriptModule * a = e->GetModule("a", asGM_ONLY_IF_EXISTS);
	asIScriptModule * b = e->GetModule("b", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(a, nullptr) << "module 'a' missing after restore";
	ASSERT_NE(b, nullptr) << "module 'b' missing after restore";
	asIScriptObject * ga = ReadHandle(a->GetAddressOfGlobalVar(a->GetGlobalVarIndexByName("ga")));
	asIScriptObject * gb = ReadHandle(b->GetAddressOfGlobalVar(b->GetGlobalVarIndexByName("gb")));
	ASSERT_NE(ga, nullptr); ASSERT_NE(gb, nullptr);
	EXPECT_EQ(PropInt(ga, "va"), 100) << "module a global not restored";
	EXPECT_EQ(PropInt(gb, "vb"), 200) << "module b global not restored";
}
