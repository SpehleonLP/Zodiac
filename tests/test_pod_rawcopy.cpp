// Coverage for the raw-copy POD path: an application POD value type registered
// with AngelScript but NOT given Zodiac save/load glue. SortTypeList auto-creates
// an entry with onSave == onLoad == nullptr (z_zodiac.cpp:323-333); the writer
// then raw-copies its bytes and the reader restores it with a plain
// memcpy(dst, src, entry->byteLength) at z_zodiacreader.cpp:1183 (the else branch
// of the enum/funcdef/onLoad dispatch). No addon registers a glue-less POD, so
// this path had no round-trip test. It is the sibling of the enum inline path at
// :1166 — both copy at the live registered width.
//
// (The hardening audit flagged these two memcpys as a potential width-drift
// over-read. They are NOT reachable as an out-of-bounds read: the copy length is
// the AS-registered GetSize()/byteLength — a registration constant, not a
// file-controlled field — and Verify() already bounds the property extent against
// the entry payload. This test therefore pins the correct behavior rather than a
// defect: the glue-less POD round-trips byte-for-byte.)
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// 12-byte POD (wider than a handle/primitive) so a live-width copy that ignored
// the stored extent would be visibly wrong.
struct RawVec { int x; int y; int z; };

void RegisterRawVec(asIScriptEngine * e)
{
	ASSERT_GE(e->RegisterObjectType("RawVec", sizeof(RawVec),
		asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<RawVec>()), 0);
	ASSERT_GE(e->RegisterObjectProperty("RawVec", "int x", offsetof(RawVec, x)), 0);
	ASSERT_GE(e->RegisterObjectProperty("RawVec", "int y", offsetof(RawVec, y)), 0);
	ASSERT_GE(e->RegisterObjectProperty("RawVec", "int z", offsetof(RawVec, z)), 0);
}

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	// Deliberately NO RegisterValueType<RawVec>() — RawVec has no Zodiac glue, so
	// it must be handled by the auto POD raw-copy path.
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}

RawVec MemberRawVec(asIScriptObject * o, const char * n)
{
	for(asUINT i = 0; i < o->GetPropertyCount(); ++i)
		if(o->GetPropertyName(i) && std::strcmp(o->GetPropertyName(i), n) == 0)
		{ RawVec v{}; std::memcpy(&v, o->GetAddressOfProperty(i), sizeof(v)); return v; }
	return RawVec{-1,-1,-1};
}
}

TEST(PodRawCopy, GlueLessPodMemberAndGlobalRoundTrip)
{
	const char * src =
		"class Holder { RawVec v; }\n"
		"Holder@ h;\n"
		"RawVec gv;\n"
		"void setup() { @h = Holder(); h.v.x = 10; h.v.y = 20; h.v.z = 30;"
		"               gv.x = 40; gv.y = 50; gv.z = 60; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		RegisterRawVec(e.get());
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", src), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
		ASSERT_NE(setup, nullptr);
		asIScriptContext * ctx = e->CreateContext();
		ASSERT_GE(ctx->Prepare(setup), 0);
		ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
		ctx->Release();

		auto z = MakeZodiac(e.get());
		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}

	TestEngine e;
	RegisterRawVec(e.get());
	auto z = MakeZodiac(e.get());
	zCMemoryFile file(image);
	ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();

	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);

	// Global RawVec restored via the top-level raw-copy path.
	int gidx = mod->GetGlobalVarIndexByName("gv");
	ASSERT_GE(gidx, 0);
	RawVec gv{};
	std::memcpy(&gv, mod->GetAddressOfGlobalVar(gidx), sizeof(gv));
	EXPECT_EQ(gv.x, 40); EXPECT_EQ(gv.y, 50); EXPECT_EQ(gv.z, 60);

	// Member RawVec restored via RestoreScriptObject :1183 (memcpy entry->byteLength).
	int hidx = mod->GetGlobalVarIndexByName("h");
	ASSERT_GE(hidx, 0);
	asIScriptObject * h = nullptr;
	std::memcpy(&h, mod->GetAddressOfGlobalVar(hidx), sizeof(h));
	ASSERT_NE(h, nullptr);
	RawVec v = MemberRawVec(h, "v");
	EXPECT_EQ(v.x, 10); EXPECT_EQ(v.y, 20); EXPECT_EQ(v.z, 30)
		<< "glue-less POD member must round-trip byte-for-byte";
}
