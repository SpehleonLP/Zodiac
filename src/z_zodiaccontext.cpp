#include "z_zodiaccontext.h"

void Zodiac::ZodiacSave(zIZodiacWriter*, asIScriptContext const*, int&) {}
void Zodiac::ZodiacLoad(zIZodiacReader*, asIScriptContext*, int&) {}

/*
struct StackFrame
{
//manually pad to 40 bytes just in case.
	uint32_t type;
	uint32_t varCount;

	union
	{
		struct
		{
			uint32_t callingSystemFunction;
			uint32_t intialFunction;
			uint32_t originalStackPointer;
			uint32_t argumentsSize;

		//call stack uses intptr_t so its split between tmp[5]/tmp[6] for x86 machines i suppose
			uint64_t valueRegister;
			uint32_t objectRegister;
			uint32_t objectType;
		} state;

		struct
		{
			uint32_t stackFramePointer;
			uint32_t currentFunction;
			uint32_t programPointer;
			uint32_t stackPointer;
			uint32_t stackIndex;
		} callState;
	};
};

struct ContextData
{
	uint32_t currentFunction;
	uint32_t callingSystemFunction;
	uint32_t callStackSize;
	uint32_t initialFunction;

	struct Registers
	{
		uint32_t programPointer;
		uint32_t stackFramePointer;
		uint32_t stackPointer;
		uint64_t valueRegister;
		uint32_t objectRegister;
		uint32_t objectType;
		bool     doProcessSuspend;
		uint32_t context;
	} regs;
};

void Zodiac::ZodiacSave(zIZodiacWriter* writer, asIScriptContext* ctx, int&)
{
	ContextData data;

	data.currentFunction       = writer->SaveFunction(ctx->GetFunction(0));
	data.callingSystemFunction = writer->SaveFunction(ctx->GetSystemFunction());
	data.callStackSize         = ctx->GetCallstackSize();

	struct Buffer
	{
		uint32_t typeId;
		uint32_t object;
		uint32_t function;
		uint32_t programPtr;
		uint32_t varCount;
	} b;


	auto file = writer->GetFile();

	for(uint32_t i = 0; i < ctx->GetCallstackSize(); ++i)
	{
		b.typeId	 = writer->SaveTypeId(ctx->GetThisTypeId(i));
		b.object	 = writer->SaveScriptObject(ctx->GetThisPointer(i), ctx->GetThisTypeId(i));
		b.function	 = writer->SaveFunction(ctx->GetFunction(i));
		b.programPtr = ctx->GetProgramPointer(i);
		b.typeId	 = ctx->GetVarCount();

		file->Write(&b);

		for(uint16_t j = 0; j < b.varCount; ++j)
		{
			if(!ctx->IsVarInScope(j, i))
				continue;

			auto typeInfo = ctx->GetVarTypeId(j, i);
//write to confirm on reading
			b.function = j;
			b.typeId = writer->SaveTypeId(typeInfo);
			b.object = writer->SaveScriptObject(ctx->GetAddressOfVar(j, i), typeInfo);

			file->Write(&b);
		}
	}


}

void Zodiac::ZodiacLoad(zIZodiacReader*, asIScriptContext*, int&) {}

*/
