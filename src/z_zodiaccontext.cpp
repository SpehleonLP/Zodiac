#include "z_zodiaccontext.h"
#ifdef HAVE_ZODIAC
#include <cassert>
#include <iostream>

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
		void * scriptObject{};
		auto _currentFunction = reader->LoadFunction(currentFunction);

		reader->LoadScriptObject(&scriptObject, objectId, reader->LoadTypeId(objectType));
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
		_initialFunction = reader->LoadFunction(initialFunction);
		_objectType	  = reader->LoadTypeInfo(objectType, true);
		reader->LoadScriptObject(&objectRegister, objectRegister, objectType);

		int r = ctx->SetStateRegisters(i, _callingSystemFunction, _initialFunction, originalStackPointer, argumentsSize, valueRegister, _objectRegister, _objectType);

		// SetStateRegisters stores these function pointers raw (it does NOT AddRef);
		// the context borrows them, kept alive by their owning module. LoadFunction
		// handed us OWNED references, so release ours here — matching the
		// CtxCallState::PushFunction load/use/release pattern — or they leak.
		if(_initialFunction)       _initialFunction->Release();
		if(_callingSystemFunction) _callingSystemFunction->Release();

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

			// GetVarTypeId is deprecated; the live typeId comes from GetVar's out-param.
			int varTypeId = 0;
			ctx->GetVar(j, i, nullptr, &varTypeId);
//write to confirm on reading
			var.stackLevel = i;
			var.varId = j;
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
		void * dst = ctx->GetAddressOfVar(var, stack, true, true);
		if(dst) reader->LoadScriptObject(dst, address, asTypeId | asTYPEID_OBJHANDLE, true);
		return;
	}
	else if(asTypeId & asTYPEID_APPOBJECT || asTypeId & asTYPEID_TEMPLATE)
	{
		auto typeInfo = reader->GetEngine()->GetTypeInfoById(asTypeId);

		if(typeInfo && typeInfo->GetFuncdefSignature())
		{
			void * dst = ctx->GetAddressOfVar(var, stack, true, true);
			if(dst) reader->LoadScriptObject(dst, address, asTypeId, true);
			return;
		}
		else if(typeInfo->GetFlags() & asOBJ_VALUE)
		{
			void * dst = ctx->GetAddressOfVar(var, stack, false, true);
			if(dst) reader->LoadScriptObject(dst, address, asTypeId, true);
			return;
		}

		void * dst = ctx->GetAddressOfVar(var, stack, true, true);
		if(dst) reader->LoadScriptObject(dst, address, asTypeId | asTYPEID_OBJHANDLE, true);
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

//write variable contents

	if(state == asEXECUTION_PREPARED)
	{
		ctx->FinishDeserialization();
		return;
	}


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

			auto typeInfo = reader->LoadTypeId(var.typeId);

			zLoadVariable(reader, ctx, j, i, var.object, typeInfo);
		}
	}

	ctx->FinishDeserialization();
}

#endif
