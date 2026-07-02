// Red-hunt: a suspended context whose live on-stack local is a value type
// (std::string) rather than a script-object handle. Exercises the value-type
// branch of the context variable restore path.
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
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}
void Yield() { asIScriptContext * c = asGetActiveContext(); if(c) c->Suspend(); }
struct Payload { asIScriptContext * ctx = nullptr; uint32_t id = 0; };
void WriteCtx(zIZodiacWriter * w, void * p)
{ auto pl = static_cast<Payload*>(p); pl->id = w->SaveContext(pl->ctx); w->GetFile()->Write(&pl->id); }
void ReadCtx(zIZodiacReader * r, void * p)
{ auto pl = static_cast<Payload*>(p); r->GetFile()->Read(&pl->id); pl->ctx = r->LoadContext(pl->id); }
}

TEST(ContextStringLocal, StringLocalSurvivesSuspend)
{
	// The local `s` is a string (value type) built before the yield; after
	// resume the function returns its length, proving the local survived.
	const char * src =
		"int run() { string s = 'hello world'; yield(); return s.length(); }\n";

	std::vector<char> image;
	{
		TestEngine e;
		ASSERT_GE(e->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL), 0);
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", src), 0);
		ASSERT_GE(mod->Build(), 0);
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

	TestEngine e;
	ASSERT_GE(e->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL), 0);
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("m", src), 0);
	ASSERT_GE(mod->Build(), 0);
	auto z = MakeZodiac(e.get());
	Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	ASSERT_NE(pl.ctx, nullptr) << "context not restored";

	int guard = 0, state = pl.ctx->GetState();
	while(state == asEXECUTION_SUSPENDED && guard++ < 100) state = pl.ctx->Execute();
	ASSERT_EQ(state, asEXECUTION_FINISHED) << "resumed context did not finish";
	EXPECT_EQ((int)pl.ctx->GetReturnDWord(), 11) << "string local 'hello world' (len 11) not restored";
	e->ReturnContext(pl.ctx);
}
