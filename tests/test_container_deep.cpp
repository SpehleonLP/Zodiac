// Red-hunt (2026-07-02): container/add-on serialization corners the Part A
// acceptance tests did not reach. Existing coverage: array<int|string|Node@>,
// grid<int>, any<int64|Node@>, dictionary mixed values, weakref live/expired.
// These push further:
//   * GridHandleIdentity          — grid<Node@> element aliasing a global handle;
//                                    only grid<int> (POD) is tested.
//   * NestedIntArray              — array<array<int>>; container nesting untested.
//   * AnyHoldsString              — any storing a heap value type (string); only
//                                    int64 + handle payloads tested.
//   * AnyEmpty                    — an any that holds no value round-trips as empty.
//   * DictionaryHandleAliasesGlobal — a dictionary handle value aliasing a global;
//                                    identity through the dictionary untested.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptgrid/scriptgrid.h"
#include "add_on/scriptany/scriptany.h"
#include "add_on/scriptdictionary/scriptdictionary.h"
#include "add_on/scriptstdstring/scriptstdstring.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

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

void BuildAndSetup(asIScriptEngine * engine, const char * src)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("m", src), 0);
	ASSERT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	ASSERT_NE(setup, nullptr);
	asIScriptContext * ctx = engine->CreateContext();
	ctx->Prepare(setup);
	ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();
}

std::vector<char> SaveEngine(asIScriptEngine * engine)
{
	auto zodiac = MakeZodiac(engine);
	zCMemoryFile file;
	auto rc = zodiac->SaveToFile(&file);
	if(rc != zE_Success) fprintf(stderr, "SAVE ERR %d: %s\n", rc, zodiac->GetErrorString());
	EXPECT_EQ(rc, zE_Success);
	return file.bytes();
}

void LoadEngine(asIScriptEngine * engine, const std::vector<char> & image)
{
	auto zodiac = MakeZodiac(engine);
	zCMemoryFile file(image);
	auto rc = zodiac->LoadFromFile(&file);
	if(rc != zE_Success) fprintf(stderr, "LOAD ERR %d: %s\n", rc, zodiac->GetErrorString());
	ASSERT_EQ(rc, zE_Success);
}

template<typename T>
T * GlobalObject(asIScriptEngine * engine, const char * name)
{
	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	EXPECT_NE(mod, nullptr);
	if(!mod) return nullptr;
	int gidx = mod->GetGlobalVarIndexByName(name);
	EXPECT_GE(gidx, 0);
	return *reinterpret_cast<T **>(mod->GetAddressOfGlobalVar(gidx));
}
}

TEST(ContainerDeep, GridHandleIdentity)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"Node@ g;\n"
			"grid<Node@>@ gr;\n"
			"void setup(){\n"
			"  Node@ n = Node(); n.v = 9;\n"
			"  @g = n;\n"
			"  grid<Node@> x(1, 1);\n"
			"  @x[0, 0] = n;\n"
			"  @gr = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto grid = GlobalObject<CScriptGrid>(engine.get(), "gr");
	ASSERT_NE(grid, nullptr);
	ASSERT_EQ(grid->GetWidth(), 1u);
	ASSERT_EQ(grid->GetHeight(), 1u);

	auto elem = *static_cast<asIScriptObject**>(grid->At(0, 0));
	ASSERT_NE(elem, nullptr);
	EXPECT_EQ(*static_cast<int*>(elem->GetAddressOfProperty(0)), 9);

	auto g = GlobalObject<asIScriptObject>(engine.get(), "g");
	EXPECT_EQ(elem, g) << "grid element must alias the same restored object as global g";
}

TEST(ContainerDeep, NestedIntArray)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"array<array<int>>@ a;\n"
			"void setup(){\n"
			"  array<array<int>> x = {{1, 2, 3}, {4, 5}};\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto outer = GlobalObject<CScriptArray>(engine.get(), "a");
	ASSERT_NE(outer, nullptr);
	ASSERT_EQ(outer->GetSize(), 2u);

	// array<int> is a reference type, so array<array<int>> stores its elements as
	// (non-handle) ref objects: CScriptArray::At already dereferences to the inner
	// object (stock scriptarray.cpp:963), so a single cast yields the row -- a
	// second `*` (the handle-slot idiom used for array<Node@>) would read into the
	// object and crash.
	auto row0 = static_cast<CScriptArray*>(outer->At(0));
	auto row1 = static_cast<CScriptArray*>(outer->At(1));
	ASSERT_NE(row0, nullptr);
	ASSERT_NE(row1, nullptr);
	ASSERT_EQ(row0->GetSize(), 3u);
	ASSERT_EQ(row1->GetSize(), 2u);
	EXPECT_EQ(*static_cast<int*>(row0->At(2)), 3);
	EXPECT_EQ(*static_cast<int*>(row1->At(1)), 5);
}

TEST(ContainerDeep, AnyHoldsString)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"any@ a;\n"
			"void setup(){\n"
			"  any x;\n"
			"  string s = 'payload';\n"
			"  x.store(s);\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto any = GlobalObject<CScriptAny>(engine.get(), "a");
	ASSERT_NE(any, nullptr);

	int strTypeId = engine->GetTypeIdByDecl("string");
	ASSERT_GE(strTypeId, 0);
	std::string out;
	ASSERT_TRUE(any->Retrieve(&out, strTypeId)) << "any did not hold a string after round-trip";
	EXPECT_EQ(out, "payload");
}

TEST(ContainerDeep, AnyEmpty)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"any@ a;\n"
			"void setup(){ any x; @a = x; }\n");  // never store()d
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto any = GlobalObject<CScriptAny>(engine.get(), "a");
	ASSERT_NE(any, nullptr);
	EXPECT_EQ(any->GetTypeId(), 0) << "an empty any must round-trip as empty (typeId 0)";
}

TEST(ContainerDeep, GridStringValues)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"grid<string>@ gr;\n"
			"void setup(){\n"
			"  grid<string> x(2, 1);\n"
			"  x[0, 0] = 'foo';\n"
			"  x[1, 0] = 'a longer value';\n"
			"  @gr = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto grid = GlobalObject<CScriptGrid>(engine.get(), "gr");
	ASSERT_NE(grid, nullptr);
	ASSERT_EQ(grid->GetWidth(), 2u);
	ASSERT_EQ(grid->GetHeight(), 1u);
	EXPECT_EQ(*static_cast<std::string*>(grid->At(0, 0)), "foo");
	EXPECT_EQ(*static_cast<std::string*>(grid->At(1, 0)), "a longer value");
}

TEST(ContainerDeep, DictionaryNestedArray)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"dictionary@ d;\n"
			"void setup(){\n"
			"  dictionary x;\n"
			"  array<int> a = {1, 2, 3};\n"
			"  x.set('arr', @a);\n"
			"  @d = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto dict = GlobalObject<CScriptDictionary>(engine.get(), "d");
	ASSERT_NE(dict, nullptr);

	int arrHandleTypeId = engine->GetTypeIdByDecl("array<int>") | asTYPEID_OBJHANDLE;
	CScriptArray * arr = nullptr;
	ASSERT_TRUE(dict->Get("arr", &arr, arrHandleTypeId)) << "dictionary lost its array value";
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(arr->GetSize(), 3u);
	EXPECT_EQ(*static_cast<int*>(arr->At(2)), 3);
	if(arr) arr->Release();
}

TEST(ContainerDeep, NestedStringArray)
{
	// array<array<string>> — nested containers whose leaves are heap value types.
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"array<array<string>>@ a;\n"
			"void setup(){\n"
			"  array<array<string>> x = {{'a', 'bb'}, {'ccc'}};\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto outer = GlobalObject<CScriptArray>(engine.get(), "a");
	ASSERT_NE(outer, nullptr);
	ASSERT_EQ(outer->GetSize(), 2u);
	// Non-handle ref element: At() already dereferences to the inner array.
	auto row0 = static_cast<CScriptArray*>(outer->At(0));
	ASSERT_NE(row0, nullptr);
	ASSERT_EQ(row0->GetSize(), 2u);
	EXPECT_EQ(*static_cast<std::string*>(row0->At(1)), "bb");
}

TEST(ContainerDeep, ArrayOfDictionaries)
{
	// array<dictionary@> — array elements that are themselves ref-type containers.
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"array<dictionary@>@ a;\n"
			"void setup(){\n"
			"  dictionary d0; d0.set('k', 11);\n"
			"  dictionary d1; d1.set('k', 22);\n"
			"  array<dictionary@> x = {@d0, @d1};\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto arr = GlobalObject<CScriptArray>(engine.get(), "a");
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(arr->GetSize(), 2u);
	auto d1 = *static_cast<CScriptDictionary**>(arr->At(1));
	ASSERT_NE(d1, nullptr);
	asINT64 v = 0;
	ASSERT_TRUE(d1->Get("k", v));
	EXPECT_EQ(v, 22);
}

TEST(ContainerDeep, AnyHoldsArrayHandle)
{
	// any storing an array<int>@ handle (a template ref type payload).
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"any@ a;\n"
			"void setup(){\n"
			"  any x;\n"
			"  array<int> arr = {5, 6, 7};\n"
			"  x.store(@arr);\n"
			"  @a = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto any = GlobalObject<CScriptAny>(engine.get(), "a");
	ASSERT_NE(any, nullptr);
	int arrHandleTypeId = engine->GetTypeIdByDecl("array<int>") | asTYPEID_OBJHANDLE;
	CScriptArray * arr = nullptr;
	ASSERT_TRUE(any->Retrieve(&arr, arrHandleTypeId)) << "any lost its array handle";
	ASSERT_NE(arr, nullptr);
	ASSERT_EQ(arr->GetSize(), 3u);
	EXPECT_EQ(*static_cast<int*>(arr->At(2)), 7);
	if(arr) arr->Release();
}

TEST(ContainerDeep, ScriptClassWithArrayMember)
{
	// A container held as a MEMBER of a script class (reached through the object
	// graph, not as a top-level global) — a distinct traversal path.
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Bag { array<int> items; }\n"
			"Bag@ g;\n"
			"void setup(){\n"
			"  Bag b;\n"
			"  b.items.insertLast(1);\n"
			"  b.items.insertLast(2);\n"
			"  b.items.insertLast(3);\n"
			"  @g = b;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto bag = GlobalObject<asIScriptObject>(engine.get(), "g");
	ASSERT_NE(bag, nullptr);
	// array<int> is a reference type, so GetAddressOfProperty already dereferences
	// the member to the array object (as_scriptobject.cpp:803-805): a single cast
	// yields it. A second `*` (the handle idiom) would read into the object.
	auto items = static_cast<CScriptArray*>(bag->GetAddressOfProperty(0));
	ASSERT_NE(items, nullptr);
	ASSERT_EQ(items->GetSize(), 3u);
	EXPECT_EQ(*static_cast<int*>(items->At(2)), 3);
}

TEST(ContainerDeep, ScriptClassWithStringArrayMember)
{
	// array<string> as a script-class member — nested heap leaves reached through
	// the object graph via a member slot.
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Bag { array<string> tags; }\n"
			"Bag@ g;\n"
			"void setup(){\n"
			"  Bag b;\n"
			"  b.tags.insertLast('x');\n"
			"  b.tags.insertLast('yy');\n"
			"  @g = b;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto bag = GlobalObject<asIScriptObject>(engine.get(), "g");
	ASSERT_NE(bag, nullptr);
	// Non-handle ref member: GetAddressOfProperty already dereferences to the array.
	auto tags = static_cast<CScriptArray*>(bag->GetAddressOfProperty(0));
	ASSERT_NE(tags, nullptr);
	ASSERT_EQ(tags->GetSize(), 2u);
	EXPECT_EQ(*static_cast<std::string*>(tags->At(1)), "yy");
}

TEST(ContainerDeep, ScriptClassWithDictionaryMember)
{
	// dictionary as a script-class member — a concrete (non-template) container in
	// a member slot, to contrast with the array-member path.
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Bag { dictionary d; }\n"
			"Bag@ g;\n"
			"void setup(){\n"
			"  Bag b;\n"
			"  b.d.set('n', 42);\n"
			"  @g = b;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto bag = GlobalObject<asIScriptObject>(engine.get(), "g");
	ASSERT_NE(bag, nullptr);
	// dictionary is a reference type: GetAddressOfProperty already dereferences the
	// member to the dictionary object, so a single cast yields it.
	auto d = static_cast<CScriptDictionary*>(bag->GetAddressOfProperty(0));
	ASSERT_NE(d, nullptr);
	asINT64 v = 0;
	ASSERT_TRUE(d->Get("n", v));
	EXPECT_EQ(v, 42);
}

TEST(ContainerDeep, DictionaryHandleAliasesGlobal)
{
	std::vector<char> image;
	{
		TestEngine engine;
		BuildAndSetup(engine,
			"class Node { int v; }\n"
			"Node@ g;\n"
			"dictionary@ d;\n"
			"void setup(){\n"
			"  Node@ n = Node(); n.v = 4;\n"
			"  @g = n;\n"
			"  dictionary x;\n"
			"  x.set('node', @n);\n"
			"  @d = x;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto dict = GlobalObject<CScriptDictionary>(engine.get(), "d");
	ASSERT_NE(dict, nullptr);

	asIScriptModule * mod = engine->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asITypeInfo * nodeType = mod->GetTypeInfoByName("Node");
	ASSERT_NE(nodeType, nullptr);
	int nodeHandleTypeId = nodeType->GetTypeId() | asTYPEID_OBJHANDLE;

	asIScriptObject * fromDict = nullptr;
	ASSERT_TRUE(dict->Get("node", &fromDict, nodeHandleTypeId))
		<< "dictionary lost its handle value";
	ASSERT_NE(fromDict, nullptr);
	EXPECT_EQ(*static_cast<int*>(fromDict->GetAddressOfProperty(0)), 4);

	auto g = GlobalObject<asIScriptObject>(engine.get(), "g");
	EXPECT_EQ(fromDict, g) << "dictionary value must alias the same restored object as global g";
	if(fromDict) fromDict->Release();  // Get() on a handle slot AddRef'd
}
