// Defect regression tests for the zCZodiac facade (src/z_zodiac.cpp).
//
//   * #13 Swapped reentry codes (z_zodiac.cpp:37 / :99): SaveToFile, when blocked
//     by an operation already in progress, reports zE_AlreadyLoading; LoadFromFile
//     reports zE_AlreadySaving. Each returns the OTHER operation's code. A save
//     re-entered from a save callback should say AlreadySaving; a load re-entered
//     from a load callback should say AlreadyLoading.
//   * #12 A throw from the post-catch tail escapes the Code-returning API
//     (z_zodiac.cpp:143-144): RestoreGlobalVariables and ReadSaveData run OUTSIDE
//     the try/catch, so a file-/callback-driven throw there propagates out of
//     LoadFromFile with GetErrorCode() still reporting zE_Success. The contract is
//     that any failure is reported through GetErrorCode(), never lost as success.
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
	"class Node { int value; Node@ next; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"    Node@ a = Node();\n"
	"    a.value = 7; @head = a;\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

void BuildModule(TestEngine & engine)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_NE(mod, nullptr);
	ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
	ASSERT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	ASSERT_NE(setup, nullptr);
	asIScriptContext * ctx = engine.get()->CreateContext();
	ASSERT_GE(ctx->Prepare(setup), 0);
	ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();
}

// ---- #13 save-side reentry --------------------------------------------------
struct SaveReentryCtx { zIZodiac * z; Code inner; };

void ReentrantSaveCb(void * ud)
{
	auto * c = static_cast<SaveReentryCtx *>(ud);
	zCMemoryFile innerFile;
	c->inner = c->z->SaveToFile(&innerFile); // blocked: a save is already in progress
}

// ---- #12 escaping read-save-data throw --------------------------------------
void WriteSomeSaveData(zIZodiacWriter * writer, void *)
{
	// Emit a few bytes so the save-data region is non-empty (its ReadSubFile on
	// load is then a normal, in-range window).
	const uint32_t payload[2] = {0xABCDEF01u, 0x12345678u};
	writer->GetFile()->Write(payload, 2);
}

void ThrowingReadSaveData(zIZodiacReader *, void *)
{
	throw zE_ObjectUnserializable; // a callback-driven failure during the load tail
}
}

// #13 — a save re-entered from within a save callback must be reported as
// AlreadySaving, not AlreadyLoading.
TEST(FacadeReentry, NestedSaveReportsAlreadySaving)
{
	TestEngine engine;
	BuildModule(engine);
	auto zodiac = MakeZodiac(engine.get());

	SaveReentryCtx ctx{zodiac.get(), zE_Success};
	zodiac->SetUserData(&ctx);
	zodiac->SetPreSavingCallback(ReentrantSaveCb);

	zCMemoryFile outer;
	zodiac->SaveToFile(&outer);

	EXPECT_EQ(ctx.inner, zE_AlreadySaving)
		<< "reentrant SaveToFile should report AlreadySaving; got " << (int)ctx.inner;
}

// #13 — a load re-entered from within a restore callback must be reported as
// AlreadyLoading, not AlreadySaving.
TEST(FacadeReentry, NestedLoadReportsAlreadyLoading)
{
	// First produce a valid image.
	std::vector<char> image;
	{
		TestEngine engine;
		BuildModule(engine);
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success);
		image = file.bytes();
	}

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());

	struct LoadReentryCtx { zIZodiac * z; std::vector<char> * image; Code inner; };
	LoadReentryCtx ctx{zodiac.get(), &image, zE_Success};
	zodiac->SetUserData(&ctx);
	zodiac->SetPreRestoreCallback([](void * ud) {
		auto * c = static_cast<LoadReentryCtx *>(ud);
		zCMemoryFile innerFile(*c->image);
		c->inner = c->z->LoadFromFile(&innerFile); // blocked: a load is in progress
	});

	zCMemoryFile file(image);
	zodiac->LoadFromFile(&file);

	EXPECT_EQ(ctx.inner, zE_AlreadyLoading)
		<< "reentrant LoadFromFile should report AlreadyLoading; got " << (int)ctx.inner;
}

// #12 — a throw from the read-save-data callback (invoked after the try/catch) must
// be reported through GetErrorCode(), not lost while GetErrorCode() stays Success.
TEST(FacadeErrorReporting, TailThrowIsReportedNotLostAsSuccess)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildModule(engine);
		auto zodiac = MakeZodiac(engine.get());
		zodiac->SetWriteSaveDataCallback(WriteSomeSaveData);
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success);
		image = file.bytes();
	}

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zodiac->SetReadSaveDataCallback(ThrowingReadSaveData);

	zCMemoryFile file(image);
	// Pre-fix the throw escapes LoadFromFile entirely; tolerate that here so the
	// assertion below (on GetErrorCode) is what actually judges the defect.
	try { zodiac->LoadFromFile(&file); }
	catch(...) {}

	EXPECT_NE(zodiac->GetErrorCode(), zE_Success)
		<< "a failing load-tail callback must be recorded in GetErrorCode()";
}
