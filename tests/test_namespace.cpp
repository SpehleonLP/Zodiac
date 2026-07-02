// Red-hunt: namespaced script types. All existing tests use global-namespace
// classes; this drives the name/nameSpace string-index pairing in the type
// tables. A namespaced class instance held by a namespaced global must
// round-trip with its value intact.
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
	if(!o) return -1;
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == n)
		{ int v=-1; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return -1;
}
}

TEST(Namespace, NamespacedTypeAndGlobalRoundTrip)
{
	const char * script =
		"namespace Foo {\n"
		"  class Bar { int x; }\n"
		"  Bar@ g;\n"
		"}\n"
		"void setup() { @Foo::g = Foo::Bar(); Foo::g.x = 99; }\n";

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
	// namespaced global lookup
	mod->SetDefaultNamespace("Foo");
	int gidx = mod->GetGlobalVarIndexByName("g");
	mod->SetDefaultNamespace("");
	ASSERT_GE(gidx, 0) << "namespaced global Foo::g missing after restore";
	asIScriptObject * obj = ReadHandle(mod->GetAddressOfGlobalVar(gidx));
	ASSERT_NE(obj, nullptr) << "Foo::g is null after restore";
	EXPECT_EQ(PropInt(obj, "x"), 99) << "Foo::Bar.x not restored";
}
