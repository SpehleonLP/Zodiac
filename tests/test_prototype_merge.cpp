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
