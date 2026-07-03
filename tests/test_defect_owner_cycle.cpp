// Defect regression test for owner-cycle recursion in the load path
// (src/z_zodiacreader.cpp:922).
//
// LoadScriptObjectImpl restores an entry's `owner` BEFORE the current object is
// registered in m_loadedObjects (it is not created until ~line 1021) or marked
// beingLoaded. Verify() only bounds owner < addressTableLength(); it never checks
// for a self-reference or cycle. So an entry whose owner is its own index makes
// LoadScriptObject re-enter LoadScriptObjectImpl on the same index forever —
// unbounded C-stack recursion → stack-overflow crash on malformed input, instead
// of the trust-boundary contract of a thrown Zodiac::Code.
//
// Run as a death test: pre-fix the child is killed by SIGSEGV (red); post-fix it
// rejects the image with a Code, is caught, and exits cleanly (green).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"
#include "z_zodiacstate.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
const char * kScript =
	"class Node { int value; Node@ next; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"    Node@ a = Node(); Node@ b = Node();\n"
	"    a.value = 111; b.value = 222;\n"
	"    @a.next = b; @head = a;\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

std::vector<char> BuildValidImage()
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kScript), 0);
	EXPECT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	EXPECT_NE(setup, nullptr);
	asIScriptContext * ctx = engine.get()->CreateContext();
	EXPECT_GE(ctx->Prepare(setup), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	return file.bytes();
}
}

TEST(OwnerCycle, SelfOwningEntryRejectedWithoutRecursion)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	auto * h = reinterpret_cast<zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
	ASSERT_GT(h->addressTableLength, 1u);
	auto * entries = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	// Make every real object own itself; whichever object the restore visits first
	// hits the self-owner branch. This stays in range, so Verify() admits it.
	for(uint32_t i = 1; i < h->addressTableLength; ++i)
		entries[i].owner = i;

	EXPECT_EXIT({
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		try { zodiac->LoadFromFile(&file); } catch(...) {}
		_exit(0);
	}, ::testing::ExitedWithCode(0), ".*")
		<< "a self-owning entry must be rejected without unbounded recursion";
}

// Companion to the self-owner case: a *mutual* owner cycle (entry A owns B, B owns
// A). The self-owner guard at z_zodiacreader.cpp:938 only rejects ownr == address,
// and beingLoaded is not set until :771 — AFTER the owner-restore block — so the
// two-cycle slips past both Verify() (both indices are in range) and the runtime
// guard, and LoadScriptObjectImpl ping-pongs A->B->A forever until the C stack
// overflows. The trust-boundary contract is a thrown Zodiac::Code, not a crash.
TEST(OwnerCycle, MutualOwnerCycleRejectedWithoutRecursion)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	auto * h = reinterpret_cast<zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
	// Need two real object entries (entry 0 is the null sentinel) to cross-link.
	ASSERT_GE(h->addressTableLength, 3u);
	auto * entries = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	// entry 1 owns entry 2, entry 2 owns entry 1 — neither owns itself, so the
	// self-owner branch never fires; every index stays < addressTableLength so
	// Verify() admits the image. Zero the rest to isolate the cycle.
	for(uint32_t i = 1; i < h->addressTableLength; ++i)
		entries[i].owner = 0;
	entries[1].owner = 2;
	entries[2].owner = 1;

	EXPECT_EXIT({
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		try { zodiac->LoadFromFile(&file); } catch(...) {}
		_exit(0);
	}, ::testing::ExitedWithCode(0), ".*")
		<< "a mutual owner cycle must be rejected without unbounded recursion";
}
