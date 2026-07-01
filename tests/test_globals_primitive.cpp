// Red-hunt: NON-handle globals. Every existing round-trip test stores handle
// globals (Node@, C@, BinOp@). Primitive, bool, enum, string, and const globals
// exercise the value path in WriteGlobalVariables / RestoreGlobalVariables that
// nothing currently touches.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
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
template<typename T> T GlobalVal(asIScriptModule * mod, const char * name)
{
	int gidx = mod->GetGlobalVarIndexByName(name);
	EXPECT_GE(gidx, 0) << "global " << name << " missing";
	T v{}; if(gidx >= 0) std::memcpy(&v, mod->GetAddressOfGlobalVar(gidx), sizeof(T));
	return v;
}
}

TEST(PrimitiveGlobals, ValueGlobalsRoundTrip)
{
	const char * script =
		"int      gi = 42;\n"
		"float    gf = 3.5f;\n"
		"bool     gb = true;\n"
		"double   gd = 2.25;\n"
		"const int gc = 7;\n"
		"string   gs = 'hello';\n"
		"void setup() {}\n";

	std::vector<char> image;
	{
		TestEngine e;
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", script), 0);
		ASSERT_GE(mod->Build(), 0);
		auto zodiac = MakeZodiac(e.get());
		zCMemoryFile file;
		ASSERT_EQ(zodiac->SaveToFile(&file), zE_Success);
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto zodiac = MakeZodiac(e.get());
	zCMemoryFile file(image);
	ASSERT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "load failed: " << zodiac->GetErrorString();

	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	EXPECT_EQ(GlobalVal<int>(mod, "gi"), 42);
	EXPECT_FLOAT_EQ(GlobalVal<float>(mod, "gf"), 3.5f);
	EXPECT_EQ(GlobalVal<bool>(mod, "gb"), true);
	EXPECT_DOUBLE_EQ(GlobalVal<double>(mod, "gd"), 2.25);
	EXPECT_EQ(GlobalVal<int>(mod, "gc"), 7) << "const global not restored";

	int gsIdx = mod->GetGlobalVarIndexByName("gs");
	ASSERT_GE(gsIdx, 0);
	auto gs = reinterpret_cast<std::string *>(mod->GetAddressOfGlobalVar(gsIdx));
	EXPECT_EQ(*gs, "hello") << "string global not restored";
}
