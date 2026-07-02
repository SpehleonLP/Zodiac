// Defect #9 — suspended-context by-reference OBJECT/HANDLE parameter leaks a
//             reference (src/z_zodiaccontext.cpp:311 / :365).
//
// When a suspended context is restored, an in-scope local is normally re-loaded
// into its stack slot. A by-reference parameter (`Obj &inout` / `Obj@ &inout`,
// unsafe references) does NOT own storage: its slot holds a POINTER that aliases
// the caller's variable. z_zodiaccontext.cpp handles this correctly for PRIMITIVE
// and VALUE ref params (the `derefBranch` defers them and re-points the slot at
// the restored aliased storage, taking NO reference). But an object/handle ref
// param falls through to the script-/app-object handle branch, which does
//     reader->LoadScriptObject(dst, address, asTypeId | asTYPEID_OBJHANDLE, false);
// with isWeak = false — depositing an OWNED (AddRef'd) reference into a slot the
// callee frame will NOT release (AngelScript skips &-parameter slots on cleanup).
// Net effect: after save + load + resume + return, the aliased object's refcount
// is one too high — a leak; its destructor never runs.
//
// Reproduction: a ref-counted app type `Tracked` with a live-instance counter,
// passed by `Tracked@ &inout` to a callee that suspends (yield()) mid-call. We
// serialize the suspended context, restore it into a fresh engine, resume to
// completion, drop the owning handle, and assert the Tracked instance count
// returns to zero.
//
// RED (pre-fix): the &inout slot's extra owned reference is never released, so the
// restored Tracked is never destroyed — Tracked::s_live stays 1 after the whole
// round-trip completes. (Under ASan/LSan it would also surface as a leak at
// shutdown.)
// GREEN (post-fix): the object ref param is treated as an alias (no owned ref), so
// dropping the caller handle destroys the object and s_live returns to 0.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// A minimal reference-counted application type whose live-instance count is
// externally observable. A leaked reference keeps refCount > 0 forever, so the
// destructor never runs and s_live never returns to 0.
struct Tracked
{
	int refCount;
	int value;
	static int s_live;
	Tracked() : refCount(1), value(0) { ++s_live; }
	void AddRef()  { ++refCount; }
	void Release() { if(--refCount == 0) { --s_live; delete this; } }
};
int Tracked::s_live = 0;

Tracked * Tracked_Factory() { return new Tracked(); }

void RegisterTracked(asIScriptEngine * e)
{
	ASSERT_GE(e->RegisterObjectType("Tracked", 0, asOBJ_REF), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Tracked", asBEHAVE_FACTORY, "Tracked@ f()",
		asFUNCTION(Tracked_Factory), asCALL_CDECL), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Tracked", asBEHAVE_ADDREF, "void f()",
		asMETHOD(Tracked, AddRef), asCALL_THISCALL), 0);
	ASSERT_GE(e->RegisterObjectBehaviour("Tracked", asBEHAVE_RELEASE, "void f()",
		asMETHOD(Tracked, Release), asCALL_THISCALL), 0);
	ASSERT_GE(e->RegisterObjectProperty("Tracked", "int value", offsetof(Tracked, value)), 0);
}

// Zodiac save/load glue for the Tracked ref type.
void SaveTracked(zIZodiacWriter * w, Tracked const* t, int&)
{ w->GetFile()->Write(&t->value); }
void LoadTracked(zIZodiacReader * r, Tracked ** t, int&, bool isHandle)
{
	if(isHandle)
		*t = Tracked_Factory();          // born with refCount == 1
	r->GetFile()->Read(&(*t)->value);
}

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->RegisterRefType<Tracked>(zIZodiac::GetTypeId<Tracked>(), sizeof(Tracked), "Tracked",
		SaveTracked, LoadTracked, nullptr);
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}

void Yield() { asIScriptContext * c = asGetActiveContext(); if(c) c->Suspend(); }
void Configure(asIScriptEngine * e)
{
	e->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 1);
	RegisterTracked(e);
	ASSERT_GE(e->RegisterGlobalFunction("void yield()", asFUNCTION(Yield), asCALL_CDECL), 0);
}

struct Payload { asIScriptContext * ctx = nullptr; uint32_t id = 0; };
void WriteCtx(zIZodiacWriter * w, void * p)
{ auto pl = static_cast<Payload*>(p); pl->id = w->SaveContext(pl->ctx); w->GetFile()->Write(&pl->id); }
void ReadCtx(zIZodiacReader * r, void * p)
{ auto pl = static_cast<Payload*>(p); r->GetFile()->Read(&pl->id); pl->ctx = r->LoadContext(pl->id); }

// The callee suspends while it holds a by-reference handle parameter aliasing the
// caller's `t`. `run()` drops its own handle on return, so a correct restore
// leaves zero live Tracked instances.
const char * kScript =
	"void inner(Tracked@ &inout x) { yield(); x.value += 1; }\n"
	"int run() {\n"
	"    Tracked@ t = Tracked();\n"
	"    t.value = 7;\n"
	"    inner(t);\n"
	"    return t.value;\n"
	"}\n";

// Runs entirely in a forked death-test child. _exit(0) ONLY if the context resumes
// cleanly, returns the aliased-write result (8), and leaves zero live Tracked
// instances. Any signal (pre-fix SEGV) or non-zero exit is a RED.
[[noreturn]] void LoadResumeAndCheck(const std::vector<char> & image)
{
	Tracked::s_live = 0;
	TestEngine e; Configure(e.get());
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("m", kScript);
	mod->Build();

	asIScriptContext * ctx = nullptr;
	{
		auto z = MakeZodiac(e.get());
		Payload pl; z->SetUserData(&pl); z->SetReadSaveDataCallback(ReadCtx);
		zCMemoryFile f(image);
		if(z->LoadFromFile(&f) != zE_Success) _exit(3);
		ctx = pl.ctx;
	}
	if(ctx == nullptr) _exit(4);

	int guard = 0;
	int st = ctx->GetState();
	while((st == asEXECUTION_SUSPENDED || st == asEXECUTION_PREPARED) && guard++ < 100)
		st = ctx->Execute();                            // <-- pre-fix SEGV here
	if(st != asEXECUTION_FINISHED) _exit(5);
	if(ctx->GetReturnDWord() != 8u) _exit(6);           // aliased &inout write lost
	e->ReturnContext(ctx);

	e->GarbageCollect(asGC_FULL_CYCLE);

	// run() dropped t on return. A leaked &inout reference keeps it alive.
	_exit(Tracked::s_live == 0 ? 0 : 7);
}
}

// Sanity: a full local round-trip (no save/restore) leaves no live Tracked.
TEST(ContextRefParam, BaselineNoLeakWithoutSerialization)
{
	Tracked::s_live = 0;
	{
		TestEngine e; Configure(e.get());
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);
		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		// Run straight through (yield suspends; resume to completion).
		int guard = 0, st = ctx->Execute();
		while(st == asEXECUTION_SUSPENDED && guard++ < 100) st = ctx->Execute();
		ASSERT_EQ(st, asEXECUTION_FINISHED);
		EXPECT_EQ(ctx->GetReturnDWord(), 8u);
		e->ReturnContext(ctx);
	}
	EXPECT_EQ(Tracked::s_live, 0) << "baseline (no serialization) already leaks";
}

TEST(ContextRefParam, InoutHandleParamDoesNotLeakAcrossRoundTrip)
{
	Tracked::s_live = 0;

	// --- Save side: suspend inside inner() while x aliases run()'s t. ---
	std::vector<char> image;
	{
		TestEngine e; Configure(e.get());
		asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("m", kScript), 0);
		ASSERT_GE(mod->Build(), 0);

		asIScriptContext * ctx = e->RequestContext();
		ctx->Prepare(mod->GetFunctionByDecl("int run()"));
		ASSERT_EQ(ctx->Execute(), asEXECUTION_SUSPENDED)
			<< "run() should suspend inside inner()";

		auto z = MakeZodiac(e.get());
		Payload pl; pl.ctx = ctx;
		z->SetUserData(&pl); z->SetWriteSaveDataCallback(WriteCtx);
		zCMemoryFile f;
		ASSERT_EQ(z->SaveToFile(&f), zE_Success) << z->GetErrorString();
		image = f.bytes();

		e->ReturnContext(ctx);
	}
	ASSERT_FALSE(image.empty());
	// The save engine is gone; every Tracked it held is released.
	ASSERT_EQ(Tracked::s_live, 0) << "save-side Tracked not released";

	// --- Load side: restore, resume, drop handles — in a forked child. ---
	// Pre-fix the restored &inout handle slot holds an owned object pointer where
	// the VM expects an alias pointer, so resume SEGVs (or, if it survives, leaks a
	// reference). The child exits 0 only on a clean resume AND zero live Tracked;
	// any signal or non-zero exit fails ExitedWithCode(0) → RED.
	EXPECT_EXIT(LoadResumeAndCheck(image), ::testing::ExitedWithCode(0), ".*");
}
