// Red-hunt: a delegate that forms a cycle with the object it is bound to. `c`
// holds a funcdef handle `cb` that is a delegate over `c.tick` — so the bound
// object of the delegate is `c` itself, and `c` owns the delegate. After a
// round-trip, invoking cb() must tick the SAME restored `c` (identity + the
// object<->delegate cycle preserved).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

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
int CallInt(asIScriptEngine * e, asIScriptModule * mod, const char * decl)
{
	asIScriptFunction * fn = mod->GetFunctionByDecl(decl);
	EXPECT_NE(fn, nullptr) << decl;
	asIScriptContext * ctx = e->CreateContext();
	ctx->Prepare(fn);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED) << decl;
	int v = (int)ctx->GetReturnDWord();
	ctx->Release();
	return v;
}
}

TEST(DelegateCycle, SelfBoundDelegateRoundTrip)
{
	const char * script =
		"funcdef void Fn();\n"
		"class C { int hits; Fn@ cb; void tick() { hits++; } }\n"
		"C@ c;\n"
		"void setup() { @c = C(); c.hits = 0; @c.cb = Fn(c.tick); }\n"
		"void fire() { c.cb(); }\n"
		"int hits() { return c.hits; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", script), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		// sanity: delegate works pre-save
		EXPECT_EQ(CallInt(e.get(), mod, "int hits()"), 0);

		auto zodiac = MakeZodiac(e.get());
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success) << zodiac->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto zodiac = MakeZodiac(e.get());
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success) << zodiac->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);

	// The restored self-bound delegate must be invocable and tick the same c.
	(void)CallInt(e.get(), mod, "void fire()");
	EXPECT_EQ(CallInt(e.get(), mod, "int hits()"), 1)
		<< "restored self-bound delegate did not tick its own object";
}
