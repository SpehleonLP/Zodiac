// Header self-consistency legs that test_malformed_input.cpp does not isolate:
// the boolSize and isBigEndian checks (Verify :115-118). A save produced on a
// machine with a different bool width or byte order must be rejected outright —
// there is no cross-ABI/endian back-compat. GREEN characterization.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"
#include "z_zodiacstate.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
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
	EXPECT_GE(mod->AddScriptSection("m", "int g = 42;\n"), 0);
	EXPECT_GE(mod->Build(), 0);
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	return file.bytes();
}

zCHeader * HeaderOf(std::vector<char> & image)
{
	return reinterpret_cast<zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
}

void ExpectRejected(std::vector<char> image, Code expected)
{
	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	Code rc = zodiac->LoadFromFile(&file);
	EXPECT_EQ(rc, expected)
		<< "expected " << (int)expected << " but got " << (int)rc
		<< " (" << zodiac->GetErrorString() << ")";
	EXPECT_EQ(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr);
}
}

// Verify :116 — a save whose bool width differs from this build's is rejected.
TEST(MalformedHeader, BoolSizeMismatch)
{
	auto image = BuildValidImage();
	ASSERT_EQ(HeaderOf(image)->boolSize, (uint8_t)sizeof(bool));
	HeaderOf(image)->boolSize = sizeof(bool) == 1 ? 4 : 1;
	ExpectRejected(std::move(image), zE_BadFileType);
}

// Verify :117 — a save whose byte order differs from this build's is rejected.
TEST(MalformedHeader, EndiannessMismatch)
{
	auto image = BuildValidImage();
	HeaderOf(image)->isBigEndian = HeaderOf(image)->isBigEndian ? 0 : 1;
	ExpectRejected(std::move(image), zE_BadFileType);
}
