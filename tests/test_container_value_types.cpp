// Third red-hunt pass (2026-07-02): application VALUE types as container elements
// / values. Two storage seams exist and they do NOT behave the same:
//
//   * array<T> / grid<T> reach the element through At(), which for a value-type
//     element yields the object pointer directly. Both a POD (Vec2) and a non-POD
//     (Widget) value type round-trip correctly here.  [GREEN — coverage]
//
//   * dictionary / any store the value in a tagged 8-byte union and reach it via
//     GetAddressOfValue()/GetObjectAddress(). A POD value type round-trips, but a
//     NON-POD value type comes back CORRUPT (uninitialized payload).  [RED]
//
// The four GREEN tests are the contrast that localizes the two RED tests: the
// bug is not "value types in containers" broadly, it is specifically the
// dictionary/any value-union path for a non-POD registered value type.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptgrid/scriptgrid.h"
#include "add_on/scriptany/scriptany.h"
#include "add_on/scriptdictionary/scriptdictionary.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// A POD value type (memcpy-able) and a NON-POD value type (real ctor/copy/dtor).
struct Vec2 { int x; int y; };
void SaveVec2(zIZodiacWriter * w, Vec2 const* v, int&)
{ w->GetFile()->Write(&v->x); w->GetFile()->Write(&v->y); }
void LoadVec2(zIZodiacReader * r, Vec2 * v, int&, bool)
{ r->GetFile()->Read(&v->x); r->GetFile()->Read(&v->y); }
void RegisterVec2(asIScriptEngine * e)
{
	ASSERT_GE(e->RegisterObjectType("Vec2", sizeof(Vec2),
		asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<Vec2>()), 0);
	ASSERT_GE(e->RegisterObjectProperty("Vec2", "int x", offsetof(Vec2, x)), 0);
	ASSERT_GE(e->RegisterObjectProperty("Vec2", "int y", offsetof(Vec2, y)), 0);
}

struct Widget
{
	int payload;
	Widget()               : payload(0)         {}
	Widget(const Widget& o): payload(o.payload) {}
	~Widget()              {}
	Widget& operator=(const Widget& o) { payload = o.payload; return *this; }
};
void WidgetConstruct(void * m)                     { new(m) Widget(); }
// asCALL_CDECL_OBJLAST => the destination object pointer is the LAST native arg.
void WidgetCopyConstruct(const Widget& o, void * m){ new(m) Widget(o); }
void WidgetDestruct(void * m)                      { static_cast<Widget*>(m)->~Widget(); }
void SaveWidget(zIZodiacWriter * w, Widget const* v, int&)
{ w->GetFile()->Write(&v->payload); }
void LoadWidget(zIZodiacReader * r, Widget * v, int&, bool)
{ r->GetFile()->Read(&v->payload); }
void RegisterWidget(asIScriptEngine * e)
{
	ASSERT_GE(e->RegisterObjectType("Widget", sizeof(Widget),
		asOBJ_VALUE | asOBJ_APP_CLASS_CDAK), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Widget", asBEHAVE_CONSTRUCT, "void f()",
		asFUNCTION(WidgetConstruct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Widget", asBEHAVE_CONSTRUCT, "void f(const Widget &in)",
		asFUNCTION(WidgetCopyConstruct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Widget", asBEHAVE_DESTRUCT, "void f()",
		asFUNCTION(WidgetDestruct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(e->RegisterObjectMethod("Widget", "Widget &opAssign(const Widget &in)",
		asMETHODPR(Widget, operator=, (const Widget&), Widget&), asCALL_THISCALL), 0);
	ASSERT_GE(e->RegisterObjectProperty("Widget", "int payload", offsetof(Widget, payload)), 0);
}
void RegisterAppTypes(asIScriptEngine * e) { RegisterVec2(e); RegisterWidget(e); }

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->RegisterValueType<Vec2>(zIZodiac::GetTypeId<Vec2>(), sizeof(Vec2), "Vec2",
		SaveVec2, LoadVec2, nullptr);
	z->RegisterValueType<Widget>(zIZodiac::GetTypeId<Widget>(), sizeof(Widget), "Widget",
		SaveWidget, LoadWidget, nullptr);
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}
void BuildAndSetup(asIScriptEngine * e, const char * src)
{
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("m", src), 0);
	ASSERT_GE(mod->Build(), 0);
	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	ASSERT_NE(setup, nullptr);
	asIScriptContext * ctx = e->CreateContext();
	ctx->Prepare(setup);
	ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();
}
std::vector<char> SaveEngine(asIScriptEngine * e)
{
	auto z = MakeZodiac(e);
	zCMemoryFile f;
	auto rc = z->SaveToFile(&f);
	if(rc != zE_Success) fprintf(stderr, "SAVE ERR %d: %s\n", rc, z->GetErrorString());
	EXPECT_EQ(rc, zE_Success);
	return f.bytes();
}
void LoadEngine(asIScriptEngine * e, const std::vector<char> & image)
{
	auto z = MakeZodiac(e);
	zCMemoryFile f(image);
	auto rc = z->LoadFromFile(&f);
	if(rc != zE_Success) fprintf(stderr, "LOAD ERR %d: %s\n", rc, z->GetErrorString());
	ASSERT_EQ(rc, zE_Success);
}
template<typename T>
T * GlobalObject(asIScriptEngine * e, const char * name)
{
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(mod, nullptr); if(!mod) return nullptr;
	int gidx = mod->GetGlobalVarIndexByName(name);
	EXPECT_GE(gidx, 0);
	return *reinterpret_cast<T **>(mod->GetAddressOfGlobalVar(gidx));
}
} // namespace

// ---- GREEN: value-type elements reached through At() (array / grid) ----

TEST(ContainerValueType, PodInArray)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"array<Vec2>@ a;\n"
			"void setup(){ array<Vec2> x; x.resize(2);"
			" x[0].x=1; x[0].y=2; x[1].x=3; x[1].y=4; @a=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto a = GlobalObject<CScriptArray>(e.get(), "a");
	ASSERT_NE(a, nullptr); ASSERT_EQ(a->GetSize(), 2u);
	auto v1 = static_cast<Vec2*>(a->At(1));  // value-type element: At() = obj ptr
	ASSERT_NE(v1, nullptr);
	EXPECT_EQ(v1->x, 3); EXPECT_EQ(v1->y, 4);
}

TEST(ContainerValueType, NonPodInArray)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"array<Widget>@ a;\n"
			"void setup(){ array<Widget> x; x.resize(2);"
			" x[0].payload=11; x[1].payload=22; @a=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto a = GlobalObject<CScriptArray>(e.get(), "a");
	ASSERT_NE(a, nullptr); ASSERT_EQ(a->GetSize(), 2u);
	EXPECT_EQ(static_cast<Widget*>(a->At(0))->payload, 11);
	EXPECT_EQ(static_cast<Widget*>(a->At(1))->payload, 22);
}

TEST(ContainerValueType, PodInGrid)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"grid<Vec2>@ g;\n"
			"void setup(){ grid<Vec2> x(1,1); x[0,0].x=7; x[0,0].y=8; @g=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto g = GlobalObject<CScriptGrid>(e.get(), "g");
	ASSERT_NE(g, nullptr);
	auto v = static_cast<Vec2*>(g->At(0,0));
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(v->x, 7); EXPECT_EQ(v->y, 8);
}

TEST(ContainerValueType, NonPodInGrid)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"grid<Widget>@ g;\n"
			"void setup(){ grid<Widget> x(1,1); x[0,0].payload=33; @g=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto g = GlobalObject<CScriptGrid>(e.get(), "g");
	ASSERT_NE(g, nullptr);
	EXPECT_EQ(static_cast<Widget*>(g->At(0,0))->payload, 33);
}

// ---- dictionary / any value union: POD ok (GREEN), non-POD corrupt (RED) ----

TEST(ContainerValueType, PodInDictionary)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"dictionary@ d;\n"
			"void setup(){ dictionary x; Vec2 v; v.x=5; v.y=6; x.set('v', v); @d=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto d = GlobalObject<CScriptDictionary>(e.get(), "d");
	ASSERT_NE(d, nullptr);
	Vec2 out{};
	ASSERT_TRUE(d->Get("v", &out, e->GetTypeIdByDecl("Vec2"))) << "dictionary lost its Vec2 value";
	EXPECT_EQ(out.x, 5); EXPECT_EQ(out.y, 6);
}

// RED: a NON-POD value type stored as a dictionary value comes back corrupt.
TEST(ContainerValueType, NonPodInDictionary)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"dictionary@ d;\n"
			"void setup(){ dictionary x; Widget w; w.payload=77; x.set('w', w); @d=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto d = GlobalObject<CScriptDictionary>(e.get(), "d");
	ASSERT_NE(d, nullptr);
	Widget out;
	ASSERT_TRUE(d->Get("w", &out, e->GetTypeIdByDecl("Widget"))) << "dictionary lost its Widget value";
	EXPECT_EQ(out.payload, 77) << "non-POD value type corrupted through the dictionary value union";
}

TEST(ContainerValueType, PodInAny)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"any@ a;\n"
			"void setup(){ any x; Vec2 v; v.x=9; v.y=10; x.store(v); @a=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto a = GlobalObject<CScriptAny>(e.get(), "a");
	ASSERT_NE(a, nullptr);
	Vec2 out{};
	ASSERT_TRUE(a->Retrieve(&out, e->GetTypeIdByDecl("Vec2"))) << "any lost its Vec2 value";
	EXPECT_EQ(out.x, 9); EXPECT_EQ(out.y, 10);
}

// RED: a NON-POD value type stored in an `any` comes back corrupt.
TEST(ContainerValueType, NonPodInAny)
{
	std::vector<char> image;
	{
		TestEngine e; RegisterAppTypes(e.get());
		BuildAndSetup(e.get(),
			"any@ a;\n"
			"void setup(){ any x; Widget w; w.payload=88; x.store(w); @a=x; }\n");
		image = SaveEngine(e.get());
	}
	ASSERT_FALSE(image.empty());
	TestEngine e; RegisterAppTypes(e.get());
	LoadEngine(e.get(), image);
	auto a = GlobalObject<CScriptAny>(e.get(), "a");
	ASSERT_NE(a, nullptr);
	Widget out;
	ASSERT_TRUE(a->Retrieve(&out, e->GetTypeIdByDecl("Widget"))) << "any lost its Widget value";
	EXPECT_EQ(out.payload, 88) << "non-POD value type corrupted through the any value union";
}
