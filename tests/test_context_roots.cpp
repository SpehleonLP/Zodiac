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
#include <memory>
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
