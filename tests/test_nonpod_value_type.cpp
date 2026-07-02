// Red-hunt S3 / Part G2: an application-registered NON-POD value type (a real
// constructor + destructor, registered asOBJ_APP_CLASS_CDAK, NOT asOBJ_POD).
// Exercises GetByteLengthOfType for a non-POD registered value type, which
// previously returned 0 (the "//what to do??" gap) instead of the real
// registered size. Used as both a script-class member and a module global.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// Non-POD: has a user-defined constructor, copy-constructor, destructor and
// assignment, so asGetTypeTraits would NOT yield asOBJ_POD. We register it
// explicitly as asOBJ_APP_CLASS_CDAK.
struct Widget
{
	int payload;
	Widget()               : payload(0)         {}
	Widget(const Widget& o): payload(o.payload) {}
	~Widget()              {}
	Widget& operator=(const Widget& o) { payload = o.payload; return *this; }
};

void WidgetConstruct(void * mem)                     { new(mem) Widget(); }
void WidgetCopyConstruct(void * mem, const Widget& o){ new(mem) Widget(o); }
void WidgetDestruct(void * mem)                      { static_cast<Widget*>(mem)->~Widget(); }

void SaveWidget(zIZodiacWriter * w, Widget const* v, int&)
{ w->GetFile()->Write(&v->payload); }
void LoadWidget(zIZodiacReader * r, Widget * v, int&, bool)
{ r->GetFile()->Read(&v->payload); }

void RegisterWidget(asIScriptEngine * e)
{
	int r = e->RegisterObjectType("Widget", sizeof(Widget),
		asOBJ_VALUE | asOBJ_APP_CLASS_CDAK);
	ASSERT_GE(r, 0);
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

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get()); // string
	z->RegisterValueType<Widget>(zIZodiac::GetTypeId<Widget>(), sizeof(Widget), "Widget",
		SaveWidget, LoadWidget, nullptr);
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}
asIScriptObject * ReadHandle(void * a)
{ asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p; }
int MemberWidgetPayload(asIScriptObject * o, const char * n)
{
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == n)
		{ Widget * w = static_cast<Widget*>(o->GetAddressOfProperty(i)); return w ? w->payload : -1; }
	return -1;
}
}

TEST(NonPodValueType, WidgetMemberAndGlobalRoundTrip)
{
	const char * src =
		"class Holder { Widget w; }\n"
		"Holder@ h;\n"
		"Widget gw;\n"
		"void setup() { @h = Holder(); h.w.payload = 42; gw.payload = 99; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		RegisterWidget(e.get());
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
	RegisterWidget(e.get());
	auto z = MakeZodiac(e.get());
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);

	asIScriptObject * h = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("h")));
	ASSERT_NE(h, nullptr);
	EXPECT_EQ(MemberWidgetPayload(h, "w"), 42) << "non-POD Widget member not restored";

	int gi = mod->GetGlobalVarIndexByName("gw");
	ASSERT_GE(gi, 0);
	Widget * gw = static_cast<Widget*>(mod->GetAddressOfGlobalVar(gi));
	ASSERT_NE(gw, nullptr);
	EXPECT_EQ(gw->payload, 99) << "non-POD Widget global not restored";
}
