// Defect #3 — LoadFunction off-by-one (src/z_zodiacreader.cpp:1192).
//
//   if((uint)id > functionTableLength())   // BUG: should be >=
//       throw Exception(zE_BadObjectAddress);
//   auto & function = GetFunctions()[id];   // OOB when id == length
//
// `id` is a function-table index read out of an object's serialized blob — the
// 4-byte value of a funcdef/delegate MEMBER. Verify() bounds funcdef indices only
// for GLOBALS (with a correct `>=`); the per-member funcdef indices embedded in a
// script object's payload are never range-checked. So a crafted image can set a
// member's function index to exactly functionTableLength() — one past the last
// valid function. The `>` guard passes it and the code indexes
// GetFunctions()[length] / m_loadedFunctions[length].
//
// Reproduction: `funcdef void CB(); class Holder { CB@ cb; }` with cb bound to a
// global function, so the Holder blob stores a function-table index for cb. We
// save, locate Holder's blob and cb's on-disk offset, and set that index to
// functionTableLength().
//
// RED (pre-fix, -DZODIAC_INSTRUMENT_LIB=ON): ASan heap-buffer-overflow on
// GetFunctions()[length] inside zCZodiacReader::LoadFunction.
// GREEN (post-fix, `>=`): a clean zE_BadObjectAddress.
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
const char * kScript =
	"funcdef void CB();\n"
	"void impl() {}\n"
	"class Holder { CB@ cb; }\n"
	"Holder@ h;\n"
	"void setup() {\n"
	"    Holder@ x = Holder();\n"
	"    @x.cb = @impl;\n"
	"    @h = x;\n"
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

// The object restore runs outside LoadFromFile's try/catch, so a rejection there
// propagates as a thrown Zodiac::Exception/Code. Normalise to a Code.
Code DoLoad(zIZodiac * z, zCMemoryFile & f)
{
	try { return z->LoadFromFile(&f); }
	catch(const Zodiac::Exception & e) { return e.code; }
	catch(Zodiac::Code c) { return c; }
}
}

TEST(LoadFunctionOOB, ValidImageLoads)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "baseline image should load: " << zodiac->GetErrorString();
}

TEST(LoadFunctionOOB, MemberFunctionIndexEqualsLengthRejected)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	zCHeader * h = HeaderOf(image);
	ASSERT_GT(h->functionTableLength, 0u);

	auto * entries   = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	auto * typeInfos = reinterpret_cast<zCTypeInfo *>(image.data() + h->typeInfoOffset);
	auto * props     = reinterpret_cast<zCProperty *>(image.data() + h->propertiesOffset);

	auto strName = [&](uint32_t off) -> std::string {
		return std::string(image.data() + h->stringTableOffset + off);
	};

	int tiHolder = -1;
	for(uint32_t i = 0; i < h->typeInfoLength; ++i)
		if(strName(typeInfos[i].name) == "Holder") { tiHolder = (int)i; break; }
	ASSERT_GE(tiHolder, 0) << "Holder type not found";

	auto & ti = typeInfos[tiHolder];
	uint32_t cbOffset = ~0u;
	for(uint32_t k = ti.propertiesBegin; k < ti.propertiesBegin + ti.propertiesLength; ++k)
		if(strName(props[k].name) == "cb") { cbOffset = props[k].offset; break; }
	ASSERT_NE(cbOffset, ~0u) << "member `cb` not found on Holder";

	int eHolder = -1;
	for(uint32_t a = 0; a < h->addressTableLength; ++a)
		if(entries[a].typeId == (uint32_t)tiHolder) { eHolder = (int)a; break; }
	ASSERT_GE(eHolder, 0) << "no Holder instance entry";

	// Set the cb member's function-table index to functionTableLength() — one past
	// the last valid function. This member index is never bounded by Verify.
	uint32_t * slot = reinterpret_cast<uint32_t *>(
		image.data() + entries[eHolder].offset + cbOffset);
	*slot = h->functionTableLength;

	// Run the load in a forked child: pre-fix it ASan-aborts (instrumented) or
	// reads OOB garbage / dies (stock) — a non-clean exit (RED). Post-fix (`>=`)
	// it rejects with zE_BadObjectAddress and exits 0 (GREEN). The death test keeps
	// a pre-fix abort from taking down the whole suite.
	EXPECT_EXIT({
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		Code rc = DoLoad(zodiac.get(), file);
		_exit(rc == zE_BadObjectAddress ? 0 : 1);
	}, ::testing::ExitedWithCode(0), ".*")
		<< "an out-of-range member function index must be rejected, not dereferenced";
}
