#include "z_zodiacreader.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacwriter.h"
#include "z_zodiac.h"
#include <stdexcept>
#include <cstring>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace Zodiac
{

zCZodiacReader::zCZodiacReader(zCZodiac * parent, zIFileDescriptor * file, std::atomic<int> & progress, std::atomic<int> & total_steps) :
	m_parent(parent),
	m_file(file),
	m_mmap(file),
	m_progress(progress),
	m_totalSteps(total_steps)
{
	m_header = (zCHeader const*)(m_mmap.GetAddress() + (m_mmap.GetLength() - sizeof(m_header)));

	m_stringAddresses	= (uint32_t const*)(m_mmap.GetAddress() + m_header->stringAddressOffset);
	m_stringTable		= (char const *)(m_mmap.GetAddress() + m_header->stringTableOffset);
	m_entries			= (zCEntry const*)(m_mmap.GetAddress() + m_header->addressTableOffset);
	m_modules			= (zCModule const*)(m_mmap.GetAddress() + m_header->moduleDataOffset);
	m_typeInfo			= (zCTypeInfo const*)(m_mmap.GetAddress() + m_header->typeInfoOffset);
	m_globals			= (zCGlobalInfo const*)(m_mmap.GetAddress() + m_header->globalsOffset);
	m_loadedObjects.reset(new LoadedInfo[addressTableLength()]);
	memset(m_loadedObjects.get(), 0, addressTableLength() * sizeof(void*));

	m_progress   = 0;
	m_totalSteps = addressTableLength();
}

const char * zCZodiacReader::Verify() const
{
	void * end = (void*)(m_mmap.GetAddress() + m_mmap.GetLength());

	zCHeader check;

	if(strncmp(check.magic, m_header->magic, sizeof(check.magic)) != 0)
		return "Not a zodiac file.";

	if(m_header->saveDataByteOffset + saveDataByteLength() >  m_mmap.GetLength())
		return "Buffer overrun: save data";

//-----------------------
//  CHECK STRINGS
//----------------------
	if(m_stringAddresses + stringAddressCount() > end)
		return "Buffer overrun: string entry";

	if(m_stringTable + stringTableLength() > end)
		return "Buffer overrun: string table";

	for(uint32_t i = 0; i < stringAddressCount(); ++i)
	{
		if(m_stringAddresses[i] > stringTableLength())
		{
			return "Buffer overrun: string address";
		}
	}

//-----------------------
//  CHECK ENTRIES
//----------------------
	if(m_entries + addressTableLength() > end)
		return "Buffer overrun: address table";

	for(uint32_t i = 0; i < addressTableLength(); ++i)
	{
		if(m_header->savedObjectOffset > m_entries[i].offset || m_entries[i].offset + m_entries[i].byteLength > m_header->savedObjectLength)
		{
			return "Buffer overrun: entry save location";
		}
	}

//-----------------------
//  CHECK MODULES
//----------------------

	if(m_modules + moduleDataLength() > end)
		return "Buffer overrun: module table";

	if(m_typeInfo + typeInfoLength() > end)
		return "Buffer overrun: type info";

	if(m_globals + globalsLength() > end)
		return "Buffer overrun: global variables";

	for(uint32_t i = 0; i < moduleDataLength(); ++i)
	{
		if(m_modules[i].name > stringTableLength())
		{
			return "Buffer overrun: name in module";
		}

		if(m_header->byteCodeOffset > m_modules[i].byteCodeOffset || m_modules[i].byteCodeOffset + m_modules[i].byteCodeLength > m_header->byteCodeOffset  + m_header->byteCodeByteLength)
		{
			return "Buffer overrun: byte code in module";
		}

		if(m_modules[i].beginTypeInfo + m_modules[i].typeInfoLength > typeInfoLength())
		{
			return "Buffer overrun: asTypeInfo in module";
		}

		if(m_modules[i].beginGlobalInfo + m_modules[i].globalsLength > globalsLength())
		{
			return  "Buffer overrun: global variables in module";
		}
	}

//-----------------------
//  CHECK Globals
//----------------------
	for(uint32_t i = 0; i < globalsLength(); ++i)
	{
		if(m_globals[i].name > stringTableLength())
		{
			return "Buffer overrun: name in global";
		}

		if(m_globals[i].nameSpace > stringTableLength())
		{
			return "Buffer overrun: namespace in global";
		}

		if(m_globals[i].address > addressTableLength())
		{
			return "Buffer overrun: entry id in global";
		}

		if( m_globals[i].typeId & asTYPEID_MASK_OBJECT
		&& (m_globals[i].typeId & asTYPEID_MASK_SEQNBR) > typeInfoLength())
		{
			return "Buffer overrun: typeId in global variable";
		}
	}

//-----------------------
//  CHECK TypeInfo
//----------------------
	for(uint32_t i = 0; i < typeInfoLength(); ++i)
	{
		if(m_typeInfo[i].name > stringTableLength())
		{
			return "Buffer overrun: name in typeInfo";
		}

		if(m_typeInfo[i].nameSpace > stringTableLength())
		{
			return "Buffer overrun: namespace in typeInfo";
		}

		if(m_typeInfo[i].propertiesBegin + m_typeInfo[i].propertiesLength > propertiesLength())
		{
			return "Buffer overrun: properties in typeInfo";
		}
	}

//-----------------------
//  CHECK Properties
//----------------------
	zCProperty const* pBegin, * pEnd;
	GetProperties(-1, pBegin, pEnd);

	for(auto p = pBegin; p < pEnd; ++p)
	{
		if(p->name > stringTableLength())
		{
			return "Buffer overrun: name in property";
		}

		if( p->typeId & asTYPEID_MASK_OBJECT
		&& (p->typeId & asTYPEID_MASK_SEQNBR) > typeInfoLength())
		{
			return "Buffer overrun: typeId in property";
		}
	}

	return nullptr;
}


void zCZodiacReader::ReadSaveData(zREADER_FUNC_t func, void * userData)
{
	if(func)
	{
		zIFileDescriptor::ReadSubFile(m_file, m_header->saveDataByteOffset, m_header->saveDataByteLength);
		func(this, userData);
	}
}

void zCZodiacReader::RestoreGlobalVariables(asIScriptEngine * engine)
{
	int typeId;
	const char *name, *nameSpace;

	uint32_t noModules = engine->GetModuleCount();
	for(uint32_t i = 0; i < noModules; ++i)
	{
		auto mod = engine->GetModuleByIndex(i);
		int index = GetModuleIndex(mod->GetName(), i);

		if(index < 0) continue;

		uint32_t varCount = mod->GetGlobalVarCount();
		for(uint32_t j = 0; j < varCount; j++ )
		{
			mod->GetGlobalVar(j, &name, &nameSpace, &typeId);
			zCGlobalInfo const* global = GetGlobalVar(index, name, nameSpace, j);

			if(global)
				Restore(mod->GetAddressOfGlobalVar(j), typeId, global->address, global->typeId);
		}
	}
}

const char * zCZodiacReader::LoadModules(asIScriptEngine * engine)
{
	uint32_t maxTypeId{};
	bool     loadedByteCode{};

	m_asTypeIdFromStored.reset(new int[typeInfoLength()]);

//load modules
	for(uint32_t i = 0; i < moduleDataLength() - 1; ++i)
	{
		bool created = false;
		asIScriptModule * module = engine->GetModule(LoadString(m_modules[i].name), asGM_ONLY_IF_EXISTS);

		if(!module && m_modules[i].byteCodeLength != 0)
		{
			created = true;
			module = engine->GetModule(LoadString(m_modules[i].name), asGM_ALWAYS_CREATE);

			zIFileDescriptor::ReadSubFile sub_file(m_file, m_modules[i].byteCodeOffset, m_modules[i].byteCodeLength);
			module->LoadByteCode(m_file, nullptr);

			loadedByteCode = true;
		}

		if(!module)
		{
			return "module missing!";
		}

//get type IDs
		auto noTypes = module->GetObjectTypeCount();
		for(uint32_t j = 0; j < noTypes; ++j)
		{
			auto typeInfo = module->GetObjectTypeByIndex(j);
			auto type = GetTypeInfo(i, typeInfo->GetName(), typeInfo->GetNamespace());

			if(type != nullptr)
			{
				m_asTypeIdFromStored[type - m_typeInfo] = typeInfo->GetTypeId();

				if((m_asTypeIdFromStored[type - m_typeInfo] & asTYPEID_MASK_SEQNBR) > maxTypeId)
					maxTypeId = m_asTypeIdFromStored[type - m_typeInfo] & asTYPEID_MASK_SEQNBR;
			}
		}
	}

//get registered types
	auto noTypes = engine->GetObjectTypeCount();
	for(uint32_t j = 0; j < noTypes; ++j)
	{
		auto typeInfo = engine->GetObjectTypeByIndex(j);
		auto type = GetTypeInfo(moduleDataLength()-1, typeInfo->GetName(), typeInfo->GetNamespace());

		if(type != nullptr)
		{
			m_asTypeIdFromStored[type - m_typeInfo] = typeInfo->GetTypeId();
		}
	}

//bind imports
	if(loadedByteCode)
	{
		auto noModules = engine->GetModuleCount();

		for(uint32_t i = 0; i < noModules; ++i)
		{
			engine->GetModuleByIndex(i)->BindAllImportedFunctions();
		}
	}


//create lookup table
#if LOOKUP_TABLE
	if((maxTypeId & asTYPEID_MASK_OBJECT))
	{
		maxTypeId &= asTYPEID_MASK_SEQNBR;

		m_storedFromAsTypeId.reset(new uint32_t[maxTypeId]);
		memset(&m_storedFromAsTypeId[0], 0, sizeof(uint32_t) * maxTypeId);

		for(uint32_t i = 0; i < typeInfoLength(); ++i)
		{
			auto asTypeId = m_asTypeIdFromStored[i];

			if(asTypeId & asTYPEID_MASK_OBJECT)
			{
				assert(m_storedFromAsTypeId[m_asTypeIdFromStored[i] & asTYPEID_MASK_SEQNBR] == 0);
				m_storedFromAsTypeId[m_asTypeIdFromStored[i] & asTYPEID_MASK_SEQNBR] = i;
			}
		}
	}
#endif

	zCProperty const* pBegin, * pEnd;
	GetProperties(-1, pBegin, pEnd);

	m_properties.reset(new Property[pEnd-pBegin]);

//create property conversion table
	auto end = m_typeInfo + typeInfoLength();
	for(auto ti = m_typeInfo; ti < end; ++ti)
	{
		auto typeId     = m_asTypeIdFromStored[ti->typeId];

		if(typeId == 0)
			return "engine missing typeId found in save file!";

		auto typeInfo = engine->GetTypeInfoById(typeId);

		int asTypeId;

		auto N = ti->propertiesBegin + ti->propertiesLength;

		uint32_t start{};
		for(uint32_t i = ti->propertiesBegin; i < N; ++i)
		{
			if(m_properties[i].propertyId != ~0u)
				return "File uses duplicate property address";

			auto prop = asGetProperty(typeInfo,LoadString(pBegin[i].name), &asTypeId, start);

			if(prop == -1)
				return "Unable to restore property...";

			start = prop+1;

			m_properties[i].propertyId = prop;
			m_properties[i].readOffset = pBegin[i].offset;
			m_properties[i].readType   = m_asTypeIdFromStored[pBegin[i].typeId];
			m_properties[i].writeType  = asTypeId;
		}
	}

	return nullptr;
}

int  zCZodiacReader::asGetProperty(asITypeInfo * typeInfo, const char * pName, int * typeId, int from) const
{
	const char * name;

	int propertyCount = typeInfo->GetPropertyCount();

//most likely to be 1 after the last thing we found so try this.....
	for(int j = from; j < propertyCount; ++j)
	{
		typeInfo->GetProperty(j, &name, typeId);
		if(strcmp(pName, name) == 0)
		{
			return j;
		}
	}

	for(int j = 0; j < from; ++j)
	{
		typeInfo->GetProperty(j, &name, typeId);
		if(strcmp(pName, name) == 0)
		{
			return j;
		}
	}

	return -1;
}

int zCZodiacReader::GetModuleIndex(const char * name, uint32_t quickCheck) const
{
	if(quickCheck < moduleDataLength())
	{
		if(strcmp(LoadString(m_modules[quickCheck].name), name) == 0)
			return quickCheck;
	}


	for(uint32_t i = 0; i < moduleDataLength(); ++i)
	{
		if(strcmp(LoadString(m_modules[i].name), name) == 0)
			return i;
	}

	return -1;
}

zCGlobalInfo const* zCZodiacReader::GetGlobalVar(uint32_t module, const char * name, const char * nameSpace, uint32_t quickCheck) const
{
	auto globals	= (zCGlobalInfo const*)(m_globals + m_modules[module].beginGlobalInfo);
	auto global_end = globals + m_modules[module].globalsLength;

	if(quickCheck < m_modules[module].globalsLength)
	{
		if(strcmp(LoadString(globals[quickCheck].name), name)  == 0
		&& strcmp(LoadString(globals[quickCheck].nameSpace), nameSpace)  == 0)
		{
			return &globals[quickCheck];
		}
	}

	for(auto p = globals; p < global_end; ++p)
	{
		if(strcmp(LoadString(p->name), name)  == 0
		&& strcmp(LoadString(p->nameSpace), nameSpace)  == 0)
		{
			return p;
		}
	}

	return nullptr;
}

zCTypeInfo const* zCZodiacReader::GetTypeInfo(uint32_t module, const char * name, const char * nameSpace, uint32_t quickCheck) const
{
	auto typeInfo	= (m_typeInfo + m_modules[module].beginTypeInfo);
	auto End		= typeInfo + m_modules[module].typeInfoLength;

	if(quickCheck < m_modules[module].typeInfoLength)
	{
		if(strcmp(LoadString(typeInfo[quickCheck].name), name)  == 0
		&& strcmp(LoadString(typeInfo[quickCheck].nameSpace), nameSpace)  == 0)
		{
			return &typeInfo[quickCheck];
		}
	}

	for(auto p = typeInfo; p < End; ++p)
	{
		if(strcmp(LoadString(p->name), name)  == 0
		&& strcmp(LoadString(p->nameSpace), nameSpace)  == 0)
		{
			return p;
		}
	}

	return nullptr;
}

void zCZodiacReader::GetProperties(int typeId, zCProperty const*& begin, zCProperty const*& end) const
{
	if((uint32_t)typeId > typeInfoLength())
	{
		begin = (zCProperty const*)(m_mmap.GetAddress() + m_header->propertiesOffset);
		end   = begin + m_header->propertiesLength;
	}
	else
	{
		auto & typeInfo = m_typeInfo[typeId];
		begin = (zCProperty const*)(m_mmap.GetAddress() + m_header->propertiesOffset) + typeInfo.propertiesLength;
		end   = begin + typeInfo.propertiesLength;
	}
}

void zCZodiacReader::LoadScriptObject(void *, uint address, uint asTypeId, bool RefCount)
{
	if(address >= addressTableLength())
		throw Exception::zZE_BadObjectAddress;





}

//if typeID is an object/handle then the next thing read should be an address, otherwise it should be a value type block.
void zCZodiacReader::LoadScriptObject(void * address, uint asTypeId, bool RefCount)
{
	if(asTypeId <= asTYPEID_DOUBLE)
		m_file->Read(address, GetEngine()->GetSizeOfPrimitiveType(asTypeId));
	else if((asTypeId & asTYPEID_OBJHANDLE)
	|| (asTypeId & asTYPEID_SCRIPTOBJECT))
	{
		uint32_t handle;
		m_file->Read(&handle, sizeof(handle));
		LoadScriptObject(address, handle, asTypeId, RefCount);
	}
//the next thing is an address
	else
	{
		auto typeInfo = GetEngine()->GetTypeInfoById(asTypeId);

//funcdef next thing is an address
		if(typeInfo && typeInfo->GetFuncdefSignature())
		{
			uint32_t handle;
			m_file->Read(&handle, sizeof(handle));
			LoadScriptObject(address, handle, asTypeId, RefCount);
			return;
		}

		auto entry = m_parent->GetTypeEntryFromAsTypeId(asTypeId);

		if(!entry)	throw Exception::zZE_UnknownEncodingProtocol;

		if(!entry->onLoad)
		{
			m_file->Read(address, entry->byteLength);
		}
		else
		{
			uint32_t handle;
			m_file->Read(&handle, sizeof(handle));
			LoadScriptObject(address, handle, asTypeId, RefCount);
		}
	}
}

void * zCZodiacReader::GetObjectAddress(uint32_t id, int * zType)
{
	if(id >= addressTableLength())
	{
		throw Exception::zZE_BadObjectAddress;
		return nullptr;
	}

	if(zType) *zType = m_loadedObjects[id].zTypeId;
	return m_loadedObjects[id].ptr;
}

bool   zCZodiacReader::StoreObjectAddress(uint32_t id, void * ptr, int zType, int asType)
{
	if(id >= addressTableLength())
	{
		throw Exception::zZE_BadObjectAddress;
		return false;
	}

	 if(m_loadedObjects[id].zTypeId != 0)
		 throw Exception::zZE_DuplicateObjectAddress;

	 if(asType < 0)
		 asType = m_parent->GetAsTypeIdFromZTypeId(zType);

	 m_loadedObjects[id].ptr = ptr;
	 m_loadedObjects[id].zTypeId = (ptr == nullptr? -1 : zType);
	 m_loadedObjects[id].asTypeId = asType;

	 return true;
}



zIFileDescriptor::ReadSubFile zCZodiacReader::GetSubFile(uint32_t id)
{
	if(id >= addressTableLength())
	{
		throw Exception::zZE_BadObjectAddress;
		return zIFileDescriptor::ReadSubFile(m_file, 0, ~0u);
	}

	return zIFileDescriptor::ReadSubFile(m_file, m_entries[id].offset, m_entries[id].byteLength);
}

bool zCZodiacReader::RestorePOD(void * ptr, int dstTypeId, uint32_t address, int zTypeId)
{
	int srcTypeId = m_asTypeIdFromStored[zTypeId];

	if(srcTypeId <= asTYPEID_DOUBLE && dstTypeId <= asTYPEID_DOUBLE)
	{
		RestorePrimitive(ptr, dstTypeId, src, srcTypeId);
		return true;
	}



	if(address >= addressTableLength())
		throw Exception::zZE_BadObjectAddress;

	if(m_loadedObjects[address].asTypeId != 0
	&& m_loadedObjects[address].asTypeId != srcTypeId)
		throw Exception::zZE_ObjectRestoreTypeMismatch;

	auto native_entry = m_parent->GetTypeEntryFromAsTypeId(srcTypeId);

	if(m_loadedObjects[address].asTypeId == 0)
	{
		m_loadedObjects[address].asTypeId = srcTypeId;
		m_loadedObjects[address].zTypeId  = (native_entry? native_entry->zTypeId : 0L);
	}

	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

	auto engine = m_parent->GetEngine();

	if( (dstTypeId & ~asTYPEID_OBJHANDLE) != (srcTypeId & ~asTYPEID_OBJHANDLE) )
		throw Exception::zZE_ObjectRestoreTypeMismatch;

	auto typeInfo = engine->GetTypeInfoById(srcTypeId);

//it is an appobject...
	if(dstTypeId & asTYPEID_APPOBJECT)
	{
		if(m_loadedObjects[address].ptr)
		{
			engine->AddRefScriptObject(m_loadedObjects[address].ptr, typeInfo);
			*((void**)ptr) = m_loadedObjects[address].ptr;
			return;
		}

		auto entry = m_parent->GetTypeEntryFromAsTypeId(dstTypeId);

//POD
		if(!entry->onLoad)
			memcpy(ptr, src, entry->byteLength);
		else
		{
			m_loadedObjects[address].zTypeId = -1;

			zIFileDescriptor::ReadSubFile(m_file, m_entries[address].offset, m_entries[address].byteLength);
			m_loadedObjects[address].ptr = (entry->onLoad)(this, srcTypeId);

			*((void**)ptr) = m_loadedObjects[address].ptr;
		}

		return;
	}


	if(dstTypeId & asTYPEID_OBJHANDLE)
	{
		if(m_loadedObjects[address].ptr)
		{
			engine->AddRefScriptObject(m_loadedObjects[address].ptr, typeInfo);
			*((void**)ptr) = m_loadedObjects[address].ptr;
			return true;
		}
	}

	return false;
}

void zCZodiacReader::Restore(void * ptr, int dstTypeId, uint32_t address, int zTypeId)
{
	if(RestorePOD(ptr, dstTypeId, address, zTypeId))
		return;

	int srcTypeId = m_asTypeIdFromStored[zTypeId];

	if(address >= addressTableLength())
		throw Exception::zZE_BadObjectAddress;

	if(m_loadedObjects[address].asTypeId != 0
	&& m_loadedObjects[address].asTypeId != srcTypeId)
		throw Exception::zZE_ObjectRestoreTypeMismatch;

	auto native_entry = m_parent->GetTypeEntryFromAsTypeId(srcTypeId);

	if(m_loadedObjects[address].asTypeId == 0)
	{
		m_loadedObjects[address].asTypeId = srcTypeId;
		m_loadedObjects[address].zTypeId  = (native_entry? native_entry->zTypeId : 0L);
	}

	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

	if(srcTypeId <= asTYPEID_DOUBLE && dstTypeId <= asTYPEID_DOUBLE)
	{
		RestorePrimitive(ptr, dstTypeId, src, srcTypeId);
		return;
	}

	auto engine = m_parent->GetEngine();

	if( (dstTypeId & ~asTYPEID_OBJHANDLE) != (srcTypeId & ~asTYPEID_OBJHANDLE) )
		throw Exception::zZE_ObjectRestoreTypeMismatch;

	auto typeInfo = engine->GetTypeInfoById(srcTypeId);

//it is an appobject...
	if(dstTypeId & asTYPEID_APPOBJECT)
	{
		if(m_loadedObjects[address].ptr)
		{
			engine->AddRefScriptObject(m_loadedObjects[address].ptr, typeInfo);
			*((void**)ptr) = m_loadedObjects[address].ptr;
			return;
		}

		auto entry = m_parent->GetTypeEntryFromAsTypeId(dstTypeId);

//POD
		if(!entry->onLoad)
			memcpy(ptr, src, entry->byteLength);
		else
		{
			m_loadedObjects[address].zTypeId = -1;

			zIFileDescriptor::ReadSubFile(m_file, m_entries[address].offset, m_entries[address].byteLength);
			m_loadedObjects[address].ptr = (entry->onLoad)(this, srcTypeId);

			*((void**)ptr) = m_loadedObjects[address].ptr;
		}

		return;
	}


	if(dstTypeId & asTYPEID_OBJHANDLE)
	{
		if(m_loadedObjects[address].ptr)
		{
			engine->AddRefScriptObject(m_loadedObjects[address].ptr, typeInfo);
			*((void**)ptr) = m_loadedObjects[address].ptr;
			return;
		}

		if(typeInfo->GetFactoryCount() == 0)
		{
			///uuuuuuum
			assert(false);
		}
		else
		{
			m_loadedObjects[address].asTypeId = srcTypeId;
			m_loadedObjects[address].zTypeId  = -1;
			m_loadedObjects[address].ptr	  = engine->CreateUninitializedScriptObject( typeInfo );
			*((void**)ptr) = m_loadedObjects[address].ptr;
		//dereference it so we can assign something to it??
			ptr = *((void**)ptr);
		}
	}

	if(dstTypeId & asTYPEID_SCRIPTOBJECT)
	{
		auto & zTypeInfo = m_typeInfo[zTypeId];

		asIScriptObject * ref = (asIScriptObject*)ptr;

		const auto begin = &m_properties[zTypeInfo.propertiesBegin];
		const auto end  = begin + zTypeInfo.propertiesLength;

		for(auto p = begin; p < end; ++p)
		{
			auto typeId = m_asTypeIdFromStored[p->readType];

//POD doesn't enter the object table
			if(typeId <= asTYPEID_DOUBLE)
			{
				Restore(ref->GetAddressOfProperty(p->propertyId), p->writeType, (uint8_t*)src + p->readOffset, p->readType);
			}
		}
	}
}


template<typename T>
static T ReadPrimitive(void const* address, int asTypeId)
{
	assert(asTypeId <= asTYPEID_DOUBLE);

	switch(asTypeId)
	{
	case asTYPEID_VOID:		return 0L;
	case asTYPEID_BOOL:		return *(uint8_t*)address;
	case asTYPEID_INT8:		return *(asINT8*)address;
	case asTYPEID_INT16:	return *(asINT16*)address;
	case asTYPEID_INT32:	return *(int32_t*)address;
	case asTYPEID_INT64:	return *(asINT64*)address;
	case asTYPEID_UINT8:	return *(asBYTE*)address;
	case asTYPEID_UINT16:	return *(asWORD*)address;
	case asTYPEID_UINT32:	return *(asDWORD*)address;
	case asTYPEID_UINT64:	return *(asQWORD*)address;
	case asTYPEID_FLOAT:	return *(float*)address;
	case asTYPEID_DOUBLE:	return *(double*)address;
	default:break;
	};

	return 0;
}

//hopefully this gets optimized a lot
void zCZodiacReader::RestorePrimitive(void * dst, int dstTypeId, void const* address, int srcTypeId)
{
	assert(dstTypeId <= asTYPEID_DOUBLE);

	switch(dstTypeId)
	{
	case asTYPEID_VOID:		break;
	case asTYPEID_BOOL:		*(bool   *)dst	= ReadPrimitive<bool>	(address, srcTypeId);	break;
	case asTYPEID_INT8:		*(asINT8 *)dst	= ReadPrimitive<asINT64>(address, srcTypeId);	break;
	case asTYPEID_INT16:	*(asINT16*)dst	= ReadPrimitive<asINT64>(address, srcTypeId);	break;
	case asTYPEID_INT32:	*(int32_t*)dst	= ReadPrimitive<asINT64>(address, srcTypeId);	break;
	case asTYPEID_INT64:	*(asINT64*)dst	= ReadPrimitive<asINT64>(address, srcTypeId);	break;
	case asTYPEID_UINT8:	*(asBYTE *)dst	= ReadPrimitive<asQWORD>(address, srcTypeId);	break;
	case asTYPEID_UINT16:	*(asWORD *)dst	= ReadPrimitive<asQWORD>(address, srcTypeId);	break;
	case asTYPEID_UINT32:	*(asDWORD*)dst	= ReadPrimitive<asQWORD>(address, srcTypeId);	break;
	case asTYPEID_UINT64:	*(asQWORD*)dst	= ReadPrimitive<asQWORD>(address, srcTypeId);	break;
	case asTYPEID_FLOAT:	*(float  *)dst	= ReadPrimitive<double>	(address, srcTypeId);	break;
	case asTYPEID_DOUBLE:	*(double *)dst	= ReadPrimitive<double>	(address, srcTypeId);	break;
	};
}

}
#endif
