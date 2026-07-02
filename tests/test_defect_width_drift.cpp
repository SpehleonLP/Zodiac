// Defect regression test for primitive-property width drift
// (src/z_zodiacreader.cpp:1125).
//
// When a script object's primitive member is restored, RestoreScriptObject copies
// GetSizeOfPrimitiveType(writeType) bytes using the LIVE (destination) type width
// and ignores the stored (source) width — unlike top-level primitives, which go
// through RestorePrimitive with a real src->dst conversion. So loading an image
// where a member was saved as `int` (4 bytes) into a module where that same-named
// member is now `double` (8 bytes) memcpy's 8 bytes out of the 4-byte stored slot:
// a 4-byte over-read that fills the double with adjacent bytes instead of the
// converted value. Correct behavior is to convert the value (or reject the image);
// either makes this green. Pre-fix the load "succeeds" with a garbage double.
//
// Mechanism (mirrors test_schema_drift.cpp): bytecode is saved, but the load engine
// PRE-COMPILES module "m" from the drifted source, so the drifted (double) type is
// kept and unification runs stored-vs-drifted by property name.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
const asDWORD kSavedValue = 287454020u; // 0x11223344

const char * kSaveSource =
	"class C { int a; }\n"
	"C@ obj;\n"
	"void setup() { @obj = C(); obj.a = 287454020; }\n";

const char * kDriftSource = // same member name 'a', now a wider type
	"class C { double a; }\n"
	"C@ obj;\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

std::vector<char> SaveImage()
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kSaveSource), 0);
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

double ReadDoubleProp(asIScriptObject * obj, const char * name)
{
	for(asUINT i = 0; i < obj->GetPropertyCount(); ++i)
		if(std::strcmp(obj->GetPropertyName(i), name) == 0)
		{
			double v = 0;
			std::memcpy(&v, obj->GetAddressOfProperty(i), sizeof(v));
			return v;
		}
	return -1;
}
}

TEST(WidthDrift, IntToDoubleMemberIsConvertedNotReinterpreted)
{
	std::vector<char> image = SaveImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_NE(mod, nullptr);
	ASSERT_GE(mod->AddScriptSection("m", kDriftSource), 0);
	ASSERT_GE(mod->Build(), 0);

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	Code rc = zodiac->LoadFromFile(&file);

	if(rc != zE_Success)
	{
		SUCCEED() << "load rejected the width drift (an acceptable outcome)";
		return;
	}

	mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	int gidx = mod->GetGlobalVarIndexByName("obj");
	ASSERT_GE(gidx, 0);
	asIScriptObject * obj = nullptr;
	std::memcpy(&obj, mod->GetAddressOfGlobalVar(gidx), sizeof(obj));
	ASSERT_NE(obj, nullptr);

	EXPECT_DOUBLE_EQ(ReadDoubleProp(obj, "a"), (double)kSavedValue)
		<< "int->double member must be converted, not filled by over-reading the "
		   "narrower stored slot";
}
