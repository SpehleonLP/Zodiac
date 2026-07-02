// Funcdef GLOBALS round-trip — the companion to test_funcdef_member. A module
// scope funcdef handle global is stored as a FUNCTION-table index in
// zCGlobalInfo.address (SaveScriptObject routes funcdefs to SaveFunction), which
// Verify() must bound against the function table, not the object-address table.
// Covers a plain-function handle, a delegate, and a null global.
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
const char * kScript =
	"funcdef int BinOp(int, int);\n"
	"int add(int a, int b) { return a + b; }\n"
	"class Adder { int bias; int add(int a, int b) { return a + b + bias; } }\n"
	"Adder@ adder;\n"
	"BinOp@ gPlain;\n"
	"BinOp@ gDelegate;\n"
	"BinOp@ gNull;\n"
	"void setup() {\n"
	"    @gPlain = @add;\n"
	"    @adder = Adder();\n"
	"    adder.bias = 100;\n"
	"    @gDelegate = BinOp(adder.add);\n"
	"    @gNull = null;\n"
	"}\n"
	"int callPlain()    { return gPlain(3, 4); }\n"     // 7
	"int callDelegate() { return gDelegate(3, 4); }\n"  // 107
	"bool nullIsNull()  { return gNull is null; }\n";   // true

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
	CallInt(engine.get(), mod, "void setup()");

	EXPECT_EQ(CallInt(engine.get(), mod, "int callPlain()"), 7);
	EXPECT_EQ(CallInt(engine.get(), mod, "int callDelegate()"), 107);

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success)
		<< "SaveToFile: " << zodiac->GetErrorString();
	return file.bytes();
}
}

TEST(FuncdefGlobal, PlainDelegateAndNullRoundTrip)
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
		<< "restored plain-function funcdef global not invocable";
	EXPECT_EQ(CallInt(engine.get(), mod, "int callDelegate()"), 107)
		<< "restored delegate funcdef global not invocable";
	EXPECT_EQ(CallInt(engine.get(), mod, "bool nullIsNull()"), 1)
		<< "restored null funcdef global should be null";
}
