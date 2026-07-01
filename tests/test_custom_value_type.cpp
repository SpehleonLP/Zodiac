// Red-hunt: an application-registered VALUE type other than std::string — the
// only app type the harness currently pairs with Zodiac glue. A POD value type
// used as both a script-class member and a global exercises the value-type
// app-object save/load path with a fresh type.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <memory>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
struct Vec2 { int x; int y; };

void SaveVec2(zIZodiacWriter * w, Vec2 const* v, int&)
{ w->GetFile()->Write(&v->x); w->GetFile()->Write(&v->y); }
void LoadVec2(zIZodiacReader * r, Vec2 * v, int&, bool)
{ r->GetFile()->Read(&v->x); r->GetFile()->Read(&v->y); }

void RegisterVec2(asIScriptEngine * e)
{
	int r = e->RegisterObjectType("Vec2", sizeof(Vec2),
		asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<Vec2>());
	ASSERT_GE(r, 0);
	ASSERT_GE(e->RegisterObjectProperty("Vec2", "int x", offsetof(Vec2, x)), 0);
	ASSERT_GE(e->RegisterObjectProperty("Vec2", "int y", offsetof(Vec2, y)), 0);
}

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get()); // string
	z->RegisterValueType<Vec2>(zIZodiac::GetTypeId<Vec2>(), sizeof(Vec2), "Vec2",
		SaveVec2, LoadVec2, nullptr);
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}
asIScriptObject * ReadHandle(void * a)
{ asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p; }
Vec2 MemberVec2(asIScriptObject * o, const char * n)
{
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == n)
		{ Vec2 v{}; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return Vec2{-1,-1};
}
}

TEST(CustomValueType, Vec2MemberAndGlobalRoundTrip)
{
	const char * src =
		"class Holder { Vec2 v; }\n"
		"Holder@ h;\n"
		"Vec2 gv;\n"
		"void setup() { @h = Holder(); h.v.x = 3; h.v.y = 4; gv.x = 5; gv.y = 6; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		RegisterVec2(e.get());
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
	RegisterVec2(e.get());
	auto z = MakeZodiac(e.get());
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);

	asIScriptObject * h = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("h")));
	ASSERT_NE(h, nullptr);
	Vec2 hv = MemberVec2(h, "v");
	EXPECT_EQ(hv.x, 3); EXPECT_EQ(hv.y, 4) << "Vec2 member not restored";

	int gi = mod->GetGlobalVarIndexByName("gv");
	ASSERT_GE(gi, 0);
	Vec2 gv{}; std::memcpy(&gv, mod->GetAddressOfGlobalVar(gi), sizeof(gv));
	EXPECT_EQ(gv.x, 5); EXPECT_EQ(gv.y, 6) << "Vec2 global not restored";
}
