// Red-hunt: script class inheritance / polymorphism. A global handle statically
// typed as the base holds a derived instance. After a round-trip the restored
// object must still BE a Derived (both base and derived properties intact),
// reached through a Base@ slot.
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
int PropInt(asIScriptObject * o, const char * n)
{
	void * a = PropAddr(o, n); int v = -0xBEEF; if(a) std::memcpy(&v, a, sizeof(v)); return v;
}
asIScriptObject * ReadHandle(void * a)
{
	asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p;
}
}

TEST(Inheritance, BaseHandleHoldsDerived)
{
	const char * script =
		"class Base { int a; }\n"
		"class Derived : Base { int b; }\n"
		"Base@ h;\n"
		"void setup() { Derived@ d = Derived(); d.a = 11; d.b = 22; @h = d; }\n";

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
	int gidx = mod->GetGlobalVarIndexByName("h");
	ASSERT_GE(gidx, 0);
	asIScriptObject * obj = ReadHandle(mod->GetAddressOfGlobalVar(gidx));
	ASSERT_NE(obj, nullptr) << "restored h is null";

	// Concrete type must still be Derived, not sliced to Base.
	asITypeInfo * ti = obj->GetObjectType();
	ASSERT_NE(ti, nullptr);
	EXPECT_STREQ(ti->GetName(), "Derived") << "restored object was sliced to its base type";
	EXPECT_EQ(PropInt(obj, "a"), 11) << "base property 'a' not restored";
	EXPECT_EQ(PropInt(obj, "b"), 22) << "derived property 'b' not restored";
}
