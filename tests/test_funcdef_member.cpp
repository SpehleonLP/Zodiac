// Funcdef-member round-trip: the completion of funcdef serialization.
//
// A script CLASS holds a funcdef-typed member (`BinOp@ op`). Two flavors:
//   * a PLAIN function handle  (@add)                — no owned object;
//   * a DELEGATE               (BinOp(adder.method)) — a heap asIScriptFunction
//     wrapping (object, method), which the reader's function table owns and must
//     Release in ~zCZodiacReader (the #11 dtor fix). Running under LSan, a missed
//     Release on the restored delegate shows as a leak.
//
// Correctness is proven by resuming script logic that CALLS the restored member
// and checking the returned value — i.e. the member still points at a live,
// invocable function after the round-trip.
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
// add() is a free function; Adder::add is a method (used to build a delegate).
// Calc holds a funcdef handle; invoke() calls it so we can observe the result.
const char * kScript =
	"funcdef int BinOp(int, int);\n"
	"int add(int a, int b) { return a + b; }\n"
	"class Adder { int bias; int add(int a, int b) { return a + b + bias; } }\n"
	"class Calc { BinOp@ op; }\n"
	"Adder@ adder;\n"
	"Calc@ plainCalc;\n"
	"Calc@ delegateCalc;\n"
	"void setup() {\n"
	"    @plainCalc = Calc();\n"
	"    @plainCalc.op = @add;\n"          // plain function handle
	"    @adder = Adder();\n"
	"    adder.bias = 100;\n"
	"    @delegateCalc = Calc();\n"
	"    @delegateCalc.op = BinOp(adder.add);\n" // delegate over adder.add
	"}\n"
	"int callPlain()    { return plainCalc.op(3, 4); }\n"       // 7
	"int callDelegate() { return delegateCalc.op(3, 4); }\n";   // 3+4+100 = 107

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

int CallInt(asIScriptEngine * engine, asIScriptModule * mod, const char * decl)
{
	asIScriptFunction * fn = mod->GetFunctionByDecl(decl);
	EXPECT_NE(fn, nullptr) << decl;
	if(!fn) return -0x0BADBEEF;
	asIScriptContext * ctx = engine->RequestContext();
	EXPECT_GE(ctx->Prepare(fn), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED) << decl;
	int v = (int)ctx->GetReturnDWord();
	engine->ReturnContext(ctx);
	return v;
}

std::vector<char> SaveImage()
{
	TestEngine engine;
	asIScriptModule * mod = BuildModule(engine.get());

	int r = CallInt(engine.get(), mod, "void setup()"); // runs setup(); return unused
	(void)r;

	// pre-save sanity: both members invoke correctly before serialization
	EXPECT_EQ(CallInt(engine.get(), mod, "int callPlain()"), 7);
	EXPECT_EQ(CallInt(engine.get(), mod, "int callDelegate()"), 107);

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success)
		<< "SaveToFile: " << zodiac->GetErrorString();
	return file.bytes();
}
}

TEST(FuncdefMember, PlainAndDelegateRoundTrip)
{
	std::vector<char> image = SaveImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());

	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "LoadFromFile: " << zodiac->GetErrorString();

	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr) << "restored module 'm' missing";

	EXPECT_EQ(CallInt(engine.get(), mod, "int callPlain()"), 7)
		<< "restored plain-function funcdef member not invocable";
	EXPECT_EQ(CallInt(engine.get(), mod, "int callDelegate()"), 107)
		<< "restored delegate funcdef member not invocable";
}
