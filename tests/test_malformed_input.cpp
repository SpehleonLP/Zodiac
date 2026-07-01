// Spec test 9: the malformed-input regression net. Each case takes a VALID saved
// image, corrupts exactly one field to trip exactly one Verify() rule, feeds it
// to LoadFromFile, and asserts:
//   * a specific non-success Code is returned (no crash / OOB — this is the whole
//     point of the trust-boundary rebuild; pre-fix these read out of bounds),
//   * the engine is NOT mutated (the restored module must not appear),
//   * ASan/UBSan stay clean (the test target is instrumented).
//
// Verify() runs inside the reader ctor, before any engine state is touched, so a
// rejected load leaves the fresh engine empty.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"
#include "z_zodiacstate.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// A script class with properties + a handle so the saved image has a non-empty
// type-info table, property table, and an addressed-object entry to corrupt.
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

// Build the Node graph and return a valid saved image.
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

// The header lives in the file's trailer.
zCHeader * HeaderOf(std::vector<char> & image)
{
	return reinterpret_cast<zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
}

// Feed a (corrupted) image to a fresh engine, expect the given rejection Code and
// no engine mutation.
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
}

// Sanity: the uncorrupted image built here really does load (guards against a
// false-green where every case is rejected for the wrong reason).
TEST(MalformedInput, ValidImageLoads)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "baseline image should load: " << zodiac->GetErrorString();
	EXPECT_NE(engine->GetModule("m", asGM_ONLY_IF_EXISTS), nullptr);
}

// Rule B.1 — file shorter than the header (pointer underflow pre-fix).
TEST(MalformedInput, ShortFile)
{
	std::vector<char> image(8, '\0');
	ExpectRejected(std::move(image), zE_BadFileType);
}

// Rule B.2 — bad magic.
TEST(MalformedInput, BadMagic)
{
	std::vector<char> image = BuildValidImage();
	HeaderOf(image)->magic[0] ^= 0xFF;
	ExpectRejected(std::move(image), zE_BadFileType);
}

// Rule B.2 — header self-consistency (pointer size mismatch).
TEST(MalformedInput, HeaderPointerSizeMismatch)
{
	std::vector<char> image = BuildValidImage();
	HeaderOf(image)->pointerSize = 3; // not sizeof(void*)
	ExpectRejected(std::move(image), zE_BadFileType);
}

// Rule B.3 — a table start offset past EOF.
TEST(MalformedInput, TableOffsetPastEnd)
{
	std::vector<char> image = BuildValidImage();
	HeaderOf(image)->stringTableOffset = (uint32_t)image.size() + 4096;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

// Rule B.3 — 32-bit offset+length overflow must not bypass the range check.
TEST(MalformedInput, OffsetLengthOverflow)
{
	std::vector<char> image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	h->addressTableOffset = 0xFFFFFFF0u;   // offset + count*sizeof(zCEntry) wraps in 32-bit
	h->addressTableLength = 16;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

// Rule B.4 — string table not NUL-terminated at its final byte.
TEST(MalformedInput, StringTableNotTerminated)
{
	std::vector<char> image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->stringTableByteLength, 0u);
	image[h->stringTableOffset + h->stringTableByteLength - 1] = 'X';
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

// Rule B.6 — zero module records (moduleDataLength()-1 underflow pre-fix).
TEST(MalformedInput, ZeroModules)
{
	std::vector<char> image = BuildValidImage();
	HeaderOf(image)->moduleDataLength = 0;
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

// Rule B.5 / #9 — a string index == table length must be rejected (>= not >).
TEST(MalformedInput, StringIndexEqualsLength)
{
	std::vector<char> image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->moduleDataLength, 0u);
	zCModule * modules = reinterpret_cast<zCModule *>(image.data() + h->moduleDataOffset);
	modules[0].name = h->stringTableByteLength; // one past the last valid index
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

// Rule B.5 — zCEntry.typeId used as an unvalidated m_typeInfo index.
TEST(MalformedInput, EntryTypeIdOutOfRange)
{
	std::vector<char> image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->addressTableLength, 0u);
	zCEntry * entries = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	entries[0].typeId = h->typeInfoLength + 1000; // far past the type-info table
	ExpectRejected(std::move(image), zE_BufferOverrun);
}

// Rule B.5 — zCEntry.owner used as an unvalidated address-table index.
TEST(MalformedInput, EntryOwnerOutOfRange)
{
	std::vector<char> image = BuildValidImage();
	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->addressTableLength, 0u);
	zCEntry * entries = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	entries[0].owner = h->addressTableLength; // one past the last valid entry index
	ExpectRejected(std::move(image), zE_BufferOverrun);
}
