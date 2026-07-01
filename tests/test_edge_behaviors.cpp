// Red-hunt: behavioral edges that "just works" infrastructure should handle
// gracefully rather than corrupt or crash.
//   * DoubleLoad        — loading twice into the same zodiac should be rejected
//                         cleanly (zE_DoubleLoad), not silently double-apply.
//   * PrivateMembers    — private/protected script members must round-trip.
//   * SaveWithoutBytecode — SAVE_BYTECODE off + a recompiled module on load:
//                         object state should still restore.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine, bool bytecode = true)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, bytecode);
	return zodiac;
}
asIScriptObject * ReadHandle(void * a)
{ asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p; }
int PropInt(asIScriptObject * o, const char * n)
{
	if(!o) return -0xBEEF;
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::string(o->GetPropertyName(i)) == n)
		{ int v=-1; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return -0xBEEF;
}
asIScriptModule * BuildAndSetup(asIScriptEngine * e, const char * src)
{
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m", src), 0);
	EXPECT_GE(mod->Build(), 0);
	if(asIScriptFunction * s = mod->GetFunctionByDecl("void setup()"))
	{
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
	}
	return mod;
}

const char * kBox =
	"class Box { int v; }\n Box@ box;\n void setup() { @box = Box(); box.v = 77; }\n";
}

TEST(EdgeBehaviors, DoubleLoadRejected)
{
	std::vector<char> image;
	{ TestEngine e; BuildAndSetup(e.get(), kBox);
	  auto z = MakeZodiac(e.get()); zCMemoryFile f;
	  ASSERT_EQ(z->SaveToFile(&f), zE_Success); image = f.bytes(); }
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto z = MakeZodiac(e.get());
	zCMemoryFile f1(image);
	ASSERT_EQ(z->LoadFromFile(&f1), zE_Success) << z->GetErrorString();
	// Second load into the same zodiac: must be a clean rejection, not UB.
	zCMemoryFile f2(image);
	Code rc = z->LoadFromFile(&f2);
	EXPECT_EQ(rc, zE_DoubleLoad)
		<< "second LoadFromFile returned " << (int)rc << " instead of zE_DoubleLoad";
}

TEST(EdgeBehaviors, PrivateMembersRoundTrip)
{
	const char * src =
		"class C { private int secret; int pub; void set() { secret = 9; pub = 3; } int get() { return secret; } }\n"
		"C@ c;\n"
		"void setup() { @c = C(); c.set(); }\n";
	std::vector<char> image;
	{ TestEngine e; BuildAndSetup(e.get(), src);
	  auto z = MakeZodiac(e.get()); zCMemoryFile f;
	  ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString(); image = f.bytes(); }
	ASSERT_FALSE(image.empty());

	TestEngine e;
	auto z = MakeZodiac(e.get());
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	asIScriptObject * c = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("c")));
	ASSERT_NE(c, nullptr);
	EXPECT_EQ(PropInt(c, "secret"), 9) << "private member not restored";
	EXPECT_EQ(PropInt(c, "pub"), 3)    << "public member not restored";
}

TEST(EdgeBehaviors, SaveWithoutBytecode)
{
	std::vector<char> image;
	{ TestEngine e; BuildAndSetup(e.get(), kBox);
	  auto z = MakeZodiac(e.get(), /*bytecode*/false); zCMemoryFile f;
	  ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString(); image = f.bytes(); }
	ASSERT_FALSE(image.empty());

	// Load side recompiles the identical module first (no bytecode in the image).
	TestEngine e;
	BuildAndSetup(e.get(), kBox); // rebuilds module 'm' (setup runs, v=77 again)
	auto z = MakeZodiac(e.get(), false);
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();
	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	asIScriptObject * box = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("box")));
	ASSERT_NE(box, nullptr) << "box null after bytecode-less restore";
	EXPECT_EQ(PropInt(box, "v"), 77);
}
