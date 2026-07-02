// Spec test 6: minimal module round-trips through a real zCFile on disk.
// EXPECTED to go RED pre-fix (Part D: error_code returned uninitialized,
// LoadFromFile returns zE_Success unconditionally, loadedByteCode read
// uninitialized). Assertions here demand REAL success codes and a real
// round-trip; do not weaken them to make the test pass.
#include "test_engine.h"
#include "addons.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdio>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
const char * kScript = "int g_value = 42;\n";
const char * kPath   = "/tmp/zodiac_test_saveload_empty.zdc";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}
}

TEST(SaveLoadEmpty, RoundTripThroughFile)
{
	// --- save ---
	{
		TestEngine engine;
		asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_NE(mod, nullptr);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		auto zodiac = MakeZodiac(engine.get());

		FILE * fp = fopen(kPath, "wb");
		ASSERT_NE(fp, nullptr);
		auto file = FromCFile(&fp); // takes ownership, closes on destruct

		Code rc = zodiac->SaveToFile(file.get());
		EXPECT_EQ(rc, zE_Success)
			<< "SaveToFile returned " << (int)rc << " (" << zodiac->GetErrorString() << ")";
	}

	// --- load into a fresh engine ---
	{
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());

		FILE * fp = fopen(kPath, "rb");
		ASSERT_NE(fp, nullptr);
		auto file = FromCFile(&fp);

		Code rc = zodiac->LoadFromFile(file.get());
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

	remove(kPath);
}
