// Defect #5 — a template-band entry.typeId indexes m_typeInfo unchecked
//             (src/z_zodiacreader.cpp:832 and :1063).
//
// Verify() bounds entry.typeId against typeTableLength() (== typeInfoLength() +
// templatesLength()) and DELIBERATELY skips the per-property containment checks
// for any typeId in the "template overflow" band [typeInfoLength, typeTableLength)
// — on the promise that such ids never index m_typeInfo (they name template
// instances / funcdefs handled elsewhere):
//
//     if(entry.typeId >= typeInfoLength())
//         continue;                       // Verify() — no property validation
//
// But PopulateTable:832 and RestoreScriptObjectContents:1063 both do
//     auto & zTypeInfo = m_typeInfo[m_entries[address].typeId];
//     const auto begin = &m_properties[zTypeInfo.propertiesBegin];
//     const auto end   = begin + zTypeInfo.propertiesLength;
//     for(auto p = begin; p < end; ++p) { ... p->writeType ... }   // (:839)
// UNCONDITIONALLY. m_typeInfo is a pointer INTO the file buffer, so a template-band
// typeId reads a zCTypeInfo-shaped slice of some *other* table; the bogus
// propertiesBegin/propertiesLength it yields then indexes the heap-allocated
// m_properties std::vector out of bounds.
//
// Reproduction: many template instances widen the band; a script class `Outer`
// owns a NON-handle `Inner b` so the Inner instance is restored through
// PopulateTable:832. We scan the band for an index whose misread zCTypeInfo has a
// propertiesBegin/Length that overruns m_properties, and point the Inner entry's
// typeId at it. Verify still admits it (in-band ⇒ property checks skipped).
//
// RED (pre-fix, -DZODIAC_INSTRUMENT_LIB=ON): ASan heap-buffer-overflow reading the
// m_properties vector inside PopulateTable.
// GREEN (post-fix): the load is rejected with a Zodiac::Code instead of indexing
// m_typeInfo / m_properties with a template-band id.
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
// Many distinct template instantiations widen the overflow band so the scan below
// has candidate slots that fall across varied (large-valued) file regions.
const char * kScript =
	"class Inner { int v; }\n"
	"class Outer { Inner b; }\n"
	"array<int>            g1 = {1};\n"
	"array<float>          g2 = {1.0f};\n"
	"array<double>         g3 = {1.0};\n"
	"array<string>         g4 = {'x'};\n"
	"array<bool>           g5 = {true};\n"
	"array<int8>           g6 = {int8(1)};\n"
	"array<int16>          g7 = {int16(1)};\n"
	"array<array<int>>     g8;\n"
	"array<Outer@>         g9;\n"
	"dictionary            gd;\n"
	"Outer@ o;\n"
	"void setup() {\n"
	"    @o = Outer();\n"
	"    o.b.v = 5;\n"
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

Code DoLoad(zIZodiac * z, zCMemoryFile & f)
{
	try { return z->LoadFromFile(&f); }
	catch(const Zodiac::Exception & e) { return e.code; }
	catch(Zodiac::Code c) { return c; }
}
}

TEST(TypeInfoOverflow, ValidImageLoads)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file(image);
	EXPECT_EQ(zodiac->LoadFromFile(&file), zE_Success)
		<< "baseline image should load: " << zodiac->GetErrorString();
}

TEST(TypeInfoOverflow, EntryTypeIdInTemplateBandRejected)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	zCHeader * h = HeaderOf(image);

	ASSERT_GT(h->templatesLength, 0u) << "no template instances";
	const uint32_t typeTableLength = h->typeInfoLength + h->templatesLength;

	auto * entries   = reinterpret_cast<zCEntry *>(image.data() + h->addressTableOffset);
	auto * typeInfos = reinterpret_cast<zCTypeInfo *>(image.data() + h->typeInfoOffset);

	auto strName = [&](uint32_t off) -> std::string {
		return std::string(image.data() + h->stringTableOffset + off);
	};

	int tiInner = -1;
	for(uint32_t i = 0; i < h->typeInfoLength; ++i)
		if(strName(typeInfos[i].name) == "Inner") { tiInner = (int)i; break; }
	ASSERT_GE(tiInner, 0) << "Inner type not found";

	int eInner = -1;
	for(uint32_t a = 0; a < h->addressTableLength; ++a)
		if(entries[a].typeId == (uint32_t)tiInner) { eInner = (int)a; break; }
	ASSERT_GE(eInner, 0) << "no Inner instance entry";

	// Scan the template band for a slot whose bytes, misread as a zCTypeInfo, yield
	// a (propertiesBegin, propertiesLength) that will walk m_properties (size ==
	// propertiesLength()) out of bounds. m_typeInfo is file-backed, so reading the
	// slot itself stays in the buffer; the OOB lands on the heap m_properties.
	uint32_t bandTypeId = 0;
	for(uint32_t k = h->typeInfoLength; k < typeTableLength; ++k)
	{
		const zCTypeInfo & slot = typeInfos[k];
		const uint64_t reach = (uint64_t)slot.propertiesBegin + slot.propertiesLength;
		if(slot.propertiesLength > 0 && reach > h->propertiesLength)
		{
			bandTypeId = k;
			break;
		}
	}
	ASSERT_NE(bandTypeId, 0u)
		<< "no template-band slot produced an out-of-range property range; "
		   "widen kScript's template set";
	ASSERT_LT(bandTypeId, typeTableLength);        // still admitted by Verify

	entries[eInner].typeId = bandTypeId;

	// Run the load in a forked child. Pre-fix a template-band typeId misreads
	// m_typeInfo → bogus property range → OOB read of the heap m_properties vector
	// (ASan heap-buffer-overflow when instrumented) or trips the writeType assert in
	// PopulateTable — either way the child dies (RED). Post-fix the load rejects the
	// image with a Code and the child exits 0 (GREEN). The death test keeps a pre-fix
	// abort from taking down the whole suite.
	EXPECT_EXIT({
		TestEngine engine;
		auto zodiac = MakeZodiac(engine.get());
		zCMemoryFile file(image);
		Code rc = DoLoad(zodiac.get(), file);
		_exit(rc != zE_Success ? 0 : 1);
	}, ::testing::ExitedWithCode(0), ".*")
		<< "a template-band entry typeId must be rejected, not dereferenced";
}
