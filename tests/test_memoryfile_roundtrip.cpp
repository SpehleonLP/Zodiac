// Spec test 7 scaffold: the same minimal round-trip as test 6 but disk-free,
// through zCMemoryFile. Same expectation re: possibly-red pre-fix (Part D).
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
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

TEST(MemoryFileRoundTrip, RoundTripThroughMemory)
{
	std::vector<char> image;

	// --- save into memory ---
	{
		TestEngine engine;
		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_NE(mod, nullptr);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		auto zodiac = MakeZodiac(engine.get());

		zCMemoryFile file;
		Code rc = zodiac->SaveToFile(&file);
		EXPECT_EQ(rc, zE_Success)
			<< "SaveToFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";

		image = file.bytes();
		EXPECT_FALSE(image.empty());
	}

	// --- load into a fresh engine from the in-memory image ---
	{
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());

		zCMemoryFile file(image);
		Code rc = zodiac->LoadFromFile(&file);
		EXPECT_EQ(rc, zE_Success)
			<< "LoadFromFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";

		asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
		if(mod != nullptr)
		{
			int idx = mod->GetGlobalVarIndexByName("g_value");
			if(idx >= 0)
			{
				void * addr = mod->GetAddressOfGlobalVar(idx);
				ASSERT_NE(addr, nullptr);
				EXPECT_EQ(*(int*)addr, 42);
			}
		}
	}
}
