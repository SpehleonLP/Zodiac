// Defect #2 — PopulateTable off-by-one (src/z_zodiacreader.cpp:811).
//
//   if(address > addressTableLength())   // BUG: should be >=
//       throw Exception(zE_BadObjectAddress);
//   ... m_loadedObjects[address] ...      // OOB when address == length
//
// `address` here is an object index read out of a script object's serialized
// blob (the 4-byte inline value of a NON-handle script-object member). Verify()
// never range-checks these payload-embedded member indices, so an attacker can
// set one to exactly addressTableLength() — one past the last valid entry. The
// `>` guard lets that value through and the code then indexes
// m_loadedObjects[length], a heap-buffer-overflow.
//
// Reproduction: a script class `Outer` with a NON-handle member `Inner b`
// (declared without `@`, so it has asTYPEID_SCRIPTOBJECT set and OBJHANDLE
// clear — the only member shape that reaches line 811). We save a valid image,
// locate Outer's instance blob and the `b` member's on-disk offset, and set the
// inline object index there to addressTableLength().
//
// RED (pre-fix): with the library ASan-instrumented (-DZODIAC_INSTRUMENT_LIB=ON)
// this aborts with a heap-buffer-overflow on m_loadedObjects[length] at
// z_zodiacreader.cpp:811-814.
// GREEN (post-fix, `>=`): LoadFromFile returns zE_BadObjectAddress and the
// engine is left unmutated.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"
#include "z_zodiacstate.h"
#include "z_zodiacexception.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// The object-graph restore runs in RestoreGlobalVariables, OUTSIDE LoadFromFile's
// try/catch, so a rejection there PROPAGATES as a thrown Zodiac::Exception/Code
// rather than a returned Code. Normalise both to a Code so the post-fix rejection
// is observable. (Pre-fix the process ASan-aborts before returning here.)
Code DoLoad(zIZodiac * z, zCMemoryFile & f)
{
	try { return z->LoadFromFile(&f); }
	catch(const Zodiac::Exception & e) { return e.code; }
	catch(Zodiac::Code c) { return c; }
}
}

namespace
{
// Outer owns Inner by composition (`Inner b;`, no `@`) so the saved blob holds
// an inline (non-handle) object index for `b` — the value PopulateTable reads.
const char * kScript =
	"class Inner { int v; }\n"
	"class Outer { Inner b; }\n"
	"Outer@ o;\n"
	"void setup() {\n"
	"    @o = Outer();\n"
	"    o.b.v = 42;\n"
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
}

// Sanity: the uncorrupted image loads (guards against a false-green).
TEST(PopulateOOB, ValidImageLoads)
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

TEST(PopulateOOB, InlineMemberIndexEqualsLengthRejected)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	zCHeader * h = HeaderOf(image);
	auto * entries   = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	auto * typeInfos = reinterpret_cast<zCTypeInfo *>(image.data() + h->typeInfoOffset);
	auto * props     = reinterpret_cast<zCProperty *>(image.data() + h->propertiesOffset);

	auto strName = [&](uint32_t off) -> std::string {
		return std::string(image.data() + h->stringTableOffset + off);
	};

	// Locate Outer's zCTypeInfo (its table index == the stored typeId script-object
	// entries of that type carry).
	int tiOuter = -1;
	for(uint32_t i = 0; i < h->typeInfoLength; ++i)
		if(strName(typeInfos[i].name) == "Outer") { tiOuter = (int)i; break; }
	ASSERT_GE(tiOuter, 0) << "Outer type not found in image";

	// Locate the `b` member and its byte offset inside the Outer blob.
	auto & ti = typeInfos[tiOuter];
	uint32_t bOffset = ~0u;
	for(uint32_t k = ti.propertiesBegin; k < ti.propertiesBegin + ti.propertiesLength; ++k)
		if(strName(props[k].name) == "b") { bOffset = props[k].offset; break; }
	ASSERT_NE(bOffset, ~0u) << "member `b` not found on Outer";

	// Find the Outer instance entry.
	int eOuter = -1;
	for(uint32_t a = 0; a < h->addressTableLength; ++a)
		if(entries[a].typeId == (uint32_t)tiOuter) { eOuter = (int)a; break; }
	ASSERT_GE(eOuter, 0) << "no Outer instance entry";

	// Overwrite the inline `b` object index with addressTableLength() — one past
	// the last valid entry. Verify() does not bound payload-embedded member
	// indices, so this reaches PopulateTable:811.
	uint32_t * slot = reinterpret_cast<uint32_t *>(
		image.data() + entries[eOuter].offset + bOffset);
	*slot = h->addressTableLength;

	// Run the load in a forked child: pre-fix it either ASan-aborts (lib
	// instrumented) or reads OOB garbage / trips a lib assert and dies — any of
	// which is a non-clean exit (RED). Post-fix (`>=`) the load rejects the image
	// with zE_BadObjectAddress and the child exits 0 (GREEN). Containing it in a
	// death test keeps a pre-fix abort from taking down the whole suite.
	EXPECT_EXIT({
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		Code rc = DoLoad(zodiac.get(), file);
		_exit(rc == zE_BadObjectAddress ? 0 : 1);
	}, ::testing::ExitedWithCode(0), ".*")
		<< "an out-of-range inline member index must be rejected, not dereferenced";
}
