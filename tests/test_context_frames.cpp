// Red-hunt (2026-07-02): deeper corners of suspended-context serialization that
// the trunk-API adaptation left unexercised. The existing context tests cover a
// single suspended frame with primitive/string/script-handle locals and a couple
// of cross-root aliasing cases; these push on the parts z_zodiaccontext.cpp does
// NOT yet have a test for:
//   * PreparedContextRoundTrip   — a context in asEXECUTION_PREPARED (Prepare but
//                                  never Executed). The load path has a dedicated
//                                  PREPARED branch that nothing exercises.
//   * DeepCallStackResumes       — a many-frame call stack (recursion) each frame
//                                  holding a live local; only 1-2 frames tested.
//   * ThisAliasesGlobalObject    — suspend inside a class METHOD whose `this`
//                                  aliases a global handle; identity of the `this`
//                                  pointer through the CtxCallState path.
//   * ArrayLocalSurvivesSuspend  — a template/app-object local (array<int>) live
//                                  at suspend; only script-class handles + string
//                                  locals covered so far.
//   * MixedLocalsSurviveSuspend  — int + string + array locals all in scope at the
//                                  same suspend point.
//   * ContextSurvivesResave      — save a suspended context, restore it, then SAVE
//                                  THE RESTORED CONTEXT AGAIN and restore once more
//                                  before resuming (idempotence of a restored frame).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptarray/scriptarray.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
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
	// Drive to completion from either a mid-run SUSPENDED state or a fresh
	// PREPARED (Prepare()'d-but-never-Executed) context — Execute() runs a
	// prepared context from the top, so both are "runnable" here.
	while((state == asEXECUTION_SUSPENDED || state == asEXECUTION_PREPARED)
			&& guard++ < 1000)
		state = ctx->Execute();
	EXPECT_EQ(state, asEXECUTION_FINISHED) << "resumed context did not finish (state " << state << ")";
	return (int)ctx->GetReturnDWord();
}

// Serialize `ctx` (via the save-data callback) into a fresh image.
std::vector<char> SaveWithContext(asIScriptEngine * e, asIScriptContext * ctx)
{
	auto z = MakeZodiac(e);
	Payload pl; pl.ctx = ctx;
	z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
	zCMemoryFile f;
	EXPECT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
	return f.bytes();
}
// Restore a context out of `image` into engine `e` (module already rebuilt).
asIScriptContext * LoadContextFrom(asIScriptEngine * e, const std::vector<char> & image)
{
	auto z = MakeZodiac(e);
	Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
	zCMemoryFile f(image);
	EXPECT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	return pl.ctx;
}
}

TEST(ContextFrames, PreparedContextRoundTrip)
{
	// A context that was Prepare()'d but never Executed. asEXECUTION_PREPARED has
	// its own load branch (FinishDeserialization before restoring vars); nothing
	// exercises it. After restore, executing from the top must run to completion.
	const char * src = "int f() { int x = 0; for(int i = 0; i < 5; i++) x += i; return x; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ASSERT_GE(ctx->Prepare(mod->GetFunctionByDecl("int f()")), 0);
		ASSERT_EQ(ctx->GetState(), asEXECUTION_PREPARED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "prepared context not restored";
	EXPECT_EQ(ctx->GetState(), asEXECUTION_PREPARED)
		<< "restored context should still be PREPARED and runnable";
	EXPECT_EQ(RunToFinish(ctx), 10) << "prepared context did not execute to 10";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, DeepCallStackResumes)
{
	// A 6-deep recursion suspends at the bottom; every frame holds a live local
	// `here`. On resume each frame must complete with its own `here` intact.
	// descend(n): here = 10*n; sum of here over n=6..0 = 10*(6+5+4+3+2+1+0) = 210.
	const char * src =
		"int descend(int n) {\n"
		"  int here = 10 * n;\n"
		"  if(n == 0) { yield(); return here; }\n"
		"  int deeper = descend(n - 1);\n"
		"  return here + deeper;\n"
		"}\n"
		"int run() { return descend(6); }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		ASSERT_GE(ctx->GetCallstackSize(), 6u) << "expected a deep call stack at suspend";
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "deep-stack context not restored";
	EXPECT_EQ(RunToFinish(ctx), 210)
		<< "per-frame locals across a deep call stack did not all survive";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, ThisAliasesGlobalObject)
{
	// Suspend inside a class method. The method's `this` object is ALSO held by a
	// global handle `g`. On restore, `this` and `g` must be the SAME object:
	// mutating `v` through `this` on resume must be visible through `g`.
	const char * src =
		"class Obj { int v; int run() { yield(); v = v + 1; return v; } }\n"
		"Obj@ g;\n"
		"void seed() { @g = Obj(); g.v = 5; }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);

		// Run seed() to populate the global with an Obj{v=5}.
		asIScriptContext * s = e->RequestContext();
		s->Prepare(mod->GetFunctionByDecl("void seed()"));
		ASSERT_EQ(s->Execute(), asEXECUTION_FINISHED);
		e->ReturnContext(s);

		int gidx = mod->GetGlobalVarIndexByName("g");
		ASSERT_GE(gidx, 0);
		asIScriptObject * obj = *reinterpret_cast<asIScriptObject**>(mod->GetAddressOfGlobalVar(gidx));
		ASSERT_NE(obj, nullptr);

		asITypeInfo * ot = mod->GetTypeInfoByName("Obj");
		ASSERT_NE(ot, nullptr);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(ot->GetMethodByDecl("int run()"));
		ctx->SetObject(obj);
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); asIScriptModule * mod = Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "method context not restored";
	// Resume: v 5 -> 6, returned. Then read g.v: must also be 6 (same object).
	EXPECT_EQ(RunToFinish(ctx), 6) << "method `this` local state not restored";
	int gidx = mod->GetGlobalVarIndexByName("g");
	ASSERT_GE(gidx, 0);
	asIScriptObject * g = *reinterpret_cast<asIScriptObject**>(mod->GetAddressOfGlobalVar(gidx));
	ASSERT_NE(g, nullptr);
	EXPECT_EQ(*static_cast<int*>(g->GetAddressOfProperty(0)), 6)
		<< "method `this` and global `g` did not alias the same restored object";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, ArrayLocalSurvivesSuspend)
{
	// A template/app-object local (array<int>) live across a suspend. The reader's
	// zLoadVariable app-object/template branch for a stack var is otherwise untested.
	const char * src =
		"int run() {\n"
		"  array<int> a = {1, 2, 3, 4};\n"
		"  yield();\n"
		"  int s = 0;\n"
		"  for(uint i = 0; i < a.length(); i++) s += a[i];\n"
		"  return s;\n"
		"}\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "array-local context not restored";
	EXPECT_EQ(RunToFinish(ctx), 10) << "array<int> stack local did not survive suspend";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, MixedLocalsSurviveSuspend)
{
	// int + string + array locals all live at the same suspend point.
	const char * src =
		"int run() {\n"
		"  int a = 7;\n"
		"  string s = 'hello';\n"
		"  array<int> arr = {1, 2};\n"
		"  yield();\n"
		"  return a + int(s.length()) + int(arr.length());\n"  // 7 + 5 + 2 = 14
		"}\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "mixed-locals context not restored";
	EXPECT_EQ(RunToFinish(ctx), 14) << "not all mixed-type locals survived suspend";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, DictionaryLocalSurvivesSuspend)
{
	// A dictionary (ref-type app object) local live across a suspend.
	const char * src =
		"int run() {\n"
		"  dictionary d;\n"
		"  d.set('k', 7);\n"
		"  yield();\n"
		"  int v = 0;\n"
		"  d.get('k', v);\n"
		"  return v;\n"
		"}\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "dictionary-local context not restored";
	EXPECT_EQ(RunToFinish(ctx), 7) << "dictionary stack local did not survive suspend";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, AnyLocalSurvivesSuspend)
{
	// An `any` (app ref type) local live across a suspend.
	const char * src =
		"int run() {\n"
		"  any box;\n"
		"  box.store(int64(99));\n"
		"  yield();\n"
		"  int64 v = 0;\n"
		"  box.retrieve(v);\n"
		"  return int(v);\n"
		"}\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "any-local context not restored";
	EXPECT_EQ(RunToFinish(ctx), 99) << "any stack local did not survive suspend";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, GridLocalSurvivesSuspend)
{
	// A grid<int> (app ref type) local live across a suspend.
	const char * src =
		"int run() {\n"
		"  grid<int> gr(2, 1);\n"
		"  gr[0, 0] = 4; gr[1, 0] = 8;\n"
		"  yield();\n"
		"  return gr[0, 0] + gr[1, 0];\n"
		"}\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image);
	ASSERT_NE(ctx, nullptr) << "grid-local context not restored";
	EXPECT_EQ(RunToFinish(ctx), 12) << "grid stack local did not survive suspend";
	e->ReturnContext(ctx);
}

TEST(ContextFrames, ContextSurvivesResave)
{
	// Idempotence of a restored frame: save a suspended context, restore it into a
	// fresh engine, immediately RE-SAVE the restored context, restore once more,
	// and only then resume. A restored frame must itself be serializable.
	const char * src =
		"int accumulate() {\n"
		"  int sum = 0;\n"
		"  for(int i = 0; i < 5; i++) { sum += i; yield(); }\n"
		"  return sum;\n"
		"}\n";

	std::vector<char> image1;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int accumulate()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED);
		image1 = SaveWithContext(e.get(), ctx);
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image1.empty());

	std::vector<char> image2;
	{
		TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
		asIScriptContext * ctx = LoadContextFrom(e.get(), image1);
		ASSERT_NE(ctx, nullptr) << "first restore failed";
		image2 = SaveWithContext(e.get(), ctx);  // re-serialize the RESTORED context
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image2.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	asIScriptContext * ctx = LoadContextFrom(e.get(), image2);
	ASSERT_NE(ctx, nullptr) << "second restore failed";
	EXPECT_EQ(RunToFinish(ctx), 10) << "twice-round-tripped context did not resume correctly";
	e->ReturnContext(ctx);
}
