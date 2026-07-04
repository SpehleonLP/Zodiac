// Round-trips through the library's mmap-backed Zodiac::zCMemoryFile — the
// disk-free descriptor the per-package hot-reload driver uses (FromMemory).
//
// Distinct from test_memoryfile_roundtrip.cpp, which exercises the vector-backed
// test-support descriptor (zodiac_test::zCMemoryFile). Both must round-trip; this
// one additionally proves the driver's exact save->rewind->load-same-descriptor
// usage and the reservation-overrun guard.
#include "test_engine.h"
#include "addons.h"
#include "z_memoryfile.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
const char * kScript = "int g_value = 42;\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}
}

// save -> bytes() -> load into a fresh engine from the extracted image.
TEST(MemoryFileMmap, RoundTripThroughImage)
{
	std::vector<char> image;

	{
		TestEngine engine;
		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_NE(mod, nullptr);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		auto zodiac = MakeZodiac(engine.get());
		Zodiac::zCMemoryFile file;
		Code rc = zodiac->SaveToFile(&file);
		EXPECT_EQ(rc, zE_Success)
			<< "SaveToFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";
		image = file.bytes();
		EXPECT_FALSE(image.empty());
	}

	{
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		Zodiac::zCMemoryFile file(image);
		Code rc = zodiac->LoadFromFile(&file);
		EXPECT_EQ(rc, zE_Success)
			<< "LoadFromFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";

		asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
		ASSERT_NE(mod, nullptr);
		int idx = mod->GetGlobalVarIndexByName("g_value");
		ASSERT_GE(idx, 0);
		EXPECT_EQ(*(int*)mod->GetAddressOfGlobalVar(idx), 42);
	}
}

// save -> rewind -> load on the SAME descriptor: the hot-reload driver's exact
// pattern (one FromMemory() buffer, written by the save pass and re-read by the
// restore pass, with a rewind() in between).
TEST(MemoryFileMmap, RoundTripSameDescriptorRewind)
{
	Zodiac::zCMemoryFile file;

	{
		TestEngine engine;
		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_NE(mod, nullptr);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		auto zodiac = MakeZodiac(engine.get());
		EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	}

	file.rewind();

	{
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success);

		asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
		ASSERT_NE(mod, nullptr);
		int idx = mod->GetGlobalVarIndexByName("g_value");
		ASSERT_GE(idx, 0);
		EXPECT_EQ(*(int*)mod->GetAddressOfGlobalVar(idx), 42);
	}
}

// Writing past the reservation is a clean zE_BufferOverrun (surfaced as a Code
// at the SaveToFile boundary), never a wild write / crash. Uses a tiny reserve
// so a modest module save overruns it.
TEST(MemoryFileMmap, OverrunReservationFailsCleanly)
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_NE(mod, nullptr);
	ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
	ASSERT_GE(mod->Build(), 0);

	auto zodiac = MakeZodiac(engine.get());
	Zodiac::zCMemoryFile file(std::size_t(16));   // 16-byte reservation
	Code rc = zodiac->SaveToFile(&file);
	EXPECT_NE(rc, zE_Success) << "a 16-byte reservation must not hold a full save";
}
