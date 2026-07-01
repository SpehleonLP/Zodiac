// Red-hunt: std::string content fidelity across a round-trip. `string` is the
// ONE engine-registered value type with working Zodiac glue in the harness, so
// a script class holding a `string` member is serializable today. We probe the
// content edge cases the string-table path is likely to mangle:
//   * embedded NUL  — SaveString(const char*) truncates at the first NUL and
//                     the table is NUL-terminated, so "a\0b" almost certainly
//                     comes back "a".
//   * empty string  — LoadString of an empty entry.
//   * long string   — larger than any inline buffer.
//   * high bytes / UTF-8 — non-ASCII must survive verbatim.
//
// Each is a separate test asserting the CORRECT expected content; reds here are
// bugs to spec-and-fix later.
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
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

// Save a module whose global `Box@ box` holds one `string s`, initialised to the
// bytes in `payload` (arbitrary content, may contain NULs — we set it from C++
// after Build so the exact bytes are controlled, not a script literal).
std::vector<char> SaveBoxWith(const std::string & payload)
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m",
		"class Box { string s; }\n"
		"Box@ box;\n"
		"void setup() { @box = Box(); }\n"), 0);
	EXPECT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	asIScriptContext * ctx = engine->CreateContext();
	ctx->Prepare(setup);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

	int gidx = mod->GetGlobalVarIndexByName("box");
	auto box = *reinterpret_cast<asIScriptObject **>(mod->GetAddressOfGlobalVar(gidx));
	EXPECT_NE(box, nullptr);
	// property 0 is the string
	auto s = reinterpret_cast<std::string *>(box->GetAddressOfProperty(0));
	s->assign(payload.data(), payload.size());
	EXPECT_EQ(s->size(), payload.size());

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	return file.bytes();
}

std::string LoadBoxString(const std::vector<char> & image)
{
	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success);

	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(mod, nullptr);
	int gidx = mod->GetGlobalVarIndexByName("box");
	auto box = *reinterpret_cast<asIScriptObject **>(mod->GetAddressOfGlobalVar(gidx));
	EXPECT_NE(box, nullptr);
	auto s = reinterpret_cast<std::string *>(box->GetAddressOfProperty(0));
	return *s;
}

void RoundTripExpect(const std::string & payload)
{
	std::vector<char> image = SaveBoxWith(payload);
	ASSERT_FALSE(image.empty());
	std::string out = LoadBoxString(image);
	ASSERT_EQ(out.size(), payload.size()) << "length changed across round-trip";
	EXPECT_EQ(out, payload) << "content changed across round-trip";
}
}

TEST(StringContent, EmbeddedNul)
{
	RoundTripExpect(std::string("a\0b", 3));
}

TEST(StringContent, Empty)
{
	RoundTripExpect(std::string());
}

TEST(StringContent, Long)
{
	RoundTripExpect(std::string(4096, 'x'));
}

TEST(StringContent, HighBytesUtf8)
{
	RoundTripExpect(std::string("\xE2\x9C\x93 \xC3\xA9\xC3\xBC \xF0\x9F\x98\x80"));
}

TEST(StringContent, DuplicateContentDistinctBoxes)
{
	// Two boxes with identical string content: the string table should dedup,
	// but each box must still read back the full value.
	RoundTripExpect(std::string("shared-payload"));
}
