#include "print_func.h"
#include <string>
#include "add_on/scriptarray/scriptarray.h"
#include <cstring>
#include <angelscript.h>
#include <cinttypes>
#include <cassert>

#include <sstream>
#include <iostream>

Print::PrintNonPrimitiveType* Print::g_PrintRegisteredType{PrintAddonTypes};
Print::PrintNonPrimitiveType* Print::g_PrintScriptObjectType{nullptr};

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

bool Print::PrintAddonTypes(std::ostream & dst, void const *objPtr, int typeId, int depth)
{
	auto ctx = asGetActiveContext();
	if(!ctx) return false;
	auto engine = ctx->GetEngine();

	int stringTypeId = engine->GetStringFactoryReturnTypeId();

	if(stringTypeId == typeId)
	{
		dst << *((std::string const*)objPtr);
		return true;
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
			dst << "[]";
		else
		{
			dst << "[";

			for(uint32_t i = 0; i < array->GetSize(); ++i)
			{
				Print::PrintTemplate(dst, array->At(i), array->GetElementTypeId(), depth+1);
				dst << ((i+1 == array->GetSize())? "]" : ", ");
			}
		}

		return true;
	}

	return false;
}

void Print::PrintTemplate(std::ostream & dst, void const* objPtr, int typeId, int depth)
{
	switch(typeId)
	{
	case asTYPEID_VOID:		return;
	case asTYPEID_BOOL:		dst << ((*(bool const*)objPtr)? "true" : "false"); return;
	case asTYPEID_INT8:		dst <<  *(int8_t   const*)objPtr; return;
	case asTYPEID_INT16:	dst <<  *(int16_t  const*)objPtr; return;
	case asTYPEID_INT32:	dst << 	*(int32_t  const*)objPtr; return;
	case asTYPEID_INT64:	dst << 	*(int64_t  const*)objPtr; return;
	case asTYPEID_UINT8:	dst <<  *(uint8_t  const*)objPtr; return;
	case asTYPEID_UINT16:	dst <<  *(uint16_t const*)objPtr; return;
	case asTYPEID_UINT32:	dst << 	*(uint32_t const*)objPtr; return;
	case asTYPEID_UINT64:	dst << 	*(uint64_t const*)objPtr; return;
	case asTYPEID_FLOAT:	dst << *(float    const*)objPtr; return;
	case asTYPEID_DOUBLE:	dst <<  *(double   const*)objPtr; return;
	default: break;
	}

	auto ctx = asGetActiveContext();
	if(!ctx) return;
	auto engine = ctx->GetEngine();

	auto typeInfo = engine->GetTypeInfoById(typeId);

	if(!typeInfo)
	{
		dst << "BAD_TYPEID";
		return;
	}

	if(objPtr == nullptr)
	{
		dst << typeInfo->GetName();
		return;
	}

	if(typeInfo->GetFuncdefSignature())
	{
		auto func = reinterpret_cast<asIScriptFunction const*>(objPtr);
		dst << func->GetDeclaration(true, true, true);
		return;
	}

	auto enumValueCount = typeInfo->GetEnumValueCount();
	if(enumValueCount)
	{
		int value = *(uint32_t const*)objPtr;

		dst << typeInfo->GetName();

		for(uint32_t i = 0; i < enumValueCount; ++i)
		{
			int val;
			const char * text = typeInfo->GetEnumValueByIndex(i, &val);

			if(val == value)
			{
				dst << "::" << text;
			}
		}

		return;
	}

	if(typeId & asTYPEID_SCRIPTOBJECT)
	{
		if(g_PrintScriptObjectType && g_PrintScriptObjectType(dst, objPtr, typeId, depth))
			return;

		if(typeId & asTYPEID_OBJHANDLE)
		{
			dst << "@" << typeInfo->GetName() << "(" <<  *(void**)objPtr << ")";
		}
		else
		{
			dst << typeInfo->GetName() << "(" <<  objPtr << ")";
		}
	}

	if(typeId & (asTYPEID_APPOBJECT|asTYPEID_TEMPLATE))
	{
		if(g_PrintRegisteredType != nullptr)
		{
			if(typeId & asTYPEID_OBJHANDLE)
			{
				typeId &= ~(asTYPEID_OBJHANDLE|asTYPEID_HANDLETOCONST);
				objPtr = *(void**)objPtr;
			}

			if(g_PrintRegisteredType(dst, objPtr, typeId, depth))
				return;
		}

		dst << "RegisteredObject";

		return;
	}

	dst << "UNKNOWN";

	return;
}

static void PrintFunc(IN_ARGS_16)
{
	Print::PrintTemplate(std::cout, W_ARGS_16);
}

static void PrintFuncLn(IN_ARGS_16)
{
	Print::PrintTemplate(std::cout, W_ARGS_16);
	printf("\n");
}

void Print::asRegister( asIScriptEngine * engine)
{
	int r;

	r = engine->RegisterGlobalFunction("void Print(" INS_16 ")", asFUNCTION(PrintFunc), asCALL_CDECL);  assert(r == asALREADY_REGISTERED || r >= 0);
	r = engine->RegisterGlobalFunction("void Println(" INS_16 ")", asFUNCTION(PrintFuncLn), asCALL_CDECL);  assert(r == asALREADY_REGISTERED || r >= 0);
}
