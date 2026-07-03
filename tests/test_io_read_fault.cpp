// Group R: a truncated context record must be REJECTED as a returned Code, not
// read as garbage/zero and silently restored. This exercises the short-read
// checks added to z_zodiaccontext.cpp (the `sizeof(x) != file->Read(&x)` idiom):
// a valid image with a suspended context is loaded through a zCFaultyFile armed
// to truncate at the very first context read (callStackSize, line 405), and
// LoadFromFile must return zE_EndOfFile with no throw/crash.
//
// The suspended-context save side is copied verbatim from test_context.cpp
// (project convention: duplicate, don't share helpers).
#include "test_engine.h"
#include "addons.h"
#include "faulty_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
const char * kScript =
	"int accumulate() {\n"
	"    int sum = 0;\n"
	"    for(int i = 0; i < 5; i++) { sum += i; yield(); }\n"
	"    return sum;\n"
	"}\n";

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

// Save-side payload (identical shape to test_context.cpp's WriteCtx).
struct SavePayload
{
	asIScriptContext * ctx = nullptr;
	uint32_t id = 0;
};

void WriteCtx(zIZodiacWriter * writer, void * p)
{
	auto pl = static_cast<SavePayload*>(p);
	pl->id = writer->SaveContext(pl->ctx);
	writer->GetFile()->Write(&pl->id);
}

// Load-side payload also carries the faulty file so the callback can arm the
// read-fault mid-load, right before LoadContext.
struct Payload
{
	asIScriptContext * ctx = nullptr;
	uint32_t id = 0;
	zCFaultyFile * faulty = nullptr;
};

void ReadCtxTruncated(zIZodiacReader * reader, void * p)
{
	auto pl = static_cast<Payload*>(p);
	reader->GetFile()->Read(&pl->id);              // id read succeeds
	// Arm: no further bytes. The very next Read (ZodiacLoad's callStackSize at
	// z_zodiaccontext.cpp:405) short-reads -> the new check throws zE_EndOfFile.
	pl->faulty->failReadAfterBytes(pl->faulty->bytesRead());
	pl->ctx = reader->LoadContext(pl->id);         // throws zE_EndOfFile
}
}

TEST(IOReadFault, TruncatedContextRecordRejected)
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
		SavePayload pl;
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

	// --- Load side: arm the fault to truncate at the first context read. ---
	TestEngine engine;
	RegisterYield(engine.get());
	BuildModule(engine.get());
	auto zodiac = MakeZodiac(engine.get());
	Payload pl;
	zCFaultyFile file(image);      // seed read-only from the valid image
	pl.faulty = &file;
	zodiac->SetUserData(&pl);
	zodiac->SetReadSaveDataCallback(ReadCtxTruncated);

	Code rc = zE_Success;
	EXPECT_NO_THROW({ rc = zodiac->LoadFromFile(&file); })
		<< "a truncated context record must become a returned Code, not a throw/crash";
	EXPECT_EQ(rc, zE_EndOfFile)
		<< "a short read at z_zodiaccontext.cpp:405 must be reported as zE_EndOfFile";
}
