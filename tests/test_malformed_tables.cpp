// Trust-boundary coverage — the per-table range checks, cross-table containment
// checks, and per-property/entry containment checks in zCZodiacReader::Verify().
//
// test_malformed_input.cpp exercises ONE representative of each corruption class;
// this file fills in the rest so that every independent Verify() branch has a
// regression test. These are GREEN characterization tests: they lock the trust
// boundary so a future refactor cannot silently delete a bounds check. Each case
// takes a valid saved image, corrupts exactly one field, and asserts a specific
// rejection Code with no engine mutation.
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
// Same graph as test_malformed_input.cpp: a script class with a primitive member
// and a handle member, one global handle, two heap objects. Guarantees non-empty
// module / typeInfo / property / globals / address tables to corrupt.
const char * kScript =
	"class Node { int value; Node@ next; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"    Node@ a = Node();\n"
	"    Node@ b = Node();\n"
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

// A count large enough that count*elemSize overflows any table's remaining file
// space, but not so large that count*sizeof wraps uint64 (elemSize <= 64).
constexpr uint32_t kHugeCount = 0x08000000u; // 128M entries
}

// Sanity — the uncorrupted image loads (guards against false-green where a case is
// rejected for the wrong reason / an empty table).
TEST(MalformedTables, ValidImageLoads)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());
	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "baseline should load: " << zodiac->GetErrorString();
	EXPECT_NE(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr);
}

//-----------------------------------------------------------------------------
// Per-table InFile range checks (Verify :123-146). Each table's length field is
// inflated so count*elemSize exceeds the file; must trip that table's check.
//-----------------------------------------------------------------------------
TEST(MalformedTables, SaveDataLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->saveDataByteLength = (uint32_t)image.size() + 4096;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, StringAddressLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->stringAddressLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, StringTableLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->stringTableByteLength = (uint32_t)image.size() + 4096;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, AddressTableLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->addressTableLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, SavedObjectLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->savedObjectLength = (uint32_t)image.size() + 4096;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, ModuleDataLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->moduleDataLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, TypeInfoLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->typeInfoLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, PropertiesLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->propertiesLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, GlobalsLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->globalsLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, FunctionTableLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->functionTableLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, TemplatesLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->templatesLength = kHugeCount;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, ByteCodeLengthOverrun)
{
	auto image = BuildValidImage();
	HeaderOf(image)->byteCodeByteLength = (uint32_t)image.size() + 4096;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//-----------------------------------------------------------------------------
// Cross-table containment checks (a module's sub-ranges must nest inside the
// parent tables; Verify :181-190, :252).
//-----------------------------------------------------------------------------
TEST(MalformedTables, ModuleByteCodeExtentPastRegion)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->moduleDataLength, 0u);
	zCModule * m = reinterpret_cast<zCModule *>(image.data() + h->moduleDataOffset);
	m[0].byteCodeLength = kHugeCount; // begin + length past the byteCode region
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, ModuleByteCodeOffsetBeforeRegion)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	zCModule * m = reinterpret_cast<zCModule *>(image.data() + h->moduleDataOffset);
	m[0].byteCodeOffset = h->byteCodeOffset == 0 ? 0 : h->byteCodeOffset - 1;
	// only meaningful when the module actually starts at/after the region start
	if(m[0].byteCodeOffset >= h->byteCodeOffset) GTEST_SKIP() << "no room below region";
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, ModuleTypeInfoExtentPastTable)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	zCModule * m = reinterpret_cast<zCModule *>(image.data() + h->moduleDataOffset);
	m[0].beginTypeInfo = h->typeInfoLength; // begin == count -> begin+len > count
	m[0].typeInfoLength = 1;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, ModuleGlobalsExtentPastTable)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	zCModule * m = reinterpret_cast<zCModule *>(image.data() + h->moduleDataOffset);
	m[0].beginGlobalInfo = h->globalsLength;
	m[0].globalsLength = 1;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, TypeInfoPropertiesExtentPastTable)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->typeInfoLength, 0u);
	zCTypeInfo * t = reinterpret_cast<zCTypeInfo *>(image.data() + h->typeInfoOffset);
	// Find the script type that actually has properties (Node).
	bool corrupted = false;
	for(uint32_t i = 0; i < h->typeInfoLength; ++i)
	{
		if(t[i].propertiesLength > 0)
		{
			t[i].propertiesBegin = h->propertiesLength; // begin == count
			corrupted = true;
			break;
		}
	}
	ASSERT_TRUE(corrupted) << "no type with properties to corrupt";
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//-----------------------------------------------------------------------------
// Property table field checks (Verify :264-272).
//-----------------------------------------------------------------------------
TEST(MalformedTables, PropertyObjectTypeIdOutOfRange)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->propertiesLength, 0u);
	zCProperty * p = reinterpret_cast<zCProperty *>(image.data() + h->propertiesOffset);
	// Forge a property claiming to be an object type whose type index (seqnbr) is
	// far past the type table. (Script handle members are stored with a small
	// remapped typeId and no object mask, so we synthesise the object-typed case
	// the :267 check exists to reject.)
	p[0].typeId = asTYPEID_MASK_OBJECT | 0x00FFFF00u; // seqnbr >> any real table length
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, PropertyNegativeByteLength)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->propertiesLength, 0u);
	zCProperty * p = reinterpret_cast<zCProperty *>(image.data() + h->propertiesOffset);
	p[0].byteLength = -1; // signed-size class
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

//-----------------------------------------------------------------------------
// Entry containment checks (Verify :306-346).
//-----------------------------------------------------------------------------
TEST(MalformedTables, EntryOffsetBeforeSavedObjectRegion)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->addressTableLength, 1u);
	zCEntry * e = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	// entry 0 is the null sentinel; corrupt entry 1 (a real object).
	e[1].offset = h->savedObjectOffset == 0 ? 0 : h->savedObjectOffset - 1;
	if(e[1].offset >= h->savedObjectOffset) GTEST_SKIP() << "region at file start";
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
TEST(MalformedTables, EntryExtentPastSavedObjectRegion)
{
	auto image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->addressTableLength, 1u);
	zCEntry * e = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	e[1].byteLength = kHugeCount; // offset + byteLength past region end
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
