// Third red-hunt pass (2026-07-02): suspended-context STACK LOCALS of types the
// trunk-API context adaptation never exercised. The existing context tests cover
// int/string/array<int>/dictionary/any/grid<int> locals and a class `this`; these
// push on three seams that are all broken:
//
//   Family B — a NESTED container (array<array<...>>) as a stack local. The
//     top-level global and script-class-member equivalents round-trip fine, but
//     as a live stack local it SEGVs on resume. Four repros pin the cause: it is
//     not the list-buffer temp (the resize-built variant crashes too) and not
//     value-vs-handle (the `@` handle variant crashes too) — it is the nested
//     container on the stack itself.
//
//   Family C — a by-reference (&inout) parameter frame (requires unsafe refs,
//     a valid single-threaded config). The paused callee frame's param is a
//     REFERENCE to the caller's local; the reference/dontDereference restore slot
//     is untested and SEGVs for both a primitive and a value type.
//
//   Family D — a `ref` (CScriptHandle) or `weakref` value-type local. UBSan flags
//     a MISALIGNED access to the handle object inside the add-on glue, then it
//     SEGVs in Set(). POD (Vec2) and non-POD (Widget) value locals round-trip
//     fine — this is specific to the ASHANDLE/weakref value types on the stack.
//
// The four GREEN tests (POD value, non-POD value, funcdef, delegate locals) are
// the contrast that localizes the reds.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptarray/scriptarray.h"

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
void WidgetCopyConstruct(void * m, const Widget& o){ new(m) Widget(o); }
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
void Yield() { asIScriptContext * c = asGetActiveContext(); if(c) c->Suspend(); }
void RegisterYield(asIScriptEngine * e)
{ ASSERT_GE(e->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL), 0); }
void EnableUnsafeRefs(asIScriptEngine * e)
{ e->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 1); }

struct Payload { asIScriptContext * ctx = nullptr; uint32_t id = 0; };
void WriteCtx(zIZodiacWriter * w, void * p)
{ auto pl = static_cast<Payload*>(p); pl->id = w->SaveContext(pl->ctx); w->GetFile()->Write(&pl->id); }
void ReadCtx(zIZodiacReader * r, void * p)
{ auto pl = static_cast<Payload*>(p); r->GetFile()->Read(&pl->id); pl->ctx = r->LoadContext(pl->id); }
int RunToFinish(asIScriptContext * ctx)
{
	int guard = 0, state = ctx->GetState();
	while((state == asEXECUTION_SUSPENDED || state == asEXECUTION_PREPARED) && guard++ < 1000)
		state = ctx->Execute();
	EXPECT_EQ(state, asEXECUTION_FINISHED) << "resumed context did not finish (state " << state << ")";
	return (int)ctx->GetReturnDWord();
}
std::vector<char> SaveWithContext(asIScriptEngine * e, asIScriptContext * ctx)
{
	auto z = MakeZodiac(e);
	Payload pl; pl.ctx = ctx;
	z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
	zCMemoryFile f;
	EXPECT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
	return f.bytes();
}
asIScriptContext * LoadContextFrom(asIScriptEngine * e, const std::vector<char> & image)
{
	auto z = MakeZodiac(e);
	Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
	zCMemoryFile f(image);
	EXPECT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	return pl.ctx;
}

// Build `src`, prepare `run()`, execute until the first Suspend(), and return the
// suspended context. `configure` runs on both the save-side and load-side engines
// (register app types / yield / engine properties) before the module is built.
template<typename Cfg>
std::vector<char> SaveSuspended(Cfg configure, const char * src)
{
	TestEngine e; configure(e.get());
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m", src), 0);
	EXPECT_GE(mod->Build(), 0);
	asIScriptContext * ctx = e->RequestContext();
	ctx->Prepare(mod->GetFunctionByDecl("int run()"));
	EXPECT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
	auto image = SaveWithContext(e.get(), ctx);
	e->ReturnContext(ctx);
	return image;
}
template<typename Cfg>
int RestoreAndRun(Cfg configure, const char * src, const std::vector<char> & image)
{
	TestEngine e; configure(e.get());
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("m", src); mod->Build();
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	EXPECT_NE(ctx, nullptr);
	if(!ctx) return -999;
	int r = RunToFinish(ctx);
	e->ReturnContext(ctx);
	return r;
}
auto CfgBasic  = [](asIScriptEngine * e){ RegisterAppTypes(e); RegisterYield(e); };
auto CfgUnsafe = [](asIScriptEngine * e){ EnableUnsafeRefs(e); RegisterAppTypes(e); RegisterYield(e); };
} // namespace

// ---- GREEN contrast: value / funcdef / delegate locals round-trip fine ----

TEST(ContextLocalType, PodValueLocal)
{
	const char * src = "int run(){ Vec2 v; v.x=3; v.y=4; yield(); return v.x + v.y; }\n";  // 7
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 7);
}

TEST(ContextLocalType, NonPodValueLocal)
{
	const char * src = "int run(){ Widget w; w.payload=55; yield(); return w.payload; }\n";
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 55);
}

TEST(ContextLocalType, FuncdefLocal)
{
	const char * src =
		"funcdef int Op(int);\n"
		"int dbl(int a){ return a * 2; }\n"
		"int run(){ Op@ f = dbl; yield(); return f(21); }\n";  // 42
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 42);
}

TEST(ContextLocalType, DelegateLocal)
{
	const char * src =
		"funcdef int Op(int);\n"
		"class C { int base; int add(int a){ return base + a; } }\n"
		"int run(){ C c; c.base = 100; Op@ f = Op(c.add); yield(); return f(5); }\n";  // 105
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 105);
}

// ---- Family B (RED): a nested container as a stack local SEGVs on resume ----

TEST(ContextLocalType, NestedArrayLocal_ListInit)
{
	const char * src =
		"int run(){ array<array<int>> a = {{1,2},{3,4,5}}; yield();"
		" return a[0][1] + a[1][2]; }\n";  // 2 + 5 = 7
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 7)
		<< "list-initialized array<array<int>> stack local did not survive suspend";
}

// Not the list-buffer temp: built with resize()/insert, still crashes.
TEST(ContextLocalType, NestedArrayLocal_Resize)
{
	const char * src =
		"int run(){ array<array<int>> a; a.resize(2); a[0].insertLast(1);"
		" a[0].insertLast(2); a[1].insertLast(5); yield();"
		" return a[0][1] + a[1][0]; }\n";  // 2 + 5 = 7
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 7)
		<< "resize-built array<array<int>> stack local did not survive suspend";
}

// Heap leaves (string) — same crash.
TEST(ContextLocalType, NestedStringArrayLocal)
{
	const char * src =
		"int run(){ array<array<string>> a = {{'a','bb'},{'ccc'}}; yield();"
		" return int(a[0][1].length()) + int(a[1][0].length()); }\n";  // 2 + 3 = 5
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 5)
		<< "nested string-array stack local did not survive suspend";
}

// Not value-vs-handle: routing the nested array through an @ handle still crashes.
TEST(ContextLocalType, NestedArrayHandleLocal)
{
	const char * src =
		"int run(){ array<array<int>>@ a = array<array<int>> = {{1,2},{3}}; yield();"
		" return a[0][1] + a[1][0]; }\n";  // 2 + 3 = 5
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 5)
		<< "nested-array @ handle stack local did not survive suspend";
}

// ---- Family C (RED): a &inout reference-param frame SEGVs on resume ----

TEST(ContextLocalType, IntRefParamFrame)
{
	const char * src =
		"void bump(int &inout v){ yield(); v += 100; }\n"
		"int run(){ int v = 3; bump(v); return v; }\n";  // 103
	auto image = SaveSuspended(CfgUnsafe, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgUnsafe, src, image), 103)
		<< "&inout int reference-param frame did not survive suspend";
}

TEST(ContextLocalType, ValueTypeRefParamFrame)
{
	const char * src =
		"void bump(Vec2 &inout v){ yield(); v.x += 100; }\n"
		"int run(){ Vec2 v; v.x=1; v.y=2; bump(v); return v.x + v.y; }\n";  // 103
	auto image = SaveSuspended(CfgUnsafe, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgUnsafe, src, image), 103)
		<< "&inout Vec2 reference-param frame did not survive suspend";
}

// ---- Family D (RED): a ref / weakref value-type local misaligns then SEGVs ----

TEST(ContextLocalType, RefHandleLocal)
{
	const char * src =
		"class Node { int v; }\n"
		"int run(){ Node@ n=Node(); n.v=7; ref r(@n); yield();"
		" Node@ b = cast<Node>(r); return b.v; }\n";  // 7
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 7)
		<< "ref (CScriptHandle) stack local did not survive suspend";
}

TEST(ContextLocalType, WeakRefLocal)
{
	const char * src =
		"class Node { int v; }\n"
		"int run(){ Node@ n=Node(); n.v=9; weakref<Node> w(n); yield();"
		" Node@ b = w.get(); return b is null ? -1 : b.v; }\n";  // 9
	auto image = SaveSuspended(CfgBasic, src);
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(RestoreAndRun(CfgBasic, src, image), 9)
		<< "weakref stack local did not survive suspend";
}
