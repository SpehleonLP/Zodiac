// Group W + Group F write-fault hardening (Task 3).
//
// The single write sink (zCFile::Write) and the new Flush() hook now THROW on
// failure, so every unchecked write call site is covered centrally. These
// tests drive the fault-injecting zCFaultyFile double (which throws the same
// Codes as production zCFile) and assert that a short write / flush failure
// becomes a RETURNED Zodiac::Code at the SaveToFile boundary, NOT an escaping
// throw and NOT a spurious zE_Success.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "faulty_file.h"
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

// Builds a real engine with a trivial module so there is data to save.
struct WriteFaultFixture : public ::testing::Test
{
	TestEngine engine;

	void SetUp() override
	{
		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", "int g = 1;\n"), 0);
		ASSERT_GE(mod->Build(), 0);
	}
};
}

// W1: a short write mid-save must abort and surface as a returned Code.
TEST_F(WriteFaultFixture, ShortWriteAbortsSaveWithCode)
{
	// First determine a valid save's total size through a plain memory file.
	uint32_t total = 0;
	{
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success);
		total = (uint32_t)file.bytes().size();
	}
	ASSERT_GT(total, 2u);

	// Now fail the write partway through; a threshold guaranteed to be crossed.
	auto zodiac = MakeZodiac(engine.get());
	zCFaultyFile faulty;
	faulty.failWriteAfterBytes(total / 2);

	Code rc = zE_Success;
	EXPECT_NO_THROW({ rc = zodiac->SaveToFile(&faulty); })
		<< "a short write must be reported as a Code, not thrown out of the API";
	EXPECT_EQ(rc, zE_IOError)
		<< "a short/failed write must return zE_IOError, never zE_Success";
}

// F1: a flush failure at Finish() must abort and surface as a returned Code.
TEST_F(WriteFaultFixture, FlushFailureAbortsSaveWithCode)
{
	auto zodiac = MakeZodiac(engine.get());
	zCFaultyFile faulty;
	faulty.failOnFlush();

	Code rc = zE_Success;
	EXPECT_NO_THROW({ rc = zodiac->SaveToFile(&faulty); })
		<< "a flush failure must be reported as a Code, not thrown out of the API";
	EXPECT_EQ(rc, zE_IOError)
		<< "a flush failure must return zE_IOError, never zE_Success";
}
