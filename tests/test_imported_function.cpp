// Red-hunt: cross-module imported/bound functions. Module "app" imports a
// function from module "lib" and binds it. After a round-trip the binding must
// survive (or the load must fail cleanly, not crash).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>

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
// Build lib + app, bind the import. Returns the app module.
asIScriptModule * BuildBound(asIScriptEngine * e)
{
	asIScriptModule * lib = e->GetModule("lib", asGM_ALWAYS_CREATE);
	EXPECT_GE(lib->AddScriptSection("lib", "int helper() { return 7; }\n"), 0);
	EXPECT_GE(lib->Build(), 0);

	asIScriptModule * app = e->GetModule("app", asGM_ALWAYS_CREATE);
	EXPECT_GE(app->AddScriptSection("app",
		"import int helper() from 'lib';\n"
		"int useHelper() { return helper() + 1; }\n"), 0);
	EXPECT_GE(app->Build(), 0);

	// bind the import to lib's helper
	asIScriptFunction * helper = lib->GetFunctionByDecl("int helper()");
	EXPECT_NE(helper, nullptr);
	EXPECT_GE(app->BindImportedFunction(0, helper), 0);
	return app;
}
int Call(asIScriptEngine * e, asIScriptModule * mod, const char * decl)
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

TEST(ImportedFunction, BoundImportSurvivesRoundTrip)
{
	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * app = BuildBound(e.get());
		EXPECT_EQ(Call(e.get(), app, "int useHelper()"), 8); // 7 + 1
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
	asIScriptModule * app = e->GetModule("app", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(app, nullptr) << "app module missing after restore";
	EXPECT_EQ(Call(e.get(), app, "int useHelper()"), 8)
		<< "imported-function binding did not survive the round-trip";
}
