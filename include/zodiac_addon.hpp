#ifndef ZODIACADDON_H
#define ZODIACADDON_H

#ifdef HAVE_ZODIAC

#ifndef ZODIAC_H
#include "zodiac.h"
#endif

#if 1
#include "add_on/scriptstdstring/scriptstdstring.h"
#include "add_on/contextmgr/contextmgr.h"
#include "add_on/scriptfile/scriptfile.h"
#include "add_on/scriptany/scriptany.h"
#include "add_on/scriptdictionary/scriptdictionary.h"
#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptgrid/scriptgrid.h"
#include "add_on/scripthandle/scripthandle.h"
#include "add_on/weakref/weakref.h"
#endif

#ifndef NDEBUG
#include <iostream>
#endif

#include <string>
#include <cassert>
#include <cstring>
extern void PrintArray(FILE * file, int depth, void const* objPtr, int typeId);

namespace Zodiac
{
#ifdef CONTEXTMGR_H
	void ZodiacSaveContextManager(zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CContextMgr const* mgr, int&)
	{
		int64_t time{};
		if(mgr->m_getTimeFunc)
		{
			time = (mgr->m_getTimeFunc)();
		}

		auto file = writer->GetFile();

		uint32_t noThreads = mgr->m_threads.size();
		uint32_t curThread = mgr->m_currentThread;

		file->Write(&noThreads);
		file->Write(&curThread);

		for(auto p : mgr->m_threads)
		{
			int sleepUntil = std::max<int64_t>((int64_t)p->sleepUntil - time, 0);
			uint32_t size = p->coRoutines.size();

			file->Write(&sleepUntil);
			file->Write(&p->currentCoRoutine);
			file->Write(&size);

			int id = writer->SaveContext(p->keepCtxAfterExecution);

			file->Write(&id);

			for(auto ctx : p->coRoutines)
			{
				id = writer->SaveContext(ctx);
				file->Write(&id);
			}
		}
	}

	void ZodiacLoadContextManager(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CContextMgr* mgr, int&, bool)
	{
		uint32_t time{};
		if(mgr->m_getTimeFunc)
		{
			time = (mgr->m_getTimeFunc)();
		}

		auto file = reader->GetFile();

		uint32_t noThreads{};
		uint32_t curThread{};

		file->Read(&noThreads);
		file->Read(&curThread);

		mgr->m_currentThread = curThread;
		mgr->m_threads.resize(noThreads, nullptr);

		for(auto & p : mgr->m_threads)
		{
			p = new CContextMgr::SContextInfo();
			int sleepUntil{};
			uint32_t size{};
			int thread_id{};

			file->Read(&sleepUntil);
			file->Read(&p->currentCoRoutine);
			file->Read(&size);
			file->Read(&thread_id);

			p->sleepUntil = (int64_t)sleepUntil + time;
			p->keepCtxAfterExecution = reader->LoadContext(thread_id);
			p->coRoutines.resize(size, nullptr);

			for(auto & ctx : p->coRoutines)
			{
				file->Read(&thread_id);
				ctx = reader->LoadContext(thread_id);
			}

			if(p->keepCtxAfterExecution)
				p->keepCtxAfterExecution->Release();
		}
	}
#endif

#ifdef SCRIPTFILE_H
	inline void ZodiacSave(Zodiac::zIZodiacWriter *, CScriptFile const*, int&)
	{
		throw zE_ObjectUnserializable;
	}

	inline void ZodiacLoad(zIZodiacReader *, CScriptFile ** file, int&, bool isHandle)
	{
		assert(file != nullptr);
		assert(*file == nullptr || isHandle == false);

		if(isHandle)
			*file = new CScriptFile();
	}
#endif


/* POD doesn't need defined...
#ifdef SCRIPTDATETIME_H
	void ZodiacSave(zIZodiacWriter *, CDateTime*, int&);
	void ZodiacLoad(zIZodiacReader *, CDateTime*, int&);
#endif*/

	struct zAny
	{
		union
		{
			asINT64 valueInt;
			double  valueFlt;
			void   *valueObj;
		};

		int typeId;
	};

#ifdef SCRIPTANY_H
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptAny const* any, int&)
	{
		assert(any != nullptr);

		zAny value;

		value.typeId = any->GetTypeId();
		int rTypeId = value.typeId | ((value.typeId & asTYPEID_SCRIPTOBJECT)? asTYPEID_OBJHANDLE : 0);
		any->Retrieve(&value, rTypeId);

		if(value.typeId > asTYPEID_DOUBLE)
		{
			value.valueInt =  writer->SaveScriptObject(value.valueObj, value.typeId, any);
		}

		writer->GetFile()->Write(&value);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptAny** any, int&, bool isHandle)
	{
		assert(any != nullptr);
		assert(*any == nullptr || isHandle == false);

		zAny value;
		reader->GetFile()->Read(&value);

		if(isHandle)
			new(*any) CScriptAny(reader->GetEngine());

//is this okay? type punning is discouraged in C++11 for some reason??
		if(value.typeId <= asTYPEID_DOUBLE)
		{
			(*any)->Store(&value.valueInt, value.typeId);
		}
		else
		{
			reader->LoadScriptObject(&value.valueObj, value.valueInt, value.typeId);

			(*any)->StoreMove(&value.valueObj, value.typeId, CScriptAny::MovePointer::isReference);
		}
	}
#endif

#ifdef SCRIPTARRAY_H
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptArray const* array, int&)
	{
		assert(array != nullptr);

		auto file = writer->GetFile();
		auto elementTypeId   = array->GetElementTypeId();
		auto typeInfo		 = array->GetArrayObjectType();
		int arrayTypeIndex   = writer->SaveTypeInfo(typeInfo);

		uint32_t size = array->GetSize();
		file->Write(&arrayTypeIndex);
		file->Write(&size);
//write

		for(uint32_t i = 0; i < size; ++i)
		{
			int id = writer->SaveScriptObject(array->At(i), elementTypeId, array);
			file->Write(&id);
		}
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptArray** array, int&, bool isHandle)
	{
		assert(array != nullptr);
		assert(*array == nullptr || isHandle == false);

		int typeId{};
		uint32_t size{};

		auto file = reader->GetFile();

		file->Read(&typeId);
		file->Read(&size);

		asITypeInfo * arrayType = reader->LoadTypeInfo(typeId, false);
		assert(arrayType != nullptr);

		if(isHandle)
			*array = CScriptArray::Create(arrayType, size);
		else
		{
			assert((*array)->GetArrayObjectType() == arrayType);
			(*array)->Resize(size);
		}

		uint32_t elementTypeId = (*array)->GetElementTypeId();

		for(uint32_t i = 0; i < size; ++i)
		{
			int address;
			file->Read(&address);
			reader->LoadScriptObject((*array)->At(i), address, elementTypeId);
		}

		assert((*array)->GetSize() == size);
	}
#endif

#if defined(SCRIPTSTDSTRING_H) || defined(SCRIPTDICTIONARY_H)
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, std::string const* string, int&)
	{
		assert(string != nullptr);

		int string_id = writer->SaveString(string->c_str());
		writer->GetFile()->Write(&string_id);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, std::string* string, int&, bool)
	{
		int string_id;
		reader->GetFile()->Read(&string_id);
		new(string) std::string(reader->LoadString(string_id));
	}
#endif

#ifdef SCRIPTDICTIONARY_H
//are these safe?? type punning is undefined behavior...
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptDictValue const* dict, int&)
	{
		assert(dict != nullptr);

		int64_t  value;
		int      typeId  = dict->GetTypeId();

		if(typeId < asTYPEID_FLOAT)
		{
			dict->Get(writer->GetEngine(), value);
		}
		else if(typeId <= asTYPEID_DOUBLE)
		{
			double dbl;
			dict->Get(writer->GetEngine(), dbl);
			memcpy(&value, &dbl, sizeof(double));
		}
		else
		{
			value =  writer->SaveScriptObject(dict->GetAddressOfValue(), typeId, dict);
		}

		typeId = writer->SaveTypeId(typeId) | (typeId & zTYPEID_OBJHANDLE);
		writer->GetFile()->Write(&typeId);
		writer->GetFile()->Write(&value);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptDictValue* dict, int&, bool)
	{
		assert(dict != nullptr);

		int64_t  value{};
		int      typeId{};

		reader->GetFile()->Read(&typeId);
		reader->GetFile()->Read(&value);
		typeId = reader->LoadTypeId(typeId & asTYPEID_MASK_SEQNBR) | (typeId & zTYPEID_OBJHANDLE);

		dict->FreeValue(reader->GetEngine());

		if(typeId <= asTYPEID_DOUBLE)
		{
			new(dict) CScriptDictValue(reader->GetEngine(), &value, typeId);
		}
		else
		{
			void * dst{};
			reader->LoadScriptObject(&dst, value, typeId);

			dict->m_typeId = typeId;
			dict->m_valueObj = dst;
		}
	}

	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptDictionary const* dict, int&)
	{
		assert(dict != nullptr);

		auto file = writer->GetFile();

		uint32_t size = dict->GetSize();
		file->Write(&size);

		for(auto & itr : *dict)
		{
			int real_type;
			ZodiacSave(writer, &itr.GetKey(), real_type);
			ZodiacSave(writer, &itr.GetValue(), real_type);
		}
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptDictionary** dict, int&, bool isHandle)
	{
		assert(dict != nullptr);
		assert(*dict == nullptr || isHandle == false);

		if(isHandle)
			*dict = CScriptDictionary::Create(reader->GetEngine());
		else
		{
			assert((*dict)->GetEngine() == reader->GetEngine());
			(*dict)->DeleteAll();
		}

		auto file = reader->GetFile();

		uint32_t size;
		file->Read(&size);

		for(uint32_t i = 0; i < size; ++i)
		{
			std::string key;
			CScriptDictValue value;

			int real_type;
			ZodiacLoad(reader, &key, real_type, false);
			ZodiacLoad(reader, &value, real_type, false);

			(*dict)->Insert(std::move(key), std::move(value));
		}
	}

#endif

#ifdef SCRIPTGRID_H
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptGrid const* grid, int&)
	{
		assert(grid != nullptr);

		auto file			= writer->GetFile();
		auto elementTypeId  = grid->GetElementTypeId();
		int gridTypeIndex   = writer->SaveTypeInfo(grid->GetGridObjectType());

		uint32_t width = grid->GetWidth();
		uint32_t height = grid->GetHeight();

		file->Write(&gridTypeIndex);
		file->Write(&width);
		file->Write(&height);

		for(uint32_t x = 0; x < width; ++x)
		{
			for(uint32_t y = 0; y < height; ++y)
			{
				int id = writer->SaveScriptObject(grid->At(x, y), elementTypeId, grid);
				file->Write(&id);
			}
		}
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptGrid** grid, int&, bool isHandle)
	{
		assert(grid != nullptr);
		assert(*grid == nullptr || isHandle == false);

		int typeId{};
		uint32_t width{}, height{};

		auto file = reader->GetFile();

		file->Read(&typeId);
		file->Read(&width);
		file->Read(&height);

		asITypeInfo * arrayType = reader->LoadTypeInfo(typeId, false);
		assert(arrayType != nullptr);

		if(isHandle)
			*grid = CScriptGrid::Create(arrayType, width, height);
		else
		{
			assert((*grid)->GetGridObjectType() == arrayType);
			(*grid)->Resize(width, height);
		}

		uint32_t elementTypeId = (*grid)->GetElementTypeId();

		for(uint32_t x = 0; x < width; ++x)
		{
			for(uint32_t y = 0; y < height; ++y)
			{
				int address;
				file->Read(&address);
				reader->LoadScriptObject((*grid)->At(x, y),address, elementTypeId);
			}
		}
	}
#endif


#ifdef SCRIPTHANDLE_H
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptHandle const* handle, int&)
	{
		assert(handle != nullptr);

		auto file = writer->GetFile();

		int typeId      = writer->SaveTypeInfo(handle->GetType());
		int objectId{};

		if(handle->GetType())
			objectId = writer->SaveScriptObject(handle->GetRef(), handle->GetType()->GetTypeId(), nullptr);

		file->Write(&typeId);
		file->Write(&objectId);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptHandle* handle, int&, bool)
	{
		assert(handle != nullptr);

		int typeId{};
		int objectId{};

		auto file = reader->GetFile();

		file->Read(&typeId);
		file->Read(&objectId);

		void * obj{};
		auto typeInfo = reader->LoadTypeInfo(typeId, false);

		if(typeInfo)
		{
			assert(typeInfo == nullptr || typeInfo->GetTypeId() & asTYPEID_OBJHANDLE);
			reader->LoadScriptObject(&obj, objectId, typeInfo->GetTypeId(), true);
		}

		handle->Set(obj, typeInfo);
	}
#endif

#ifdef SCRIPTWEAKREF_H
	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptWeakRef const* handle, int&)
	{
		assert(handle != nullptr);

		auto file = writer->GetFile();

		int refTypeId{};
		int typeId{};
		int objectId{};

		auto object = handle->Get();

		if(object)
		{
			auto typeInfo = handle->GetRefType();

			refTypeId = writer->SaveTypeInfo(handle->GetObjectType());
			typeId    = writer->SaveTypeInfo(typeInfo);
			objectId  = writer->SaveScriptObject(object, typeInfo, nullptr);

			writer->GetEngine()->ReleaseScriptObject(object, typeInfo);
		}

		file->Write(&refTypeId);
		file->Write(&typeId);
		file->Write(&objectId);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptWeakRef* handle, int&, bool)
	{
		assert(handle != nullptr);

		int refTypeId{};
		int typeId{};
		int objectId{};

		reader->GetFile()->Read(&refTypeId);
		reader->GetFile()->Read(&typeId);
		reader->GetFile()->Read(&objectId);

		auto typeInfo = reader->LoadTypeInfo(typeId, false);
		auto handleTypeInfo = reader->LoadTypeInfo(refTypeId, false);

		void * obj{};
		reader->LoadScriptObject(&obj, objectId, typeInfo, true);

		if(handleTypeInfo != nullptr)
		{
			*handle = CScriptWeakRef(obj, handleTypeInfo);
		}
	}
#endif

	inline void ZodiacRegisterAddons(zIZodiac * zodiac)
	{
		(void)zodiac;

#ifdef SCRIPTANY_H
		zRegisterRefType(zodiac, AS_NAMESPACE_QUALIFIER CScriptAny, "any", nullptr);
#endif

#ifdef SCRIPTARRAY_H
		zRegisterRefType(zodiac, AS_NAMESPACE_QUALIFIER CScriptArray, "array", nullptr);
#endif

#ifdef SCRIPTDICTIONARY_H
		zRegisterRefType(zodiac, AS_NAMESPACE_QUALIFIER CScriptDictionary, "dictionary", nullptr);
		zRegisterValueType(zodiac, AS_NAMESPACE_QUALIFIER CScriptDictValue, "dictionaryValue", nullptr);
#endif

#ifdef SCRIPTFILE_H
		zRegisterRefType(zodiac, AS_NAMESPACE_QUALIFIER CScriptFile, "file", nullptr);
#endif

#ifdef SCRIPTGRID_H
		zRegisterRefType(zodiac, AS_NAMESPACE_QUALIFIER CScriptGrid, "grid", nullptr);
#endif

#ifdef SCRIPTSTDSTRING_H
		zRegisterValueType(zodiac, std::string, "string", nullptr);
#endif

#ifdef SCRIPTHANDLE_H
		zRegisterValueType(zodiac, AS_NAMESPACE_QUALIFIER CScriptHandle, "ref", nullptr);
#endif

#ifdef SCRIPTWEAKREF_H
		zRegisterValueType(zodiac, AS_NAMESPACE_QUALIFIER CScriptWeakRef, "weakref", nullptr);
		zRegisterValueType(zodiac, AS_NAMESPACE_QUALIFIER CScriptWeakRef, "const_weakref", nullptr);
#endif

	}

}

#endif

#endif // ZODIACADDON_H
