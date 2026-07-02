#ifndef ZODIACADDON_H
#define ZODIACADDON_H

#ifdef HAVE_ZODIAC

#ifndef ZODIAC_H
#include "zodiac.h"
#endif

#if 1
#include "add_on/scriptstdstring/scriptstdstring.h"
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
// NOTE: The CContextMgr save/load glue moved to include/zodiac_contextmgr.hpp
// (compiled only under ZODIAC_HAVE_PATCHED_CONTEXTMGR). It reaches into
// now-protected CContextMgr internals that the stock SDK does not expose.

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

		zAny value{};

		int asTypeId = any->GetTypeId();

	// An empty any (nothing was store()d) reports typeId asTYPEID_VOID (0). Do NOT
	// call Retrieve on it -- Retrieve asserts on typeId 0. Emit a typeId-0 sentinel
	// record; the load side reconstructs an empty any. (SaveTypeId never yields 0
	// for a real payload: primitives map to their own id >=1 and object payloads
	// carry a non-zero table index / handle bit, so 0 unambiguously means "empty".)
		if(asTypeId == AS_NAMESPACE_QUALIFIER asTYPEID_VOID)
		{
			value.typeId  = 0;
			value.valueInt = 0;
			writer->GetFile()->Write(&value);
			return;
		}

		if(asTypeId <= asTYPEID_DOUBLE)
		{
	// Primitive: GetObjectAddress() points at the 8-byte value union; copy the raw
	// bits (the load side reconstructs them via Store(&valueInt, typeId)).
			memcpy(&value.valueInt,
			       const_cast<AS_NAMESPACE_QUALIFIER CScriptAny*>(any)->GetObjectAddress(),
			       sizeof(value.valueInt));
		}
		else
		{
	// Object payload. GetObjectAddress() yields exactly what SaveScriptObject wants
	// (mirrors the dictionary GetAddressOfValue() path): for a HANDLE it returns
	// &valueObj (the handle slot, which EnqueueNode dereferences); for a VALUE-TYPE
	// object (e.g. string) it returns valueObj (the object pointer itself). This
	// fixes the value-type case: the old Retrieve(&value, ...) path did a full
	// object copy of a >8-byte value type into the 8-byte union, smashing the stack
	// and later handing SaveString a garbage pointer (the reported SEGV).
			void * addr = const_cast<AS_NAMESPACE_QUALIFIER CScriptAny*>(any)->GetObjectAddress();
			value.valueInt = writer->SaveScriptObject(addr, asTypeId, any);
		}

	// Translate the engine-local AS typeId into a stable stored typeId (mirrors the
	// dictionary value path); a raw AS typeId is meaningless in a reload engine.
		value.typeId = writer->SaveTypeId(asTypeId) | (asTypeId & zTYPEID_OBJHANDLE);
		writer->GetFile()->Write(&value);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptAny** any, int&, bool isHandle)
	{
		assert(any != nullptr);
		assert(*any == nullptr || isHandle == false);

		zAny value;
		reader->GetFile()->Read(&value);

		int storedTypeId = value.typeId;

		// CScriptAny is a REF type: the handle path receives *any == null and must
		// ALLOCATE a new object (refcount 1), mirroring array's `*array = Create()`.
		// A placement-new into the null slot would dereference null and crash.
		if(isHandle)
			*any = new CScriptAny(reader->GetEngine());

	// Empty-any sentinel (see save side): a freshly-constructed any already reports
	// typeId 0, so leave it untouched -- do NOT call Store (Store asserts on 0).
		if(storedTypeId == 0)
			return;

	// Reverse the SaveTypeId translation applied on the save side.
		int asTypeId = reader->LoadTypeId(storedTypeId & asTYPEID_MASK_SEQNBR) | (storedTypeId & zTYPEID_OBJHANDLE);

//is this okay? type punning is discouraged in C++11 for some reason??
		if(asTypeId <= asTYPEID_DOUBLE)
		{
			(*any)->Store(&value.valueInt, asTypeId);
		}
		else if(asTypeId & asTYPEID_OBJHANDLE)
		{
	// LoadScriptObject writes the (AddRef'd) object into obj; StoreMove takes that
	// reference (no extra AddRef), so the load-side ref balances the store.
	// MUST use isHandle: StoreMove treats the arg as `*(void**)ref` under isHandle
	// (the object pointer we hold) but as the raw `ref` under isReference -- the
	// latter would store &obj, a dangling stack address, which is what the original
	// glue did and why any-of-handle crashed on read-back.
			void * obj{};
			reader->LoadScriptObject(&obj, (int)value.valueInt, asTypeId);

			(*any)->StoreMove(&obj, asTypeId, CScriptAny::isHandle);
		}
		else
		{
	// Value-type object payload (e.g. string). Reconstruct into engine-owned storage
	// (LoadScriptObject fills the already-constructed object), then StoreMove by
	// reference -- for a value type StoreMove does a CreateScriptObjectCopy, so it
	// copies our transient object and we release ours afterwards. Mirrors the
	// dictionary value-type load path.
			auto engine   = reader->GetEngine();
			auto typeInfo = engine->GetTypeInfoById(asTypeId);
			void * obj    = engine->CreateScriptObject(typeInfo);
			reader->LoadScriptObject(obj, (int)value.valueInt, asTypeId);

			(*any)->StoreMove(obj, asTypeId, CScriptAny::isReference);
			engine->ReleaseScriptObject(obj, typeInfo);
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

		// Length-explicit: content may contain embedded NUL bytes, so store the
		// exact byte count rather than relying on NUL termination.
		int string_id = writer->SaveString(string->data(), (uint32_t)string->size());
		writer->GetFile()->Write(&string_id);
	}

	inline void ZodiacLoad(zIZodiacReader * reader, std::string* string, int&, bool)
	{
		int string_id;
		reader->GetFile()->Read(&string_id);
		uint32_t len = 0;
		const char * data = reader->LoadString(string_id, &len);
		new(string) std::string(data ? data : "", data ? len : 0u);
	}
#endif

#ifdef SCRIPTDICTIONARY_H
// The dictionary value is a tagged union: a primitive (punned into an int64),
// a value-type object (stored as an object pointer), or an object handle. The
// primitive path is written/read verbatim; object/handle values route through
// SaveScriptObject/LoadScriptObject and are reconstructed via the PUBLIC
// CScriptDictValue::Set (we never touch the now-protected m_typeId/m_valueObj).
	inline void ZodiacSaveDictValue(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptDictValue const* dict, int typeId, const void * addressOfValue)
	{
		int64_t  value{};

		if(typeId <= AS_NAMESPACE_QUALIFIER asTYPEID_DOUBLE)
		{
	// Primitive: the dictionary stores every primitive verbatim in its 8-byte
	// value union (int64/double), and GetAddressOfValue() points at that union.
	// Read the raw bits directly -- the previous dict->Get() path crashed when
	// called from the dictionary iterator, which legitimately passes dict==null
	// (only addressOfValue is available there). The load side reconstructs the
	// same raw bits via CScriptDictValue(engine, &value, typeId), so this is
	// symmetric. (void)dict silences the now-unused parameter.
			(void)dict;
			memcpy(&value, addressOfValue, sizeof(value));
		}
		else
		{
	// GetAddressOfValue() already yields exactly what SaveScriptObject expects:
	// for a HANDLE it returns &m_valueObj (the address of the handle slot, which
	// SaveScriptObject/EnqueueNode dereference), and for a VALUE-TYPE object it
	// returns m_valueObj (the object pointer itself). Pass it verbatim in both
	// cases -- the old code dereferenced it for value types, reading the object's
	// first word as a bogus pointer and crashing in the element's ZodiacSave.
			void * objPtr = const_cast<void*>(addressOfValue);
			value = writer->SaveScriptObject(objPtr, typeId, dict);
		}

		int wt = writer->SaveTypeId(typeId) | (typeId & zTYPEID_OBJHANDLE);
		writer->GetFile()->Write(&wt);
		writer->GetFile()->Write(&value);
	}

	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptDictValue const* dict, int&)
	{
		assert(dict != nullptr);
		ZodiacSaveDictValue(writer, dict, dict->GetTypeId(), dict->GetAddressOfValue());
	}

	inline void ZodiacLoad(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CScriptDictValue* dict, int&, bool)
	{
		assert(dict != nullptr);

		int64_t  value{};
		int      typeId{};

		reader->GetFile()->Read(&typeId);
		reader->GetFile()->Read(&value);
		typeId = reader->LoadTypeId(typeId & AS_NAMESPACE_QUALIFIER asTYPEID_MASK_SEQNBR) | (typeId & zTYPEID_OBJHANDLE);

		auto engine = reader->GetEngine();
		dict->FreeValue(engine);

		if(typeId <= AS_NAMESPACE_QUALIFIER asTYPEID_DOUBLE)
		{
			new(dict) CScriptDictValue(engine, &value, typeId);
		}
		else if(typeId & AS_NAMESPACE_QUALIFIER asTYPEID_OBJHANDLE)
		{
	// Load the handle into a transient slot; Set() dereferences it and takes its
	// own reference, so we release the transient one afterwards.
			void * obj{};
			reader->LoadScriptObject(&obj, (int)value, typeId);
			dict->Set(engine, &obj, typeId);
			if(obj)
				engine->ReleaseScriptObject(obj, engine->GetTypeInfoById(typeId));
		}
		else
		{
	// Value-type object: reconstruct into engine-owned storage, then let Set()
	// copy it into the dictionary before we release our transient copy.
			auto typeInfo = engine->GetTypeInfoById(typeId);
			void * obj = engine->CreateScriptObject(typeInfo);
			reader->LoadScriptObject(obj, (int)value, typeId);
			dict->Set(engine, obj, typeId);
			engine->ReleaseScriptObject(obj, typeInfo);
		}
	}

	inline void ZodiacSave(Zodiac::zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CScriptDictionary const* dict, int&)
	{
		assert(dict != nullptr);

		auto file = writer->GetFile();

		uint32_t size = dict->GetSize();
		file->Write(&size);

		int real_type;
		for(auto itr = dict->begin(); itr != dict->end(); ++itr)
		{
			AS_NAMESPACE_QUALIFIER dictKey_t key = itr.GetKey();
			ZodiacSave(writer, &key, real_type);
			ZodiacSaveDictValue(writer, nullptr, itr.GetTypeId(), itr.GetAddressOfValue());
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

		auto engine = reader->GetEngine();
		auto file = reader->GetFile();

		uint32_t size;
		file->Read(&size);

		for(uint32_t i = 0; i < size; ++i)
		{
			AS_NAMESPACE_QUALIFIER dictKey_t key;
			int real_type;
			ZodiacLoad(reader, &key, real_type, false);

	// Reconstruct the value in a scratch CScriptDictValue, then hand it to the
	// public dictionary Set (which takes ownership via CScriptDictValue::Set).
			AS_NAMESPACE_QUALIFIER CScriptDictValue scratch;
			ZodiacLoad(reader, &scratch, real_type, false);

			int typeId = scratch.GetTypeId();
			if(typeId <= AS_NAMESPACE_QUALIFIER asTYPEID_DOUBLE)
			{
				if(typeId < AS_NAMESPACE_QUALIFIER asTYPEID_FLOAT)
				{
					asINT64 v{}; scratch.Get(engine, v);
					(*dict)->Set(key, v);
				}
				else
				{
					double v{}; scratch.Get(engine, v);
					(*dict)->Set(key, v);
				}
			}
			else
			{
	// GetAddressOfValue() returns exactly what CScriptDictValue::Set expects: for a
	// HANDLE it returns &m_valueObj and Set does `m_valueObj = *(void**)value`; for a
	// VALUE-TYPE object it returns m_valueObj and Set copies via CreateScriptObjectCopy.
	// Pass it verbatim in both cases (the old code dereferenced it for value types).
				const void * addr = scratch.GetAddressOfValue();
				void * setArg = const_cast<void*>(addr);
				(*dict)->Set(key, setArg, typeId);
			}

			scratch.FreeValue(engine);
		}
	}

// The dictionary add-on also registers a transient foreach-iterator ref type
// (`dictionaryIter`). It is never stored in a module/global, but Zodiac requires
// a save/load entry for every registered non-POD app type, so provide an
// unserializable stub -- reaching it at save time means something is wrong.
	inline void ZodiacSave(Zodiac::zIZodiacWriter *, AS_NAMESPACE_QUALIFIER CScriptDictionary::CScriptDictIter const*, int&)
	{
		throw zE_ObjectUnserializable;
	}

	inline void ZodiacLoad(zIZodiacReader *, AS_NAMESPACE_QUALIFIER CScriptDictionary::CScriptDictIter**, int&, bool)
	{
		throw zE_ObjectUnserializable;
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

	// A `ref` value local lives INLINE on AngelScript's dword-aligned script stack, so
	// `handle` may be only 4-byte aligned while CScriptHandle needs 8. Member calls on a
	// misaligned object are UB (UBSan flags them). CScriptHandle is trivially relocatable
	// and we only READ it (no mutation, no refcount change), so copy the bytes into an
	// aligned local and operate on that.
		alignas(AS_NAMESPACE_QUALIFIER CScriptHandle) unsigned char storage[sizeof(AS_NAMESPACE_QUALIFIER CScriptHandle)];
		memcpy(storage, handle, sizeof(storage));
		auto aligned = reinterpret_cast<AS_NAMESPACE_QUALIFIER CScriptHandle*>(storage);

		int typeId      = writer->SaveTypeInfo(aligned->GetType());
		int objectId{};

	// CScriptHandle::GetRef() is non-const upstream; the save signature must stay
	// `CScriptHandle const*` to match the registered zSAVE_FUNC_t. We read from the
	// aligned copy, so no mutation reaches the real (misaligned) object.
		if(aligned->GetType())
			objectId = writer->SaveScriptObject(aligned->GetRef(), aligned->GetType()->GetTypeId(), nullptr);

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
	// Mirror the (working) dictionary handle path exactly. typeInfo is the referenced
	// OBJECT type (e.g. Node); load into a transient handle slot with the OBJHANDLE bit
	// set. LoadScriptObject (isWeak=false) hands back a reference we own; Set() takes
	// its OWN reference, so we release the transient one afterwards. The earlier
	// isWeak=true / no-release variant left the handle referencing a dangling object.
			int objTypeId = typeInfo->GetTypeId() | AS_NAMESPACE_QUALIFIER asTYPEID_OBJHANDLE;
			reader->LoadScriptObject(&obj, objectId, objTypeId);
			handle->Set(obj, typeInfo);
			if(obj)
				reader->GetEngine()->ReleaseScriptObject(obj, typeInfo);
		}
		else
			handle->Set(nullptr, nullptr);
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

	// As with CScriptHandle: a `weakref<T>` value local is inline on the dword-aligned
	// script stack and may be only 4-byte aligned. Read through an aligned copy (Get()
	// and GetRefType() are const and only AddRef the referenced object, not the weakref)
	// so the member calls are not misaligned UB.
		alignas(AS_NAMESPACE_QUALIFIER CScriptWeakRef) unsigned char storage[sizeof(AS_NAMESPACE_QUALIFIER CScriptWeakRef)];
		memcpy(storage, handle, sizeof(storage));
		auto aligned = reinterpret_cast<AS_NAMESPACE_QUALIFIER CScriptWeakRef const*>(storage);

		auto object = aligned->Get();

		if(object)
		{
			auto typeInfo = aligned->GetRefType();

	// GetObjectType() was removed upstream; GetRefType() is the type of the held
	// reference and serves for both the weakref type and the referenced object.
			refTypeId = writer->SaveTypeInfo(typeInfo);
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

		(void)refTypeId;
		auto subType = reader->LoadTypeInfo(typeId, false);

		void * obj{};
		if(subType && objectId)
		{
	// Load the referenced object into a transient handle slot -- a STRONG reference
	// we own for the moment. isWeak=false so LoadScriptObject hands us a real ref.
			reader->LoadScriptObject(&obj, objectId, subType->GetTypeId() | AS_NAMESPACE_QUALIFIER asTYPEID_OBJHANDLE);
		}

	// `handle` is the in-place, engine-constructed weakref<T>: it already carries the
	// correct TEMPLATE type (m_type == weakref<T>). Reuse it via Set(newRef). The old
	// code reconstructed CScriptWeakRef(obj, GetRefType()), but GetRefType() is the
	// SUBTYPE (T), not weakref<T>, so the ctor's "type must be weakref/const_weakref"
	// assert fired. Set() also consumes the strong ref we just loaded and keeps only a
	// weak one; a null obj (objectId 0, i.e. an already-expired weakref) restores null.
		handle->Set(obj);
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
		zRegisterRefType(zodiac, AS_NAMESPACE_QUALIFIER CScriptDictionary::CScriptDictIter, "dictionaryIter", nullptr);
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
