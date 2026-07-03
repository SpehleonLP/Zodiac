// zIZodiacReader::GetCurrentOwner — during a type-callback onLoad, the reader must
// expose the enclosing object whose member is being restored (Zodiac's recorded
// `owner` for the current entry). A member entry sees its owning script object; a
// module-global entry (no owner) sees nullptr. This is the API the engine's Arc-1
// hot-reload GameObject load entry uses to attach a restored component to its owner.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// captures (payload, owner-at-load) for every Marker restored, in load order.
std::vector<std::pair<int, void*>> g_ownerCaptures;

struct Marker { int payload = 0; };

void SaveMarker(zIZodiacWriter * w, Marker const* v, int&)
{ w->GetFile()->Write(&v->payload); }
// The load half records what GetCurrentOwner() reports at the moment this member
// is restored — that is the whole point of the new API.
void LoadMarker(zIZodiacReader * r, Marker * v, int&, bool)
{
	r->GetFile()->Read(&v->payload);
	g_ownerCaptures.emplace_back(v->payload, r->GetCurrentOwner());
}

void MarkerConstruct(void * mem) { new(mem) Marker(); }
void MarkerDestruct(void * mem)  { static_cast<Marker*>(mem)->~Marker(); }

void RegisterMarker(asIScriptEngine * e)
{
	ASSERT_GE(e->RegisterObjectType("Marker", sizeof(Marker),
		asOBJ_VALUE | asOBJ_APP_CLASS_CD), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Marker", asBEHAVE_CONSTRUCT, "void f()",
		asFUNCTION(MarkerConstruct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Marker", asBEHAVE_DESTRUCT, "void f()",
		asFUNCTION(MarkerDestruct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(e->RegisterObjectProperty("Marker", "int payload", offsetof(Marker, payload)), 0);
}

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->RegisterValueType<Marker>(zIZodiac::GetTypeId<Marker>(), sizeof(Marker), "Marker",
		SaveMarker, LoadMarker, nullptr);
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}

asIScriptObject * ReadHandle(void * a)
{ asIScriptObject * p = nullptr; if(a) std::memcpy(&p, a, sizeof(p)); return p; }

void * OwnerForPayload(int payload)
{
	for(auto & c : g_ownerCaptures) if(c.first == payload) return c.second;
	return reinterpret_cast<void*>(-1);   // sentinel: not captured at all
}
}

// A Marker that is a MEMBER of a script class sees its owning script object as the
// current owner; a Marker that is a MODULE GLOBAL (no enclosing owner) sees nullptr.
TEST(ReaderCurrentOwner, MemberSeesOwnerGlobalSeesNull)
{
	const char * src =
		"class Holder { Marker m; }\n"
		"Holder@ h;\n"
		"Marker g;\n"
		"void setup() { @h = Holder(); h.m.payload = 42; g.payload = 99; }\n";

	std::vector<char> image;
	{
		TestEngine e;
		RegisterMarker(e.get());
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", src), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptFunction * s = mod->GetFunctionByDecl("void setup()");
		asIScriptContext * ctx = e->CreateContext();
		ctx->Prepare(s); ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED); ctx->Release();
		auto z = MakeZodiac(e.get());
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();
	}
	ASSERT_FALSE(image.empty());

	g_ownerCaptures.clear();

	TestEngine e;
	RegisterMarker(e.get());
	auto z = MakeZodiac(e.get());
	zCMemoryFile f(image);
	ASSERT_EQ(z->LoadFromFile(&f), zE_Success) << z->GetErrorString();

	asIScriptModule * mod = e->GetModule("m", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(mod, nullptr);
	asIScriptObject * h = ReadHandle(mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("h")));
	ASSERT_NE(h, nullptr);

	// both markers were restored (guards against the fixture silently not exercising the path)
	ASSERT_NE(OwnerForPayload(42), reinterpret_cast<void*>(-1)) << "member Marker never restored";
	ASSERT_NE(OwnerForPayload(99), reinterpret_cast<void*>(-1)) << "global Marker never restored";

	// the member's owner is exactly the enclosing Holder instance
	EXPECT_EQ(OwnerForPayload(42), static_cast<void*>(h)) << "member owner != enclosing Holder";
	// the module-global marker has no enclosing owner
	EXPECT_EQ(OwnerForPayload(99), nullptr) << "module-global marker should have no owner";
}
