// Red-hunt: objects reachable ONLY (or ALSO) through a suspended context's
// stack, and identity between a context-local handle and a global handle.
//   * ContextOnlyObjectSurvives — an object referenced only by an on-stack local
//     in a suspended context must be discovered, serialized, and restored.
//   * ContextGlobalAliasing — a context-local handle and a global that alias the
//     same object pre-save must alias the SAME restored object.
//   * SaveContextWithoutBytecodeRejected — saving a live context with bytecode
//     disabled should fail cleanly (zE_CantSaveContextWithoutBytecode).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e, bool bytecode = true)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, bytecode);
	return z;
}
void Yield() { asIScriptContext * c = asGetActiveContext(); if(c) c->Suspend(); }
void RegisterYield(asIScriptEngine * e)
{ ASSERT_GE(e->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL), 0); }
asIScriptModule * Build(asIScriptEngine * e, const char * src)
{
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m", src), 0);
	EXPECT_GE(mod->Build(), 0);
	return mod;
}
struct Payload { asIScriptContext * ctx = nullptr; uint32_t id = 0; };
void WriteCtx(zIZodiacWriter * w, void * p)
{ auto pl = static_cast<Payload*>(p); pl->id = w->SaveContext(pl->ctx); w->GetFile()->Write(&pl->id); }
void ReadCtx(zIZodiacReader * r, void * p)
{ auto pl = static_cast<Payload*>(p); r->GetFile()->Read(&pl->id); pl->ctx = r->LoadContext(pl->id); }
int RunToFinish(asIScriptContext * ctx)
{
	int guard = 0, state = ctx->GetState();
	while(state == asEXECUTION_SUSPENDED && guard++ < 100) state = ctx->Execute();
	EXPECT_EQ(state, asEXECUTION_FINISHED) << "resumed context did not finish";
	return (int)ctx->GetReturnDWord();
}
}

TEST(ContextRoots, ContextOnlyObjectSurvives)
{
	// `o` is reachable ONLY via the suspended stack — no global points at it.
	const char * src =
		"class Obj { int v; }\n"
		"int run() { Obj@ o = Obj(); o.v = 42; yield(); return o.v; }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		auto z = MakeZodiac(e.get());
		Payload pl; pl.ctx = ctx;
		z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	auto z = MakeZodiac(e.get());
	Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr) << "context not restored";
	EXPECT_EQ(RunToFinish(pl.ctx), 42)
		<< "on-stack-only object 'o' not discovered/restored";
	e->ReturnContext(pl.ctx);
}

TEST(ContextRoots, ContextGlobalAliasing)
{
	// local `p` aliases global `g`; after resume, mutating through the local must
	// be visible through the global iff identity was preserved.
	const char * src =
		"class Obj { int v; }\n"
		"Obj@ g;\n"
		"int run() { @g = Obj(); g.v = 5; Obj@ p = g; yield(); p.v = p.v + 1; return g.v; }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		auto z = MakeZodiac(e.get());
		Payload pl; pl.ctx = ctx;
		z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	auto z = MakeZodiac(e.get());
	Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr);
	// resume: p.v++ -> 6, returned through g. If p and g are distinct copies,
	// g.v stays 5.
	EXPECT_EQ(RunToFinish(pl.ctx), 6)
		<< "context-local and global did not alias the same restored object";
	e->ReturnContext(pl.ctx);
}

TEST(ContextRoots, OnStackArgumentSurvives)
{
	// An object is pushed as an argument for a pending call to take(), then a
	// sibling argument expression side() suspends before take() is entered. At
	// suspend, `o` lives BOTH as a local of run() (a named var) AND as a value
	// already pushed on the stack as take()'s first argument (args-on-stack). If
	// the on-stack argument is not preserved, take() receives garbage on resume.
	const char * src =
		"class Obj { int v; }\n"
		"int side() { yield(); return 100; }\n"
		"int take(Obj@ o, int s) { return o.v + s; }\n"
		"int run() { Obj@ o = Obj(); o.v = 7; return take(o, side()); }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		auto z = MakeZodiac(e.get());
		Payload pl; pl.ctx = ctx;
		z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	auto z = MakeZodiac(e.get());
	Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr) << "context not restored";
	// take(o, 100) == o.v + 100 == 7 + 100 == 107; proves the on-stack `o`
	// argument survived the round trip with the right identity/value.
	EXPECT_EQ(RunToFinish(pl.ctx), 107)
		<< "on-stack object argument not preserved across save/restore";
	e->ReturnContext(pl.ctx);
}

TEST(ContextRoots, TwoContextsShareObject)
{
	// A single object is aliased by a global and referenced by a context-local in
	// TWO independently-suspended contexts. All three references must restore to
	// the SAME object: mutating through one context's local must be visible to the
	// other context and through the global.
	const char * src =
		"class Obj { int v; }\n"
		"Obj@ g;\n"
		"int run() { Obj@ p = g; yield(); p.v = p.v + 1; return p.v; }\n";

	struct TwoPayload { asIScriptContext * c1 = nullptr; asIScriptContext * c2 = nullptr;
		uint32_t id1 = 0, id2 = 0; };
	auto WriteTwo = [](zIZodiacWriter * w, void * p) {
		auto pl = static_cast<TwoPayload*>(p);
		pl->id1 = w->SaveContext(pl->c1); w->GetFile()->Write(&pl->id1);
		pl->id2 = w->SaveContext(pl->c2); w->GetFile()->Write(&pl->id2);
	};
	auto ReadTwo = [](zIZodiacReader * r, void * p) {
		auto pl = static_cast<TwoPayload*>(p);
		r->GetFile()->Read(&pl->id1); pl->c1 = r->LoadContext(pl->id1);
		r->GetFile()->Read(&pl->id2); pl->c2 = r->LoadContext(pl->id2);
	};

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		// Seed the shared global `g` with an Obj{v=5} before either context runs
		// (run() reads g into its local on the first statement, before yield).
		int gidx = mod->GetGlobalVarIndexByName("g");
		ASSERT_GE(gidx, 0);
		asITypeInfo * ot = mod->GetTypeInfoByDecl("Obj");
		ASSERT_NE(ot, nullptr);
		asIScriptObject * obj = (asIScriptObject*)e->CreateScriptObject(ot);
		ASSERT_NE(obj, nullptr);
		for(asUINT i = 0; i < obj->GetPropertyCount(); ++i)
			if(std::string(obj->GetPropertyName(i)) == "v")
			{ int five = 5; std::memcpy(obj->GetAddressOfProperty(i), &five, 4); }
		*(asIScriptObject**)mod->GetAddressOfGlobalVar(gidx) = obj; // g owns this ref

		asIScriptContext * c1 = e->RequestContext();
		c1->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(c1->Execute(), asEXECUTION_SUSPENDED);
		asIScriptContext * c2 = e->RequestContext();
		c2->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(c2->Execute(), asEXECUTION_SUSPENDED);

		auto z = MakeZodiac(e.get());
		TwoPayload pl; pl.c1 = c1; pl.c2 = c2;
		z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteTwo);
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
		e->ReturnContext(c1); e->ReturnContext(c2);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	auto z = MakeZodiac(e.get());
	TwoPayload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadTwo);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	ASSERT_NE(pl.c1, nullptr); ASSERT_NE(pl.c2, nullptr);
	// c1 resumes first: p.v 5 -> 6. c2 shares the same object, so it sees 6 and
	// bumps to 7. If the two context locals and the global were distinct copies,
	// c2 would return 6.
	EXPECT_EQ(RunToFinish(pl.c1), 6) << "c1 local did not alias the shared object";
	EXPECT_EQ(RunToFinish(pl.c2), 7)
		<< "c2 local did not share the same restored object as c1/global";
	e->ReturnContext(pl.c1); e->ReturnContext(pl.c2);
}

TEST(ContextRoots, SaveContextWithoutBytecodeRejected)
{
	const char * src =
		"int run() { int x = 1; yield(); return x; }\n";
	TestEngine e; RegisterYield(e.get());
	asIScriptModule * mod = Build(e.get(), src);
	asIScriptContext * ctx = e->RequestContext();
	ctx->Prepare(mod->GetFunctionByDecl("int run()"));
	ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);

	auto z = MakeZodiac(e.get(), /*bytecode*/false);
	Payload pl; pl.ctx = ctx;
	z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
	zCMemoryFile f;
	Code rc = z->SaveToFile(&f);
	EXPECT_EQ(rc, zE_CantSaveContextWithoutBytecode)
		<< "saving a live context without bytecode returned " << (int)rc;
	e->ReturnContext(ctx);
}
