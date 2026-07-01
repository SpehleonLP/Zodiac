// Red-hunt: a global delegate whose bound object is ANOTHER global. The
// delegate's bound object and the `adder` global must be the same restored
// instance — mutating through one must be visible through the delegate.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

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
void CallVoid(asIScriptEngine * e, asIScriptModule * mod, const char * decl)
{
	asIScriptFunction * fn = mod->GetFunctionByDecl(decl);
	EXPECT_NE(fn, nullptr) << decl;
	asIScriptContext * ctx = e->CreateContext();
	ctx->Prepare(fn); EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED) << decl; ctx->Release();
}
}

TEST(DelegateGlobalAlias, DelegateBoundToAnotherGlobal)
{
	const char * src =
		"funcdef int Op();\n"
		"class Adder { int base; int get() { return base; } }\n"
		"Adder@ adder;\n"
		"Op@ cb;\n"
		"void setup() { @adder = Adder(); adder.base = 50; @cb = Op(adder.get); }\n"
		"int callCb() { return cb(); }\n"
		"void bump() { adder.base = 99; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", src), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		EXPECT_EQ(CallInt(e.get(), mod, "int callCb()"), 50);
		auto z = MakeZodiac(e.get());
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto z = MakeZodiac(e.get());
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);

	EXPECT_EQ(CallInt(e.get(), mod, "int callCb()"), 50)
		<< "restored delegate over a global object not invocable / wrong value";
	// mutate via the adder global; the delegate must observe it (same instance).
	CallVoid(e.get(), mod, "void bump()");
	EXPECT_EQ(CallInt(e.get(), mod, "int callCb()"), 99)
		<< "delegate's bound object and the 'adder' global are not the same restored instance";
}
