#include "print_func.h"
#include <string>
#include "add_on/scriptarray/scriptarray.h"
#include <cstring>
#include <angelscript.h>
#include <cinttypes>
#include <cassert>

#include <iostream>

#define INS_1 "?&in = null"
#define INS_2 INS_1 ", " INS_1
#define INS_4 INS_2 ", " INS_2
#define INS_8 INS_4 ", " INS_4
#define INS_16 INS_8 ", " INS_8

#define V_ARG(n, q) void q* objPtr##n, int typeId##n
#define V_ARGS_1(q) V_ARG(0, q)
#define V_ARGS_2(q) V_ARGS_1(q) , V_ARG(1, q)
#define V_ARGS_4(q) V_ARGS_2(q) , V_ARG(2, q) , V_ARG(3, q)
#define V_ARGS_8(q) V_ARGS_4(q) , V_ARG(4, q) , V_ARG(5, q) , V_ARG(6, q), V_ARG(7, q)
#define V_ARGS_16(q) V_ARGS_8(q), V_ARG(8, q) , V_ARG(9, q) , V_ARG(10, q), V_ARG(11, q), V_ARG(12, q) , V_ARG(13, q) , V_ARG(14, q), V_ARG(15, q)

#define IN_ARGS_1 V_ARGS_1(const)
#define IN_ARGS_2 V_ARGS_2(const)
#define IN_ARGS_4 V_ARGS_4(const)
#define IN_ARGS_8 V_ARGS_8(const)
#define IN_ARGS_16 V_ARGS_16(const)

#define W_ARG(n) objPtr##n, typeId##n
#define W_ARGS_1 W_ARG(0)
#define W_ARGS_2 W_ARGS_1 , W_ARG(1)
#define W_ARGS_4 W_ARGS_2 , W_ARG(2) , W_ARG(3)
#define W_ARGS_8 W_ARGS_4 , W_ARG(4) , W_ARG(5) , W_ARG(6), W_ARG(7)
#define W_ARGS_16 W_ARGS_8, W_ARG(8) , W_ARG(9) , W_ARG(10), W_ARG(11), W_ARG(12) , W_ARG(13) , W_ARG(14), W_ARG(15)

static void PrintItem(FILE * file, int depth, void const* objPtr, int typeId)
{
	switch(typeId)
	{
	case asTYPEID_VOID: return;
	case asTYPEID_BOOL:		fprintf(file, "%s", (*(bool const*)objPtr)? "true" : "false"); return;
	case asTYPEID_INT8:		fprintf(file, "%i",   (uint32_t) *(int8_t   const*)objPtr); return;
	case asTYPEID_INT16:	fprintf(file, "%i",   (uint32_t) *(int16_t  const*)objPtr); return;
	case asTYPEID_INT32:	fprintf(file, "%i",				 *(int32_t  const*)objPtr); return;
	case asTYPEID_INT64:	fprintf(file, "%li",			 *(int64_t  const*)objPtr); return;
	case asTYPEID_UINT8:	fprintf(file, "%u",   (uint32_t) *(uint8_t  const*)objPtr); return;
	case asTYPEID_UINT16:	fprintf(file, "%u",	  (uint32_t) *(uint16_t const*)objPtr); return;
	case asTYPEID_UINT32:	fprintf(file, "%u",				 *(uint32_t const*)objPtr); return;
	case asTYPEID_UINT64:	fprintf(file, "%lu",			 *(uint64_t const*)objPtr); return;
	case asTYPEID_FLOAT:	fprintf(file, "%f",				 *(float    const*)objPtr); return;
	case asTYPEID_DOUBLE:	fprintf(file, "%f",   (float)	 *(double   const*)objPtr); return;
	default: break;
	}

	auto ctx = asGetActiveContext();
	if(!ctx) return;
	auto engine = ctx->GetEngine();

	int stringTypeId = engine->GetStringFactoryReturnTypeId();

	if(stringTypeId == typeId)
	{
		fprintf(file, "%s", ((std::string const*)objPtr)->c_str());
		return;
	}

	auto typeInfo = engine->GetTypeInfoById(typeId);

	if(depth < 2 && strcmp(typeInfo->GetName(), "array") == 0)
	{
		CScriptArray const* array{};

		if(typeId & asTYPEID_OBJHANDLE)
			array = *reinterpret_cast<CScriptArray * const*>(objPtr);
		else
			array = reinterpret_cast<CScriptArray const*>(objPtr);


		std::cerr << (void*)array << std::endl;

		if(array->GetSize() == 0)
			fprintf(file, "[]");
		else
		{
			fprintf(file, "[");

			for(uint32_t i = 0; i < array->GetSize(); ++i)
			{
				PrintItem(file, depth+1, array->At(i), array->GetElementTypeId());
				fprintf(file, (i+1 == array->GetSize())? "]" : ", ");
			}
		}

		return;
	}


	if(typeInfo)
	{
		if(typeId & asTYPEID_OBJHANDLE)
		{
			fprintf(file, "@%s(" PRIxPTR ")", typeInfo->GetName(), (intptr_t)(*(void*const*)objPtr));
		}
		else
		{
			fprintf(file, "%s(" PRIxPTR ")", typeInfo->GetName(), (intptr_t)(objPtr));
		}

		return;
	}


	if(typeId & asTYPEID_SCRIPTOBJECT)
	{
		auto typeInfo = engine->GetTypeInfoById(typeId);

		fprintf(file, "%s(" PRIxPTR ")", typeInfo->GetName(), (intptr_t)objPtr);
		return;
	}

	if(typeId & asTYPEID_APPOBJECT)
	{
		auto typeInfo = engine->GetTypeInfoById(typeId);

		fprintf(file, "%s(" PRIxPTR ")", typeInfo->GetName(), (intptr_t)objPtr);
		return;
	}

	fprintf(file, "UNKNOWN");

	return;
}

static void PrintTemplate(FILE * file, void const *objPtr, int typeId)
{
	PrintItem(file, 0, objPtr, typeId);
}

template<typename... Args>
static void PrintTemplate(FILE * file, void const *objPtr, int typeId, Args... args)
{
	PrintItem(file, 0, objPtr, typeId);
	PrintTemplate(file, std::move(args)...);
}

static void PrintFunc(IN_ARGS_16)
{
	PrintTemplate(stdout, W_ARGS_16);
}

static void PrintFuncLn(IN_ARGS_16)
{
	PrintTemplate(stdout, W_ARGS_16);
	printf("\n");
}

void Print::asRegister( asIScriptEngine * engine)
{
	int r;

	r = engine->RegisterGlobalFunction("void Print(" INS_16 ")", asFUNCTION(PrintFunc), asCALL_CDECL);  assert(r == asALREADY_REGISTERED || r >= 0);
	r = engine->RegisterGlobalFunction("void Println(" INS_16 ")", asFUNCTION(PrintFuncLn), asCALL_CDECL);  assert(r == asALREADY_REGISTERED || r >= 0);
}
