// Defect regression test for saving a PREPARED context (src/z_zodiaccontext.cpp:207).
//
// A context in asEXECUTION_PREPARED state (Prepare() + SetArg* done, not yet
// Executed) is serialized as ONLY its initial function id; the arguments already
// pushed onto the prepared frame are dropped, and the load side re-Prepare()s with
// zeroed argument slots. So an argument set before the save is lost, and Execute()
// after the round-trip runs with a zero argument instead of the value supplied.
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
const char * kScript = "int echo(int n) { return n; }\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

asIScriptModule * BuildModule(asIScriptEngine * engine)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kScript), 0);
	EXPECT_GE(mod->Build(), 0);
	return mod;
}

struct Payload { asIScriptContext * ctx = nullptr; uint32_t id = 0; };

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

TEST(PreparedContext, ArgumentsSurviveRoundTrip)
{
	const asDWORD kArg = 42;

	// --- Save a context that is PREPARED with an argument but not yet executed. ---
	std::vector<char> image;
	{
		TestEngine engine;
		asIScriptModule * mod = BuildModule(engine.get());
		asIScriptFunction * fn = mod->GetFunctionByDecl("int echo(int n)");
		ASSERT_NE(fn, nullptr);

		asIScriptContext * ctx = engine->RequestContext();
		ASSERT_GE(ctx->Prepare(fn), 0);
		ASSERT_GE(ctx->SetArgDWord(0, kArg), 0);
		ASSERT_EQ(ctx->GetState(), asEXECUTION_PREPARED);

		auto zodiac = MakeZodiac(engine.get());
		Payload pl; pl.ctx = ctx;
		zodiac->SetUserData(&pl);
		zodiac->SetWriteSaveDataCallback(WriteCtx);

		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success) << zodiac->GetErrorString();
		image = file.bytes();

		engine->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());

	// --- Restore it and execute; the argument must still be 42. ---
	TestEngine engine;
	BuildModule(engine.get());
	auto zodiac = MakeZodiac(engine.get());
	Payload pl;
	zodiac->SetUserData(&pl);
	zodiac->SetReadSaveDataCallback(ReadCtx);

	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success) << zodiac->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr) << "context not restored";

	ASSERT_EQ(pl.ctx->Execute(), asEXECUTION_FINISHED);
	EXPECT_EQ(pl.ctx->GetReturnDWord(), kArg)
		<< "argument set on the prepared context before save was lost";

	engine->ReturnContext(pl.ctx);
}
