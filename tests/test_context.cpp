// Spec test 8 (suspended-context portion): the payoff of the context-adaptation
// slice. A script coroutine is executed until it SUSPENDS mid-loop, its live
// context (call stack + on-stack locals) is serialized, restored into a FRESH
// engine that recompiled the identical source, and resumed to completion. The
// returned value proves the on-stack locals (sum, i) survived the round-trip —
// i.e. that z_zodiaccontext.cpp's trunk adaptation (2-arg PushFunction +
// GetAddressOfVar in place of SetVarContents) actually reconstitutes the frame.
//
// Contexts are reached through the save-data callback (writer->SaveContext /
// reader->LoadContext), which is the disk-free, CContextMgr-free way in — the
// context manager add-on doesn't build standalone, but the raw context API does.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// accumulate() sums 0..4, yielding (suspending) after each add. First Execute()
// returns suspended with sum==0,i==0 done; a full run returns 0+1+2+3+4 = 10.
const char * kScript =
	"int accumulate() {\n"
	"    int sum = 0;\n"
	"    for(int i = 0; i < 5; i++) { sum += i; yield(); }\n"
	"    return sum;\n"
	"}\n";

// Registered global: suspend the running context (VM returns from Execute()).
void Yield()
{
	asIScriptContext * ctx = asGetActiveContext();
	if(ctx) ctx->Suspend();
}

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

void RegisterYield(asIScriptEngine * engine)
{
	int r = engine->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL);
	ASSERT_GE(r, 0);
}

asIScriptModule * BuildModule(asIScriptEngine * engine)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kScript), 0);
	EXPECT_GE(mod->Build(), 0);
	return mod;
}

// The context to save (write side) / the restored context (read side), plus the
// address-table id SaveContext hands back, threaded through the save blob.
struct Payload
{
	asIScriptContext * ctx = nullptr;
	uint32_t id = 0;
};

void WriteCtx(zIZodiacWriter * writer, void * p)
{
	auto pl = static_cast<Payload*>(p);
	pl->id = writer->SaveContext(pl->ctx);
	writer->GetFile()->Write(&pl->id);
}

void ReadCtx(zIZodiacReader * reader, void * p)
{
	auto pl = static_cast<Payload*>(p);
	reader->GetFile()->Read(&pl->id);
	pl->ctx = reader->LoadContext(pl->id);
}
}

TEST(SuspendedContext, LocalsSurviveRoundTripAndResume)
{
	// --- Save side: run until suspended, then serialize the live context. ---
	std::vector<char> image;
	{
		TestEngine engine;
		RegisterYield(engine.get());
		asIScriptModule * mod = BuildModule(engine.get());

		asIScriptFunction * fn = mod->GetFunctionByDecl("int accumulate()");
		ASSERT_NE(fn, nullptr);
		asIScriptContext * ctx = engine->RequestContext();
		ASSERT_GE(ctx->Prepare(fn), 0);
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED)
			<< "context should suspend at the first yield()";

		auto zodiac = MakeZodiac(engine.get());
		Payload pl;
		pl.ctx = ctx;
		zodiac->SetUserData(&pl);
		zodiac->SetWriteSaveDataCallback(WriteCtx);

		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success)
			<< "SaveToFile: " << zodiac->GetErrorString();
		image = file.bytes();

		engine->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	// --- Load side: fresh engine, identical recompiled source, restore + resume. ---
	TestEngine engine;
	RegisterYield(engine.get());
	BuildModule(engine.get());

	auto zodiac = MakeZodiac(engine.get());
	Payload pl;
	zodiac->SetUserData(&pl);
	zodiac->SetReadSaveDataCallback(ReadCtx);

	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "LoadFromFile: " << zodiac->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr) << "context not restored";

	// Resume to completion — each remaining yield() re-suspends.
	int guard = 0;
	int state = pl.ctx->GetState();
	while(state == asEXECUTION_SUSPENDED && guard++ < 100)
		state = pl.ctx->Execute();

	ASSERT_EQ(state, asEXECUTION_FINISHED)
		<< "resumed context did not finish (state " << state << ")";
	EXPECT_EQ(pl.ctx->GetReturnDWord(), 10u)
		<< "on-stack locals (sum/i) did not survive the round-trip";

	engine->ReturnContext(pl.ctx);
}
