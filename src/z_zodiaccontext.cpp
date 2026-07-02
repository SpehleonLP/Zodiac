#include "z_zodiaccontext.h"
#ifdef HAVE_ZODIAC
#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

struct CtxCallState
{
	uint32_t stackFramePointer;
	uint32_t currentFunction;
	uint32_t programPointer;
	uint32_t stackPointer;
	uint32_t stackIndex;
	uint32_t objectType;
	uint32_t objectId;


	void PushFunction(Zodiac::zIZodiacReader * reader, asIScriptContext * ctx)
	{
		void * scriptObject = nullptr;
		auto _currentFunction = reader->LoadFunction(currentFunction);

		// The frame's `this` slot is a handle-to-object: `scriptObject` is a void*
		// that receives the object pointer, so it must be loaded with the OBJHANDLE
		// flag (and nulled first) exactly like the global/member/local handle paths.
		// The prior call omitted the flag and left `scriptObject` uninitialised, so an
		// object already materialised via an aliasing global reached the alias branch
		// of LoadScriptObjectImpl through the wrong (by-value) type comparison — adding
		// a reference the frame does not own. On resume the VM's `this` release then
		// drove the shared object's refcount below its live count (use-after-free,
		// tripping asCAtomic). isWeak=false: PushFunction stores the pointer raw and
		// the frame OWNS one reference (released when the frame unwinds), so the reader
		// must deposit exactly one owned reference here.
		if(objectId)
			reader->LoadScriptObject(&scriptObject, objectId,
				reader->LoadTypeId(objectType) | asTYPEID_OBJHANDLE, false);
		// trunk PushFunction is 2-arg (func, object); the 'this' typeId is derived
		// from the function's object type, so the old typeId argument is gone.
		ctx->PushFunction(_currentFunction, scriptObject);

		SetToContext(reader, ctx, 0, _currentFunction);

		if(_currentFunction) _currentFunction->Release();
	}

	int GetFromContext(Zodiac::zIZodiacWriter * writer, asIScriptContext * ctx, int i)
	{
		asIScriptFunction * _currentFunction{};
		int r = ctx->GetCallStateRegisters(i, &stackFramePointer, &_currentFunction, &programPointer, &stackPointer, &stackIndex);

		currentFunction	= writer->SaveFunction(_currentFunction);
		objectType = writer->SaveTypeId(ctx->GetThisTypeId(i));
		objectId   = writer->SaveScriptObject(ctx->GetThisPointer(i), ctx->GetThisTypeId(i));

		return r;
	}

	int SetToContext(Zodiac::zIZodiacReader * reader, asIScriptContext * ctx, int i, asIScriptFunction * _currentFunction = nullptr)
	{
		if(!_currentFunction) reader->LoadFunction(currentFunction);
		return ctx->SetCallStateRegisters(i, stackFramePointer, _currentFunction, programPointer, stackPointer, stackIndex);
	}
};


struct CtxStackState
{
	uint32_t callingSystemFunction;
	uint32_t initialFunction;
	uint32_t originalStackPointer;
	uint32_t argumentsSize;

//call stack uses intptr_t so its split between tmp[5]/tmp[6] for x86 machines i suppose
	uint64_t valueRegister;
	uint32_t objectRegister;
	uint32_t objectType;

	int GetFromContext(Zodiac::zIZodiacWriter * writer, asIScriptContext * ctx, int i)
	{
		asIScriptFunction * _callingSystemFunction{};
		asIScriptFunction * _initialFunction{};
		void * _objectRegister{};
		asITypeInfo * _objectType{};

		int r = ctx->GetStateRegisters(i, &_callingSystemFunction, &_initialFunction, &originalStackPointer, &argumentsSize, &valueRegister, &_objectRegister, &_objectType);

		callingSystemFunction	= writer->SaveFunction(_callingSystemFunction);
		initialFunction			= writer->SaveFunction(_initialFunction);
		objectType				= writer->SaveTypeInfo(_objectType);
		objectRegister			= writer->SaveScriptObject(_objectRegister, _objectType, nullptr);

		return r;
	}

	int SetToContext(Zodiac::zIZodiacReader * reader, asIScriptContext * ctx, int i)
	{
		asIScriptFunction * _callingSystemFunction{};
		asIScriptFunction * _initialFunction{};
		void * _objectRegister{};
		asITypeInfo * _objectType{};

		_callingSystemFunction = reader->LoadFunction(callingSystemFunction);
		_objectType	  = reader->LoadTypeInfo(objectType, true);

		// At stackLevel 0 the context ALREADY owns m_initialFunction: PushFunction ->
		// Prepare set it (AddRef'd) and Unprepare will Release it. SetStateRegisters(0)
		// stores initialFunction RAW (no AddRef, no release of the prior value), so
		// handing it a freshly-loaded, possibly-different function corrupts the count:
		// for a virtual method call Prepare stored the resolved REAL function while the
		// serialized initialFunction is the VIRTUAL stub — the clobber leaks Prepare's
		// reference and Unprepare then over-releases the raw-stored stub (use-after-free
		// tripping asCAtomic at engine teardown). Reuse the already-prepared function so
		// the register write is reference-neutral; only nested (level>0) states, whose
		// initialFunction lives borrowed in the call-stack array, load+release their own.
		//
		// m_initialFunction is the OUTERMOST frame's function (the first one Prepared),
		// not the innermost current function — for a deep call stack GetFunction(0) is
		// the wrong (innermost) frame, so read the top of the stack.
		if(i == 0)
			_initialFunction = ctx->GetFunction(ctx->GetCallstackSize() - 1);
		else
			_initialFunction = reader->LoadFunction(initialFunction);

		// Restore the object register into the LOCAL void* slot that is actually
		// handed to SetStateRegisters. The previous code loaded into the 4-byte
		// `objectRegister` MEMBER -- an 8-byte pointer write that overflowed into
		// the adjacent `objectType` field -- and then passed the still-null local
		// `_objectRegister`, silently dropping the register. `objectRegister` is an
		// object-address index (see GetFromContext's SaveScriptObject); index 0 is
		// a null register. NOTE: SetStateRegisters stores this pointer raw and the
		// VM assumes ownership of one reference, so load it as an owning handle
		// (isWeak=false, the reader's default) -- symmetric with the call/global
		// paths. This branch is currently unexercised (no test suspends with a live
		// object register); the fix restores correct mechanics without changing the
		// tested null-register behaviour.
		if(objectRegister != 0 && _objectType)
			reader->LoadScriptObject(&_objectRegister, objectRegister,
				_objectType->GetTypeId() | asTYPEID_OBJHANDLE);

		int r = ctx->SetStateRegisters(i, _callingSystemFunction, _initialFunction, originalStackPointer, argumentsSize, valueRegister, _objectRegister, _objectType);

		// SetStateRegisters stores these function pointers raw (it does NOT AddRef);
		// the context borrows them, kept alive by their owning module. LoadFunction
		// handed us OWNED references, so release ours here — matching the
		// CtxCallState::PushFunction load/use/release pattern — or they leak. The
		// level-0 initialFunction is the exception: it is not a fresh LoadFunction ref
		// (it is the context's own prepared function, see above), so it is not released.
		if(i != 0 && _initialFunction) _initialFunction->Release();
		if(_callingSystemFunction)     _callingSystemFunction->Release();

		return r;
	}
};

struct StackFrame
{
	uint16_t varCount;
	uint16_t isCallState;

	union
	{
		CtxCallState callState;
		CtxStackState state;
	};
};

struct StackVar
{
	uint16_t stackLevel;
	uint16_t varId;
	uint32_t typeId;
	uint32_t object;
};

void Zodiac::ZodiacSave(zIZodiacWriter* writer, asIScriptContext const* _ctx, int&)
{
	int callStackSize = -1;
	uint32_t status{};
	auto file = writer->GetFile();
	auto ctx = const_cast<asIScriptContext*>(_ctx);

	if(ctx)
	{
		callStackSize = ctx->GetCallstackSize();
		status   = ctx->GetState();
	}

	if(!(status == asEXECUTION_PREPARED || status == asEXECUTION_SUSPENDED))
	{
		callStackSize = 0;
	}

	file->Write(&callStackSize);
	file->Write(&status);

	if(callStackSize <= 0)
		return;

	// A PREPARED-but-never-executed context has no live call frame to serialize —
	// GetCallstackSize() reports 1 only because the initial function is set. The
	// deserialization API (StartDeserialization/PushFunction/FinishDeserialization)
	// can only ever finish in asEXECUTION_SUSPENDED, so resurrecting a frame here
	// would silently downgrade the restored state. Persist just the initial function
	// and re-Prepare on load, which faithfully reproduces asEXECUTION_PREPARED.
	if(status == asEXECUTION_PREPARED)
	{
		asIScriptFunction * initialFn = ctx->GetFunction(0);
		uint32_t fn = writer->SaveFunction(initialFn);
		file->Write(&fn);

		// The arguments already pushed onto the prepared frame (Prepare() + SetArg*)
		// must be persisted too, or Execute() after the round-trip runs with zeroed
		// slots. GetAddressOfVar is unusable here (programPointer == 0 in PREPARED
		// state), so reach the pushed slots through GetAddressOfArg — valid only in
		// PREPARED state — and serialize each through the SAME SaveScriptObject/StackVar
		// machinery the suspended-frame variable loop uses. The load side re-Prepare()s
		// and restores these slots via GetAddressOfArg.
		uint32_t argCount = initialFn ? initialFn->GetParamCount() : 0;
		file->Write(&argCount);

		StackVar arg;
		arg.stackLevel = 0;
		for(uint32_t a = 0; a < argCount; ++a)
		{
			int argTypeId = 0;
			initialFn->GetParam(a, &argTypeId);

			void * argAddr = ctx->GetAddressOfArg(a);

			arg.varId  = a;
			arg.typeId = argAddr ? writer->SaveTypeId(argTypeId) : 0;
			arg.object = argAddr ? writer->SaveScriptObject(argAddr, argTypeId) : 0;
			file->Write(&arg);
		}
		return;
	}

//write stack frames
	StackFrame sf;
	for(int i = ctx->GetCallstackSize()-1; i >= 0; --i)
	{
		sf.varCount		= ctx->GetVarCount();

		if(sf.callState.GetFromContext(writer, ctx, i) >= 0)

			sf.isCallState = true;
		else if(sf.state.GetFromContext(writer, ctx, i) >= 0)
			sf.isCallState = false;

		file->Write(&sf);
	}
//write current registers
	sf.varCount = 0;
	sf.isCallState = 2;
	sf.state.GetFromContext(writer, ctx, 0);
	file->Write(&sf);

//write variable contents
	StackVar var;
	for(uint32_t i = 0; i < ctx->GetCallstackSize(); ++i)
	{
		auto N = ctx->GetVarCount(i);

		for(uint16_t j = 0; j < N; ++j)
		{
			if(!ctx->IsVarInScope(j, i))
				continue;

			var.stackLevel = i;
			var.varId = j;

			// A variable can be reported in-scope yet have no live storage: the
			// compiler emits anonymous temporaries (e.g. the list-buffer local of an
			// initializer-list expression — an asOBJ_LIST_PATTERN app type with no
			// registered save handler) whose slot is already dead at the suspend
			// point. GetAddressOfVar returns null for these; there is no value to
			// preserve and SaveTypeId would reject the list-pattern typeId. Emit a
			// placeholder record (typeId 0 = "nothing to restore") so the per-variable
			// record stream stays in lockstep with the load-side in-scope walk, which
			// cannot itself re-derive the null-storage predicate during deserialization.
			if(ctx->GetAddressOfVar(j, i) == nullptr)
			{
				var.typeId = 0;
				var.object = 0;
				file->Write(&var);
				continue;
			}

			// GetVarTypeId is deprecated; the live typeId comes from GetVar's out-param.
			int varTypeId = 0;
			ctx->GetVar(j, i, nullptr, &varTypeId);
//write to confirm on reading
			var.typeId = writer->SaveTypeId(varTypeId);
			var.object = writer->SaveScriptObject(ctx->GetAddressOfVar(j, i), varTypeId);

			file->Write(&var);
		}
	}
}

// Restore one on-stack variable of a suspended frame. Trunk removed
// SetVarContents(); the replacement is GetAddressOfVar(var, stack,
// dontDereference, returnAddressOfUninitializedObjects=true) to obtain the
// destination slot (or the raw, uninitialized value storage), then load
// straight into it — which for value types constructs in place via the
// registered onLoad, avoiding the old temp-buffer copy entirely.
//
// dontDereference: for handle/reference slots we want the address of the
// pointer slot itself (we write the restored pointer there), so pass true; for
// primitives and value objects we want the value storage, so pass false. For a
// by-value local handle GetAddressOfVar returns the slot regardless, so the
// flag only matters for reference-parameter slots.
static void zLoadVariable(Zodiac::zIZodiacReader* reader, asIScriptContext* ctx, int var, int stack, int address, int asTypeId)
{
	if(asTypeId <= asTYPEID_DOUBLE)
	{
		void * dst = ctx->GetAddressOfVar(var, stack, false, true);
		if(dst) reader->LoadScriptObject(dst, address, asTypeId, true);
		return;
	}
	else if(asTypeId & asTYPEID_SCRIPTOBJECT)
	{
		// A local handle slot is uninitialized storage (GetAddressOfVar with
		// returnUninitialized=true): the mid-function resume skips the entry
		// bytecode that would have cleared it, so it holds stack garbage. Null it
		// before loading so the reader's alias/null bookkeeping (which asserts an
		// empty slot before populating) sees a clean pointer. isWeak=false: a
		// script-local handle OWNS its reference exactly like a global/member --
		// the function epilogue releases it -- so the reader must leave one owned
		// ref in the slot (needRelease balanced), not treat it as a weak borrow.
		void * dst = ctx->GetAddressOfVar(var, stack, true, true);
		if(dst)
		{
			*(void**)dst = nullptr;
			reader->LoadScriptObject(dst, address, asTypeId | asTYPEID_OBJHANDLE, false);
		}
		return;
	}
	else if(asTypeId & asTYPEID_APPOBJECT || asTypeId & asTYPEID_TEMPLATE)
	{
		auto typeInfo = reader->GetEngine()->GetTypeInfoById(asTypeId);

		if(typeInfo && typeInfo->GetFuncdefSignature())
		{
			void * dst = ctx->GetAddressOfVar(var, stack, true, true);
			if(dst) { *(void**)dst = nullptr; reader->LoadScriptObject(dst, address, asTypeId, false); }
			return;
		}
		else if(typeInfo->GetFlags() & asOBJ_VALUE)
		{
			void * dst = ctx->GetAddressOfVar(var, stack, false, true);
			if(!dst)
				return;

			// A value local's inline stack slot is RAW, uninitialized memory (the resume
			// skipped the prologue that would have constructed it). Most registered value
			// types' onLoad handlers cope: they placement-construct or write fields into the
			// raw slot without reading prior state, so loading straight into dst is correct
			// (and required — a std::string, for instance, keeps a self-pointer in SSO mode
			// and must NOT be relocated by a byte copy).
			//
			// The exception is the asOBJ_ASHANDLE value types (`ref`/`weakref`): their onLoad
			// Set() RELEASES the previously-held reference first, so it needs a validly
			// constructed object — a raw slot makes Set() release garbage (SEGV). They also
			// need 8-byte alignment the 4-byte stack slot cannot give, tripping UBSan in the
			// glue. ASHANDLE types hold only external pointers (no self-pointers), so they ARE
			// trivially relocatable: construct + load in an aligned heap instance, then move
			// the bytes into the slot and release the emptied husk (destructor is a no-op).
			if(typeInfo->GetFlags() & asOBJ_ASHANDLE)
			{
				auto engine = reader->GetEngine();
				if(void * aligned = engine->CreateScriptObject(typeInfo))
				{
					asUINT sz = typeInfo->GetSize();
					reader->LoadScriptObject(aligned, address, asTypeId, true);
					memcpy(dst, aligned, sz);
					memset(aligned, 0, sz);
					engine->ReleaseScriptObject(aligned, typeInfo);
				}
				return;
			}

			reader->LoadScriptObject(dst, address, asTypeId, true);
			return;
		}

		// app-object handle: same owning-reference semantics as a script handle.
		void * dst = ctx->GetAddressOfVar(var, stack, true, true);
		if(dst) { *(void**)dst = nullptr; reader->LoadScriptObject(dst, address, asTypeId | asTYPEID_OBJHANDLE, false); }
		return;
	}

	assert(false);
}


void Zodiac::ZodiacLoad(zIZodiacReader* reader, asIScriptContext** _ctx, int&)
{
	auto file = reader->GetFile();

	int callStackSize{};
	uint32_t status;
	file->Read(&callStackSize);
	file->Read(&status);

	asEContextState state = (asEContextState)status;

	if(callStackSize < 0)
	{
		*_ctx = nullptr;
		return;
	}

	auto ctx = *_ctx = reader->GetEngine()->RequestContext();
	if(!callStackSize) return;

	// Symmetric with ZodiacSave: a PREPARED context was persisted as just its initial
	// function. Re-Prepare rather than running the deserialization frame path, which
	// would leave the context in asEXECUTION_SUSPENDED instead of asEXECUTION_PREPARED.
	if(state == asEXECUTION_PREPARED)
	{
		uint32_t fn{};
		file->Read(&fn);
		auto func = reader->LoadFunction(fn);
		if(func)
		{
			ctx->Prepare(func);

			// Restore the pushed argument frame that ZodiacSave persisted. After
			// Prepare() the slots are addressable through GetAddressOfArg (still
			// PREPARED state); load each with the same flags zLoadVariable uses:
			// primitives load by value, handles null-then-load an owned reference the
			// prepared frame's clean-up will release.
			uint32_t argCount{};
			file->Read(&argCount);

			StackVar arg;
			for(uint32_t a = 0; a < argCount; ++a)
			{
				if(sizeof(arg) != file->Read(&arg)) { func->Release(); throw zE_EndOfFile; }

				if(arg.typeId == 0)
					continue;

				int argTypeId = reader->LoadTypeId(arg.typeId);
				void * argAddr = ctx->GetAddressOfArg(a);
				if(!argAddr)
					continue;

				if(argTypeId <= asTYPEID_DOUBLE)
					reader->LoadScriptObject(argAddr, arg.object, argTypeId, true);
				else if(argTypeId & asTYPEID_OBJHANDLE)
				{
					*(void**)argAddr = nullptr;
					reader->LoadScriptObject(argAddr, arg.object, argTypeId, false);
				}
				else
					reader->LoadScriptObject(argAddr, arg.object, argTypeId, true);
			}

			func->Release();
		}
		return;
	}

	ctx->StartDeserialization();

//write stack frames
	StackFrame sf;
	for(int i = 0; i < callStackSize; ++i)
	{
		file->Read(&sf);

		if(sf.isCallState == true)
		{
			sf.callState.PushFunction(reader, ctx);
		}
		else
		{
			ctx->PushState();
			sf.state.SetToContext(reader, ctx, 1);
		}
	}

//write current registers
	if(sizeof(sf) != file->Read(&sf)) { throw zE_EndOfFile;	}

	assert(sf.varCount == 0);
	assert(sf.isCallState == 2);
	sf.state.SetToContext(reader, ctx, 0);

//null out-of-scope object/handle slots (reproduce the function-prologue state)
//
// AngelScript does not zero-initialize context stack memory (asNEWARRAY, not calloc)
// and a compiled function relies on two things to make its pointer/handle slots null
// before first use: (a) asBC_ClrVPtr emitted at a variable's declaration, and (b) the
// entry-time nulling in asCContext::PrepareScriptFunction for every onHeap object/handle
// variable. Both happen relative to the ORIGINAL forward run. A deserialized frame is
// rebuilt with fresh (uninitialized) stack memory and its stack registers are then
// overwritten with the saved values, so any object/handle slot whose declaration lies
// AFTER the suspend point — and every temporary the compiler assumed was pre-nulled —
// still holds stack garbage. When execution resumes, the first assignment into such a
// slot runs asBC_RefCpyV, which RELEASES the slot's prior contents (CScriptArray::Release,
// script-object release, CScriptHandle release, ...) — releasing garbage and crashing.
//
// This clears EVERY onHeap object/handle slot (mirroring PrepareScriptFunction's entry
// step); the live, in-scope variables are then restored on top of the cleared slots in
// the loop below. Clearing the in-scope ones too is deliberate: an in-scope variable can
// be a dead anonymous temporary whose storage was already gone at the suspend point (the
// writer emitted a typeId-0 placeholder for it), and the restore loop skips those — so if
// we did not null them here they would keep their stack garbage and be released on resume.
// Inline value locals are left untouched — their construction is tracked separately by the
// VM and they are not pointer-released; the in-scope live ones are rebuilt by the loop.
	for(uint32_t i = 0; i < ctx->GetCallstackSize(); ++i)
	{
		auto N = ctx->GetVarCount(i);

		for(uint16_t j = 0; j < N; ++j)
		{
			int slotTypeId = 0;
			ctx->GetVar(j, i, nullptr, &slotTypeId);
			if(slotTypeId <= asTYPEID_DOUBLE)
				continue; // primitive slot holds no releasable pointer

			void * slot = ctx->GetAddressOfVar(j, i, true, true);
			if(!slot)
				continue;

			bool pointerSlot = (slotTypeId & asTYPEID_OBJHANDLE) != 0;
			if(!pointerSlot)
			{
				// A non-handle object variable holds a raw pointer in its slot only when
				// it lives on the heap; then GetAddressOfVar dereferences that pointer, so
				// the value-address differs from the slot-address. Inline value locals
				// return the same address for both and must be left alone.
				void * valueAddr = ctx->GetAddressOfVar(j, i, false, true);
				pointerSlot = (valueAddr != slot);
			}

			if(pointerSlot)
				*(void**)slot = nullptr;
		}
	}

//write variable contents
//	(PREPARED is handled up-front via re-Prepare; only SUSPENDED reaches here)

	// A by-reference parameter (&inout, unsafe refs) does not own storage: its slot
	// holds a POINTER into another frame — the caller variable it aliases. The writer
	// unified the two by address, so the aliased caller local and the reference slot
	// carry the SAME object id (StackVar::object). We cannot restore such a slot by
	// dereferencing (the pointer is fresh stack garbage) and we cannot give it private
	// storage (the aliasing — the callee writing through the caller's variable — is the
	// whole point). Instead, restore every OWNED variable first, remembering where each
	// object id landed, then point each reference slot at the restored storage of its id.
	// The passes matter because the callee frame (innermost) is restored before its
	// caller, so the alias target does not yet exist when the reference slot is seen.
	std::unordered_map<uint32_t, void*> restoredById;
	struct DeferredRef { void ** slot; uint32_t object; };
	std::vector<DeferredRef> deferredRefs;

	StackVar var;
	for(uint32_t i = 0; i < ctx->GetCallstackSize(); ++i)
	{
		auto N = ctx->GetVarCount(i);

		for(uint16_t j = 0; j < N; ++j)
		{
			if(!ctx->IsVarInScope(j, i))
				continue;

			if(sizeof(var) != file->Read(&var)) { throw zE_EndOfFile; }

			assert(var.stackLevel == i);
			assert(var.varId == j);

			// Placeholder record for a dead anonymous temporary (see ZodiacSave):
			// nothing was serialized, so there is nothing to restore.
			if(var.typeId == 0)
				continue;

			auto typeInfo = reader->LoadTypeId(var.typeId);

			// Is this slot a by-reference parameter? A reference parameter does not own
			// storage — its slot holds a pointer aliasing the caller's variable, so it
			// must be re-pointed at the restored aliased storage taking NO owned
			// reference. The reliable signal is the variable's type modifier
			// (asTM_INOUTREF for unsafe &inout / &in / &out), which GetVar derives from
			// the static function signature (valid on the reconstructed frame). This is
			// necessary for OBJECT/HANDLE ref params: the address-differ heuristic below
			// cannot tell an aliasing reference from an owned heap object, and SaveTypeId
			// strips asTYPEID_OBJHANDLE so the loaded typeId cannot be inspected for
			// handle-ness. Without this an object/handle ref param would fall to
			// zLoadVariable and deposit an owned (AddRef'd) reference into a slot the
			// callee never releases (AngelScript skips &-parameter slots on cleanup) — a
			// leak that also drives a use-after-free on resume.
			asETypeModifiers mods = asTM_NONE;
			{
				int mtid = 0;
				ctx->GetVar(j, i, nullptr, &mtid, &mods, nullptr, nullptr);
			}
			bool isRefParam = (mods & asTM_INOUTREF) != 0;

			// The primitive/value ref-param path: such slots hold a pointer AngelScript
			// dereferences, so the value-address (dereferenced) and the slot-address
			// (raw) differ. Owned inline primitives/values return the same address for
			// both. These two branches DO dereference in zLoadVariable, so the alias is
			// restored by re-pointing the raw slot.
			bool derefBranch = false;
			if(typeInfo > 0 && typeInfo <= asTYPEID_DOUBLE)
				derefBranch = true;
			else if(auto ti = reader->GetEngine()->GetTypeInfoById(typeInfo);
			        ti && (ti->GetFlags() & asOBJ_VALUE)
			        && !(typeInfo & asTYPEID_OBJHANDLE) && !ti->GetFuncdefSignature())
				derefBranch = true;

			if(derefBranch
			&& ctx->GetAddressOfVar(j, i, false, true) != ctx->GetAddressOfVar(j, i, true, true))
			{
				// Primitive/value reference parameter: defer until its aliased id is restored.
				deferredRefs.push_back(
					DeferredRef{ (void**)ctx->GetAddressOfVar(j, i, true, true), var.object });
				continue;
			}
			// Object/handle reference parameter: same alias treatment. Its slot is the
			// raw pointer slot (dontDereference); re-point it at the aliased storage with
			// no owned reference, so net refcount stays correct after frame cleanup.
			else if(isRefParam)
			{
				if(void ** slot = (void**)ctx->GetAddressOfVar(j, i, true, true))
				{
					deferredRefs.push_back(DeferredRef{ slot, var.object });
					continue;
				}
			}

			zLoadVariable(reader, ctx, j, i, var.object, typeInfo);

			// Remember the storage of each OWNED object id so a reference parameter that
			// aliases it can be re-pointed here (record the value storage, not the slot).
			if(void * owned = ctx->GetAddressOfVar(j, i, false, true))
				restoredById.emplace(var.object, owned);
		}
	}

	for(auto & d : deferredRefs)
	{
		auto it = restoredById.find(d.object);
		*d.slot = (it != restoredById.end()) ? it->second : nullptr;
	}

	ctx->FinishDeserialization();
}

#endif
