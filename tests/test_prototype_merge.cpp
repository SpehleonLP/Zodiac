// Parent Task 3 (Arc-1 hot reload): the embedded prototype table + provider hook.
//
// A "prototype" is the default-constructed field layout of a script class at save
// time. When a prototype provider is installed (SetPrototypeProvider), a scoped
// save embeds one prototype record per in-scope script-object type; the load side
// parses + Verify()-validates that table and (Task 4) uses it for a three-way
// property merge. THIS task only proves the table builds, round-trips, and that an
// unset provider leaves the file free of prototype data.
//
// Uses the two-image pattern the other Arc-1 tests use (save into one zCMemoryFile,
// load from a fresh one seeded with those bytes) because zCMemoryFile has no
// rewind(). The Task-4 merge asserts will extend THIS fixture.
#include <gtest/gtest.h>
#include "test_engine.h"
#include "memory_file.h"
#include "addons.h"
#include "zodiac.h"
#include "z_zodiacstate.h"

#include <angelscript.h>
#include <memory>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace {
constexpr char kSrcV1[] = R"(
	class Ball
	{
		float elasticity = 0.2f;   // authored config
		int   hits = 0;            // runtime state
	}
	Ball g_ball;
)";

// v2: the author edited the elasticity default 0.2 -> 0.5. Same field layout.
constexpr char kSrcV2[] = R"(
	class Ball
	{
		float elasticity = 0.5f;   // author edited 0.2 -> 0.5
		int   hits = 0;
	}
	Ball g_ball;
)";

void Compile(asIScriptEngine * engine, const char * name, const char * src)
{
	asIScriptModule * mod = engine->GetModule(name, asGM_ALWAYS_CREATE);
	ASSERT_NE(mod, nullptr);
	ASSERT_EQ(mod->AddScriptSection(name, src), 0);
	ASSERT_GE(mod->Build(), 0);
}

// Prototype provider: construct via the type's default factory. The provider owns
// the returned object's lifetime; the test releases them in teardown.
asIScriptObject * MakeProto(void * userData, asITypeInfo * type)
{
	auto * protos = (std::vector<asIScriptObject*>*)userData;
	asIScriptObject * obj = (asIScriptObject*)type->GetEngine()->CreateScriptObject(type);
	protos->push_back(obj);   // released in test teardown
	return obj;
}

// The header lives in the file's trailer.
const zCHeader * HeaderOf(const std::vector<char> & image)
{
	return reinterpret_cast<const zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
}
} // namespace

// With a provider installed, the scoped save embeds exactly one prototype (class
// Ball) and the remapped hot-reload load parses + validates the table and succeeds.
TEST(PrototypeTable, EmbedsAndRoundTrips)
{
	TestEngine engine;
	Compile(engine.get(), "pkg#1", kSrcV1);
	std::vector<asIScriptObject*> protos;

	std::vector<char> image;
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetProperty(zZP_SAVE_BYTECODE, false);
		z->SetSaveScope("pkg#1");
		z->SetUserData(&protos);
		z->SetPrototypeProvider(&MakeProto);
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	// The provider was asked for the one in-scope script class and the file records it.
	EXPECT_EQ(protos.size(), 1u);
	EXPECT_EQ(HeaderOf(image)->prototypeTableLength, 1u);

	Compile(engine.get(), "pkg#2", kSrcV1);   // "recompiled" module, same source
	{
		auto z = zCreateZodiac(engine.get());   // fresh instance (zE_DoubleLoad guard)
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		z->SetUserData(&protos);
		z->SetPrototypeProvider(&MakeProto);
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();   // file with proto table loads
	}

	for(auto * p : protos) if(p) p->Release();
}

// Unset provider = today's behavior: NO prototype table is written (zero-length),
// and the image still loads. This pins the "byte-identical payload when unset"
// contract.
TEST(PrototypeTable, UnsetProviderWritesNoTable)
{
	TestEngine engine;
	Compile(engine.get(), "pkg#1", kSrcV1);

	std::vector<char> image;
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetProperty(zZP_SAVE_BYTECODE, false);
		z->SetSaveScope("pkg#1");
		// no SetPrototypeProvider
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());
	EXPECT_EQ(HeaderOf(image)->prototypeTableLength, 0u);

	Compile(engine.get(), "pkg#2", kSrcV1);
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
	}
}

// -------------------------------------------------------------------------------
// Task 4: three-way merge. Save pkg#1 with a prototype provider (OLD default), then
// hot-reload into pkg#2 whose class edited the elasticity default. Each field runs
// (saved) vs (OLD default = embedded prototype) vs (NEW default = live class):
//   * saved == OLD default  -> field untouched by the object -> adopt NEW default.
//   * saved != OLD default  -> object diverged -> keep the saved value.
// -------------------------------------------------------------------------------

// Save one image of pkg#1's g_ball with the elasticity/hits the caller sets, with a
// prototype provider installed. Returns the bytes; `protos` collects provider-owned
// objects (released by the caller).
static std::vector<char> SaveV1(TestEngine & engine, float elasticity, int hits,
                                std::vector<asIScriptObject*> & protos)
{
	Compile(engine.get(), "pkg#1", kSrcV1);
	asIScriptModule * m1 = engine.get()->GetModule("pkg#1", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(m1, nullptr);
	auto * ball1 = (asIScriptObject*)m1->GetAddressOfGlobalVar(m1->GetGlobalVarIndexByName("g_ball"));
	EXPECT_NE(ball1, nullptr);
	EXPECT_EQ(ball1->GetPropertyCount(), 2u);
	*(float*)ball1->GetAddressOfProperty(0) = elasticity;   // 0 = elasticity
	*(int*)  ball1->GetAddressOfProperty(1) = hits;         // 1 = hits

	std::vector<char> image;
	auto z = zCreateZodiac(engine.get());
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, false);
	z->SetSaveScope("pkg#1");
	z->SetUserData(&protos);
	z->SetPrototypeProvider(&MakeProto);
	zCMemoryFile file;
	EXPECT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
	return file.bytes();
}

// live==oldProto(0.2) on elasticity -> code default (0.5) wins;
// live(42)!=oldProto(0) on hits     -> divergence respected.
TEST(ThreeWayMerge, SpecMatrix)
{
	TestEngine engine;
	std::vector<asIScriptObject*> protos;
	std::vector<char> image = SaveV1(engine, 0.2f /*untouched*/, 42 /*diverged*/, protos);
	ASSERT_FALSE(image.empty());

	Compile(engine.get(), "pkg#2", kSrcV2);   // the EDITED source
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		z->SetUserData(&protos);
		z->SetPrototypeProvider(&MakeProto);
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
	}

	asIScriptModule * m2 = engine.get()->GetModule("pkg#2", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m2, nullptr);
	auto * ball2 = (asIScriptObject*)m2->GetAddressOfGlobalVar(m2->GetGlobalVarIndexByName("g_ball"));
	ASSERT_NE(ball2, nullptr);
	ASSERT_EQ(ball2->GetPropertyCount(), 2u);
	EXPECT_FLOAT_EQ(*(float*)ball2->GetAddressOfProperty(0), 0.5f);   // code wins
	EXPECT_EQ(*(int*)ball2->GetAddressOfProperty(1), 42);            // divergence respected

	for(auto * p : protos) if(p) p->Release();
}

// The live instance mutated elasticity to 0.7 before save -> saved != OLD default
// -> divergence must survive the reload (stays 0.7, NOT the edited 0.5).
TEST(ThreeWayMerge, MutatedConfigKeepsDivergence)
{
	TestEngine engine;
	std::vector<asIScriptObject*> protos;
	std::vector<char> image = SaveV1(engine, 0.7f /*diverged config*/, 42, protos);
	ASSERT_FALSE(image.empty());

	Compile(engine.get(), "pkg#2", kSrcV2);
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		z->SetUserData(&protos);
		z->SetPrototypeProvider(&MakeProto);
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
	}

	asIScriptModule * m2 = engine.get()->GetModule("pkg#2", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m2, nullptr);
	auto * ball2 = (asIScriptObject*)m2->GetAddressOfGlobalVar(m2->GetGlobalVarIndexByName("g_ball"));
	ASSERT_NE(ball2, nullptr);
	ASSERT_EQ(ball2->GetPropertyCount(), 2u);
	EXPECT_FLOAT_EQ(*(float*)ball2->GetAddressOfProperty(0), 0.7f);   // divergence kept
	EXPECT_EQ(*(int*)ball2->GetAddressOfProperty(1), 42);

	for(auto * p : protos) if(p) p->Release();
}

// No prototype provider on EITHER side -> merge is inert -> every field keeps its
// saved value. The edited class default (0.5) must NOT propagate: elasticity stays
// the saved 0.2.
TEST(ThreeWayMerge, NoPrototypeMeansKeepLive)
{
	TestEngine engine;

	Compile(engine.get(), "pkg#1", kSrcV1);
	asIScriptModule * m1 = engine.get()->GetModule("pkg#1", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m1, nullptr);
	auto * ball1 = (asIScriptObject*)m1->GetAddressOfGlobalVar(m1->GetGlobalVarIndexByName("g_ball"));
	ASSERT_NE(ball1, nullptr);
	ASSERT_EQ(ball1->GetPropertyCount(), 2u);
	*(int*)ball1->GetAddressOfProperty(1) = 42;   // untouched elasticity stays 0.2

	std::vector<char> image;
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetProperty(zZP_SAVE_BYTECODE, false);
		z->SetSaveScope("pkg#1");
		// no SetPrototypeProvider
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	ASSERT_FALSE(image.empty());

	Compile(engine.get(), "pkg#2", kSrcV2);
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		// no SetPrototypeProvider
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
	}

	asIScriptModule * m2 = engine.get()->GetModule("pkg#2", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m2, nullptr);
	auto * ball2 = (asIScriptObject*)m2->GetAddressOfGlobalVar(m2->GetGlobalVarIndexByName("g_ball"));
	ASSERT_NE(ball2, nullptr);
	ASSERT_EQ(ball2->GetPropertyCount(), 2u);
	EXPECT_FLOAT_EQ(*(float*)ball2->GetAddressOfProperty(0), 0.2f);   // edit did NOT propagate
	EXPECT_EQ(*(int*)ball2->GetAddressOfProperty(1), 42);
}

// TRUST BOUNDARY: Verify() bounds the prototype record's typeId/address INDICES but
// not how many payload bytes the merge reads out of the referenced entry. A file
// can name a prototype `address` that points at a region too small for the object's
// fields (here entry 0 — the null entry, byteLength 0). The per-field bound in the
// merge (readOffset+size <= protoLen) MUST reject that read and degrade to keep-live
// instead of over-reading. Proof: the load still succeeds and every field keeps its
// SAVED value (the code default is NOT applied, because the OLD-default read was
// refused). Run under ASan, an unbounded read here would fault or be flagged.
TEST(ThreeWayMerge, PrototypeFieldReadPastRegionIsSkipped)
{
	TestEngine engine;
	std::vector<asIScriptObject*> protos;
	// elasticity untouched (== OLD default 0.2) so a WORKING merge would overwrite it
	// with the code default 0.5; the corrupted prototype must instead leave it at 0.2.
	std::vector<char> image = SaveV1(engine, 0.2f, 42, protos);
	ASSERT_FALSE(image.empty());
	ASSERT_EQ(HeaderOf(image)->prototypeTableLength, 1u);

	// Repoint the sole prototype record's payload at entry 0 (byteLength 0). Verify()
	// still admits it (0 < addressTableLength); only the merge-time bound stops the
	// out-of-region field read.
	const zCHeader * h = HeaderOf(image);
	auto * recs = reinterpret_cast<zCPrototype*>(image.data() + h->prototypeTableOffset);
	recs[0].address = 0;

	Compile(engine.get(), "pkg#2", kSrcV2);
	{
		auto z = zCreateZodiac(engine.get());
		RegisterZodiacAddons(z.get());
		z->SetModuleRemap("pkg#1", "pkg#2");
		z->SetUserData(&protos);
		z->SetPrototypeProvider(&MakeProto);
		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();   // safe, no crash
	}

	asIScriptModule * m2 = engine.get()->GetModule("pkg#2", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(m2, nullptr);
	auto * ball2 = (asIScriptObject*)m2->GetAddressOfGlobalVar(m2->GetGlobalVarIndexByName("g_ball"));
	ASSERT_NE(ball2, nullptr);
	ASSERT_EQ(ball2->GetPropertyCount(), 2u);
	// Merge refused (out-of-region OLD-default read) -> keep-live -> saved 0.2, NOT 0.5.
	EXPECT_FLOAT_EQ(*(float*)ball2->GetAddressOfProperty(0), 0.2f);
	EXPECT_EQ(*(int*)ball2->GetAddressOfProperty(1), 42);

	for(auto * p : protos) if(p) p->Release();
}
