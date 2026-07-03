// Trust-boundary coverage — the string-index (badStr) field checks and the
// function-table typeId field checks in Verify() that test_malformed_input.cpp
// does not reach because its Node graph has no functions or templates.
//
// This image populates the function table (a plain-function handle + a delegate),
// the templates table (array<int>), typeInfo (Adder + funcdef), properties, and
// globals, so every badStr()/range check over those tables can be tripped. Each
// case corrupts exactly one field to == the string-table length (badStr rejects
// idx >= length) or past the type table, and asserts zE_BufferOverrun with no
// engine mutation. GREEN characterization — locks the boundary.
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
const char * kScript =
	"funcdef int BinOp(int, int);\n"
	"int add(int a, int b) { return a + b; }\n"
	"class Adder { int bias; int add(int a, int b) { return a + b + bias; } }\n"
	"Adder@ adder;\n"
	"BinOp@ gPlain;\n"
	"BinOp@ gDelegate;\n"
	"array<int> nums;\n"
	"void setup() {\n"
	"    @gPlain = @add;\n"
	"    @adder = Adder();\n"
	"    adder.bias = 100;\n"
	"    @gDelegate = BinOp(adder.add);\n"
	"    nums.insertLast(1); nums.insertLast(2);\n"
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
	asIScriptContext * ctx = engine->CreateContext();
	EXPECT_GE(ctx->Prepare(setup), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

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
	EXPECT_EQ(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr)
		<< "engine was mutated by a rejected load";
}

zCModule *   Modules(std::vector<char> & i)   { return reinterpret_cast<zCModule *>(i.data() + HeaderOf(i)->moduleDataOffset); }
zCFunction * Functions(std::vector<char> & i) { return reinterpret_cast<zCFunction *>(i.data() + HeaderOf(i)->functionTableOffset); }
zCGlobalInfo*Globals(std::vector<char> & i)   { return reinterpret_cast<zCGlobalInfo *>(i.data() + HeaderOf(i)->globalsOffset); }
zCTypeInfo * TypeInfos(std::vector<char> & i) { return reinterpret_cast<zCTypeInfo *>(i.data() + HeaderOf(i)->typeInfoOffset); }
zCProperty * Properties(std::vector<char> & i){ return reinterpret_cast<zCProperty *>(i.data() + HeaderOf(i)->propertiesOffset); }
zCTemplate * Templates(std::vector<char> & i) { return reinterpret_cast<zCTemplate *>(i.data() + HeaderOf(i)->templatesOffset); }

// An index one past the last valid string (badStr rejects idx >= length).
uint32_t BadStrIndex(std::vector<char> & i) { return HeaderOf(i)->stringTableByteLength; }
}

TEST(MalformedFields, ValidImageLoads)
{
	auto image = BuildValidImage();
	ASSERT_FALSE(image.empty());
	zCHeader * h = HeaderOf(image);
	// Confirm the richer tables we intend to corrupt are actually populated.
	EXPECT_GT(h->functionTableLength, 0u) << "no functions to corrupt";
	EXPECT_GT(h->templatesLength, 0u)     << "no templates to corrupt";
	EXPECT_GT(h->typeInfoLength, 0u);
	EXPECT_GT(h->propertiesLength, 0u);

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "baseline should load: " << zodiac->GetErrorString();
	EXPECT_NE(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr);
}

//----- module string index (Verify :178) -----
TEST(MalformedFields, ModuleNameStringIndex)
{
	auto image = BuildValidImage();
	Modules(image)[0].name = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//----- function table field checks (Verify :200-212) -----
TEST(MalformedFields, FunctionDelegateTypeIdOutOfRange)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->functionTableLength, 0u);
	Functions(image)[0].delegateTypeId =
		HeaderOf(image)->typeInfoLength + HeaderOf(image)->templatesLength + 1000;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, FunctionObjectTypeOutOfRange)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->functionTableLength, 0u);
	Functions(image)[0].objectType =
		HeaderOf(image)->typeInfoLength + HeaderOf(image)->templatesLength + 1000;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, FunctionModuleStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->functionTableLength, 0u);
	Functions(image)[0]._module = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, FunctionDeclarationStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->functionTableLength, 0u);
	Functions(image)[0].declaration = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//----- global string index checks (Verify :223-226) -----
TEST(MalformedFields, GlobalNameStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->globalsLength, 0u);
	Globals(image)[0].name = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, GlobalNamespaceStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->globalsLength, 0u);
	Globals(image)[0].nameSpace = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//----- typeInfo string index checks (Verify :246-249) -----
TEST(MalformedFields, TypeInfoNameStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->typeInfoLength, 0u);
	TypeInfos(image)[0].name = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, TypeInfoNamespaceStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->typeInfoLength, 0u);
	TypeInfos(image)[0].nameSpace = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//----- property name string index (Verify :264) -----
TEST(MalformedFields, PropertyNameStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->propertiesLength, 0u);
	Properties(image)[0].name = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//----- template field checks (Verify :282-291) -----
TEST(MalformedFields, TemplateNameStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->templatesLength, 0u);
	Templates(image)[0].name = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, TemplateNamespaceStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->templatesLength, 0u);
	Templates(image)[0].nameSpace = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, TemplateModuleStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->templatesLength, 0u);
	Templates(image)[0]._module = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedFields, TemplateDeclarationStringIndex)
{
	auto image = BuildValidImage();
	ASSERT_GT(HeaderOf(image)->templatesLength, 0u);
	Templates(image)[0].declaration = BadStrIndex(image);
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
