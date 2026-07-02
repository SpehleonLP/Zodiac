// Red-hunt: contexts beyond the single-frame single-context case. Multi-context
// serialization is the stated reason the context machinery exists.
//   * TwoContexts       — two independently-suspended contexts in one save;
//                         both must restore and resume to their own results.
//   * NestedCallStack   — suspend inside a CALLED function so the saved context
//                         has >1 frame; the outer frame's local must survive.
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
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}
void Yield() { asIScriptContext * c = asGetActiveContext(); if(c) c->Suspend(); }
void RegisterYield(asIScriptEngine * e)
{
	ASSERT_GE(e->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL), 0);
}
asIScriptModule * Build(asIScriptEngine * e, const char * src)
{
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m", src), 0);
	EXPECT_GE(mod->Build(), 0);
	return mod;
}

// --- Two independent contexts through the save-data callback. ---
struct TwoPayload { asIScriptContext * c1 = nullptr; asIScriptContext * c2 = nullptr; uint32_t id1=0, id2=0; };
void WriteTwo(zIZodiacWriter * w, void * p)
{
	auto pl = static_cast<TwoPayload*>(p);
	pl->id1 = w->SaveContext(pl->c1);
	pl->id2 = w->SaveContext(pl->c2);
	w->GetFile()->Write(&pl->id1);
	w->GetFile()->Write(&pl->id2);
}
void ReadTwo(zIZodiacReader * r, void * p)
{
	auto pl = static_cast<TwoPayload*>(p);
	r->GetFile()->Read(&pl->id1);
	r->GetFile()->Read(&pl->id2);
	pl->c1 = r->LoadContext(pl->id1);
	pl->c2 = r->LoadContext(pl->id2);
}
int RunToFinish(asIScriptContext * ctx)
{
	int guard = 0, state = ctx->GetState();
	while(state == asEXECUTION_SUSPENDED && guard++ < 100) state = ctx->Execute();
	EXPECT_EQ(state, asEXECUTION_FINISHED);
	return (int)ctx->GetReturnDWord();
}
}

TEST(MultiContext, TwoContexts)
{
	const char * src =
		"int accumulate(int n) {\n"
		"  int sum = 0;\n"
		"  for(int i = 0; i < n; i++) { sum += i; yield(); }\n"
		"  return sum; }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptFunction * fn = mod->GetFunctionByDecl("int accumulate(int)");

		asIScriptContext * c1 = e->RequestContext();
		c1->Prepare(fn); c1->SetArgDWord(0, 5); ASSERT_EQ(c1->Execute(), asEXECUTION_SUSPENDED);
		asIScriptContext * c2 = e->RequestContext();
		c2->Prepare(fn); c2->SetArgDWord(0, 4);
		ASSERT_EQ(c2->Execute(), asEXECUTION_SUSPENDED);
		ASSERT_EQ(c2->Execute(), asEXECUTION_SUSPENDED); // advance c2 one extra step

		auto zodiac = MakeZodiac(e.get());
		TwoPayload pl; pl.c1 = c1; pl.c2 = c2;
		zodiac->SetUserData(&pl);
		zodiac->SetWriteSaveDataCallback(WriteTwo);
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success) << zodiac->GetErrorString();
		image = file.bytes();
		e->ReturnContext(c1); e->ReturnContext(c2);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	auto zodiac = MakeZodiac(e.get());
	TwoPayload pl;
	zodiac->SetUserData(&pl);
	zodiac->SetReadSaveDataCallback(ReadTwo);
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success) << zodiac->GetErrorString();
	ASSERT_NE(pl.c1, nullptr) << "context 1 not restored";
	ASSERT_NE(pl.c2, nullptr) << "context 2 not restored";

	EXPECT_EQ(RunToFinish(pl.c1), 10) << "ctx1 (accumulate 5) wrong result";
	EXPECT_EQ(RunToFinish(pl.c2), 6)  << "ctx2 (accumulate 4) wrong result";
	e->ReturnContext(pl.c1); e->ReturnContext(pl.c2);
}

// --- Single context, multi-frame call stack. ---
namespace {
struct OnePayload { asIScriptContext * ctx = nullptr; uint32_t id = 0; };
void WriteOne(zIZodiacWriter * w, void * p)
{ auto pl = static_cast<OnePayload*>(p); pl->id = w->SaveContext(pl->ctx); w->GetFile()->Write(&pl->id); }
void ReadOne(zIZodiacReader * r, void * p)
{ auto pl = static_cast<OnePayload*>(p); r->GetFile()->Read(&pl->id); pl->ctx = r->LoadContext(pl->id); }
}

TEST(MultiContext, NestedCallStack)
{
	const char * src =
		"void inner() { yield(); }\n"
		"int outer() { int x = 7; inner(); return x; }\n";

	std::vector<char> image;
	{
		TestEngine e; RegisterYield(e.get());
		asIScriptModule * mod = Build(e.get(), src);
		asIScriptFunction * fn = mod->GetFunctionByDecl("int outer()");
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(fn);
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED)
			<< "should suspend inside inner() with outer() still on the stack";

		auto zodiac = MakeZodiac(e.get());
		OnePayload pl; pl.ctx = ctx;
		zodiac->SetUserData(&pl);
		zodiac->SetWriteSaveDataCallback(WriteOne);
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success) << zodiac->GetErrorString();
		image = file.bytes();
		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	TestEngine e; RegisterYield(e.get()); Build(e.get(), src);
	auto zodiac = MakeZodiac(e.get());
	OnePayload pl;
	zodiac->SetUserData(&pl);
	zodiac->SetReadSaveDataCallback(ReadOne);
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success) << zodiac->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr) << "nested-frame context not restored";
	EXPECT_EQ(RunToFinish(pl.ctx), 7) << "outer() frame local x lost across multi-frame restore";
	e->ReturnContext(pl.ctx);
}
