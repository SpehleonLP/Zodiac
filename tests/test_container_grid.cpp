// Part A acceptance: grid<int>@ round-trips through Zodiac. A 2x3 grid of POD
// ints exercises the grid save/load glue (dimensions + element buffer).
//
// Mirrors test_container_array.cpp: the grid global is an `@` handle assigned in
// setup(); save into a zCMemoryFile, reload into a fresh engine, read back.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include "add_on/scriptgrid/scriptgrid.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
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

TEST(ContainerGrid, IntValues)
{
	std::vector<char> image;
	{
		TestEngine engine;
		// 2 columns (width) x 3 rows (height). Fill g[x,y] = 10*x + y.
		BuildAndSetup(engine,
			"grid<int>@ g;\n"
			"void setup(){\n"
			"  grid<int> gr(2, 3);\n"
			"  for(uint x = 0; x < 2; ++x)\n"
			"    for(uint y = 0; y < 3; ++y)\n"
			"      gr[x, y] = int(10*x + y);\n"
			"  @g = gr;\n"
			"}\n");
		image = SaveEngine(engine.get());
	}
	ASSERT_FALSE(image.empty());

	TestEngine engine;
	LoadEngine(engine.get(), image);
	auto grid = GlobalObject<CScriptGrid>(engine.get(), "g");
	ASSERT_NE(grid, nullptr);
	ASSERT_EQ(grid->GetWidth(), 2u);
	ASSERT_EQ(grid->GetHeight(), 3u);

	for(asUINT x = 0; x < 2; ++x)
		for(asUINT y = 0; y < 3; ++y)
			EXPECT_EQ(*static_cast<int*>(grid->At(x, y)), int(10*x + y))
				<< "at (" << x << "," << y << ")";
}
