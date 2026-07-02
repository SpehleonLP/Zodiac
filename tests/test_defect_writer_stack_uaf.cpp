// Defect #6 — dangling m_stack reference in the app-object save path
//             (src/z_zodiacwriter.cpp, WriteObject "application object" else
//              branch: `(entry->onSave)(this, n.address, n.zTypeId);`).
//
// WriteObject takes `Node & n`, a reference into the std::vector m_stack. The
// application-object else branch passes `n.zTypeId` — a reference INTO that vector
// element — as the onSave callback's `int & real_type` out-param. If the callback
// enqueues child objects (SaveObject / SaveScriptObject → push_back onto m_stack),
// the vector reallocates and the callback's writeback through that reference is a
// heap-use-after-free. The sibling onSave sites correctly use a LOCAL `int
// real_type` (WriteScriptObject and the save_func branch), which is the fix.
//
// FINDING (documented, verified by instrumentation): the buggy else branch is
// currently UNREACHABLE through the public save API. Every app object is enqueued
// with asTypeId > 0 (SaveScriptObject) and is written via WriteScriptObject, whose
// onSave call uses a LOCAL out-param — or via SaveObject, which sets save_func and
// takes the save_func branch (also a local). No public path produces a node with
// save_func == null AND asTypeId <= 0 AND zTypeId > 0, which is the only shape that
// reaches the else branch. Instrumenting both onSave sites and running the whole
// suite shows the buggy site fires 0 times and the local site 61 times.
//
// This test therefore exercises the REACHABLE analog — an onSave callback that
// forces m_stack to reallocate many times WHILE it runs (a large array<Holder@>,
// whose element saves push_back onto m_stack) — and asserts the save + round-trip
// stay clean under -DZODIAC_INSTRUMENT_LIB=ON. It is GREEN today (the reachable
// path uses a local); it would turn RED (ASan heap-use-after-free) if a future
// change routed app-object onSave through the dangling-reference else branch, and
// it guards the reallocation invariant the fix depends on.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptarray/scriptarray.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// A large array of script-object handles. The array's Zodiac onSave iterates the
// elements calling writer->SaveScriptObject for each, every one of which
// push_back's a node onto m_stack — reallocating the vector repeatedly WHILE the
// array's onSave callback is on the stack. This is exactly the "callback grows
// m_stack" condition that makes a dangling m_stack out-param reference fatal.
const char * kScript =
	"class Holder { int v; }\n"
	"array<Holder@> g_bag;\n"
	"void setup() {\n"
	"    for(int i = 0; i < 600; i++) {\n"
	"        Holder@ hh = Holder();\n"
	"        hh.v = i;\n"
	"        g_bag.insertLast(hh);\n"
	"    }\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}
}

TEST(WriterStackUAF, ArrayOnSaveReallocatesStackCleanly)
{
	std::vector<char> image;
	{
		TestEngine engine;
		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_NE(mod, nullptr);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
		ASSERT_NE(setup, nullptr);
		asIScriptContext * ctx = engine->CreateContext();
		ASSERT_GE(ctx->Prepare(setup), 0);
		ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
		ctx->Release();

		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file;
		// Pre-fix, IF the app-object onSave path were reachable, this save would be
		// an ASan heap-use-after-free at the out-param writeback (m_stack realloc
		// during the array's onSave). It completes cleanly on the reachable path.
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success) << zodiac->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	// Round-trip into a fresh engine: the bytecode + the 600-element array restore
	// cleanly (the load side must also survive the graph the reallocating save
	// produced). The load running to zE_Success under lib-ASan is the invariant.
	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success) << zodiac->GetErrorString();

	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	ASSERT_GE(mod->GetGlobalVarIndexByName("g_bag"), 0);
}
