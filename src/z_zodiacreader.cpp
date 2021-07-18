#include "z_zodiacreader.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacwriter.h"
#include "z_zodiac.h"
#include "z_zodiaccontext.h"
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
	m_functions			= (zCFunction const*)(m_mmap.GetAddress() + m_header->functionTableOffset);
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
//  CHECK Functions
//----------------------
	if(m_functions + functionTableLength() > end)
		return "Buffer overrun: function table";

	for(uint32_t i = 0; i < functionTableLength(); ++i)
	{
		if(m_functions[i].delegateAddress > addressTableLength())
		{
			return "Buffer overrun: entry id in function";
		}

		if(m_functions[i].delegateTypeId > typeInfoLength())
		{
			return "Buffer overrun: delegate id in function";
		}

		if(m_functions[i].module > stringTableLength())
		{
			return "Buffer overrun: module id in function";
		}

		if( m_functions[i].objectType > typeInfoLength())
		{
			return "Buffer overrun: typeId in function";
		}

		if(m_functions[i].declaration > stringTableLength())
		{
			return "Buffer overrun: declaration in function";
		}

	}

//-----------------------
//  CHECK Globals
//----------------------
	if(m_globals + globalsLength() > end)
		return "Buffer overrun: global variables";

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
	}

//-----------------------
//  CHECK TypeInfo
//----------------------
	if(m_typeInfo + typeInfoLength() > end)
		return "Buffer overrun: type info";

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
				LoadScriptObject(mod->GetAddressOfGlobalVar(j), global->address, typeId);
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


bool zCZodiacReader::RestoreAppObject(void * dst, int address, uint asTypeId)
{
	if(!(asTypeId & asTYPEID_APPOBJECT))
		return false;

	if(RestoreFunction((void**)dst, address, GetEngine()->GetTypeInfoById(asTypeId)))
		return true;

///registered thing?
	auto entry = m_parent->GetTypeEntryFromAsTypeId(asTypeId);

	if(!entry)
		throw Exception::zZE_UnknownEncodingProtocol;

	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

//POD
	if(!entry->onLoad)
		memcpy(dst, src, entry->byteLength);
	else
	{
		m_loadedObjects[address].zTypeId  = entry->zTypeId;
		m_loadedObjects[address].asTypeId = entry->asTypeId;

		zIFileDescriptor::ReadSubFile(m_file, m_entries[address].offset, m_entries[address].byteLength);

		void * tmp;
		(entry->onLoad)(this, &tmp, m_loadedObjects[address].zTypeId);

		m_loadedObjects[address].ptr = tmp;
		*((void**)dst) = m_loadedObjects[address].ptr;
	}

	return true;

}

void zCZodiacReader::LoadScriptObject(void * dst, int address, uint asTypeId)
{
//object address 0 is nullptr so negative values aren't considered
	if((uint32_t)address >= addressTableLength())
		throw Exception::zZE_BadObjectAddress;

//if the owner is non-zero restore the owner
	if(m_entries[address].owner)
	{
		void * ptr;
		auto ownr = m_entries[address].owner;
		auto ownrTypeId =  m_asTypeIdFromStored[m_entries[ownr].typeId];

//load owner if it isn't loaded (check to avoid addreffing it i guess)
		if(!m_loadedObjects[ownr].ptr)
		{
			LoadScriptObject(&ptr, ownr, ownrTypeId | asTYPEID_OBJHANDLE);
			m_loadedObjects[ownr].needRelease += 1;
		}
	}

	auto & loaded = m_loadedObjects[address];

//it is a handle i suppose
	if(loaded.ptr)
	{
		assert(loaded.asTypeId & (asTYPEID_SCRIPTOBJECT|asTYPEID_APPOBJECT));

		auto engine = GetEngine();
		engine->RefCastObject(m_loadedObjects[address].ptr, engine->GetTypeInfoById(loaded.asTypeId), engine->GetTypeInfoById(asTypeId), (void**)dst);

		if(*(void**)dst == nullptr)
			throw Exception::zZE_ObjectRestoreTypeMismatch;

		return;
	}
//it loaded as nullptr
	else if(loaded.asTypeId & asTYPEID_OBJHANDLE)
	{
		*(void**)dst = nullptr;
		return;
	}

	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

	if(RestoreAppObject(dst, address, asTypeId))
		return;
	else if(asTypeId <= asTYPEID_DOUBLE && m_entries[address].typeId <= asTYPEID_DOUBLE)
	{
		RestorePrimitive(dst, asTypeId, src, m_entries[address].typeId);
		return;
	}

//should always be a script object by this point
	assert(!(asTypeId & asTYPEID_SCRIPTOBJECT));

//---------------------------------------------------------
// Script Object
//---------------------------------------------------------
	auto _typeId   = m_asTypeIdFromStored[m_entries[address].typeId];
	auto _typeInfo = GetEngine()->GetTypeInfoById(_typeId);
	auto typeInfo = GetEngine()->GetTypeInfoById(asTypeId);

	if(asTypeId & asTYPEID_OBJHANDLE)
	{
//impossible??
		assert(typeInfo->GetFactoryCount() != 0);

		m_loadedObjects[address].asTypeId = _typeId;
		m_loadedObjects[address].zTypeId  = zIZodiac::GetTypeId<asIScriptObject>();
		m_loadedObjects[address].ptr	  = GetEngine()->CreateUninitializedScriptObject(_typeInfo);
		++m_progress;

		GetEngine()->RefCastObject(m_loadedObjects[address].ptr, _typeInfo, typeInfo, (void**)dst);

		if(*(void**)dst == nullptr)
			throw Exception::zZE_ObjectRestoreTypeMismatch;

		m_loadedObjects[address].needRelease = 1;
//dereference to set up script object...
		dst = *(void**)dst;
	}
	else if(m_loadedObjects[address].ptr != dst)
	{
//populate table
		assert(m_loadedObjects[address].ptr == nullptr);

		m_loadedObjects[address].ptr	 = dst;
		m_loadedObjects[address].zTypeId = zIZodiac::GetTypeId<asIScriptObject>();
		m_loadedObjects[address].asTypeId = asTypeId;
		++m_progress;
	}

//---------------------------------------------------------
// set up script object
//---------------------------------------------------------
	auto & zTypeInfo = m_typeInfo[m_entries[address].typeId];
	asIScriptObject * ref = (asIScriptObject*)dst;

	const auto begin = &m_properties[zTypeInfo.propertiesBegin];
	const auto end  = begin + zTypeInfo.propertiesLength;

//first loop populate lookup table
	for(auto p = begin; p < end; ++p)
	{
		auto typeId   = m_asTypeIdFromStored[p->readType];
		auto offset   = ref->GetAddressOfProperty(p->propertyId);
		uint32_t read = *(uint32_t*)((uint8_t*)src + p->readOffset);

//app objects don't have an owner so it shouldn't cause an infinite loop
		if((typeId & asTYPEID_SCRIPTOBJECT) && !(typeId & asTYPEID_OBJHANDLE))
		{
			if(m_loadedObjects[read].ptr)
			{
				throw Exception::zZE_DoubleLoad;
			}

			m_loadedObjects[read].ptr	   = offset;
			m_loadedObjects[read].zTypeId  = zIZodiac::GetTypeId<asIScriptObject>();
			m_loadedObjects[read].asTypeId = ref->GetPropertyTypeId(p->propertyId);
			++m_progress;
		}
	}

//Restore object contents
	for(auto p = begin; p < end; ++p)
	{
		auto offset   = ref->GetAddressOfProperty(p->propertyId);
		auto typeId   = ref->GetPropertyTypeId(p->propertyId);

		void * read = ((uint8_t*)src + p->readOffset);

//app objects don't have an owner so it shouldn't cause an infinite loop
		RestoreScriptObject(offset, read, typeId);
	}
}

bool zCZodiacReader::RestoreFunction(void ** dst, uint32_t handle, asITypeInfo * typeInfo)
{
	if(!(typeInfo && typeInfo->GetFuncdefSignature()))
		return false;

	auto delegate = LoadFunction(handle);
	GetEngine()->RefCastObject(delegate, delegate->GetDelegateObjectType(), typeInfo, dst);

//it was addreffed in the ref cast
	if(delegate) delegate->Release();

	if(delegate && !*dst)
		throw Exception::zZE_ObjectRestoreTypeMismatch;

	return true;
}

//if typeID is an object/handle then the next thing read should be an address, otherwise it should be a value type block.
void zCZodiacReader::LoadScriptObject(void * dst, uint asTypeId)
{
	RestoreScriptObject(dst, m_mmap.GetAddress() + m_file->AbsoluteTell(), asTypeId);
}

void zCZodiacReader::RestoreScriptObject(void * dst, void const* src, uint asTypeId)
{
	if(asTypeId <= asTYPEID_DOUBLE)
	{
		memcpy(dst, src, GetEngine()->GetSizeOfPrimitiveType(asTypeId));
		return;
	}
	else if((asTypeId & asTYPEID_OBJHANDLE) || (asTypeId & asTYPEID_SCRIPTOBJECT))
	{
		LoadScriptObject(dst, *(uint32_t*)src, asTypeId);
	}
	else
	{
		auto typeInfo = GetEngine()->GetTypeInfoById(asTypeId);

//funcdef, next thing is an address
		if(typeInfo && typeInfo->GetFuncdefSignature())
		{
			RestoreFunction((void**)dst, *(uint32_t*)src, typeInfo);
			return;
		}

		auto entry = m_parent->GetTypeEntryFromAsTypeId(asTypeId);

		if(!entry)	throw Exception::zZE_UnknownEncodingProtocol;

		if(!entry->onLoad)
		{
			memcpy(dst, src, entry->byteLength);
		}
		else
		{
			LoadScriptObject(dst, *(uint32_t*)src, asTypeId);
		}
	}
}

asITypeInfo * zCZodiacReader::LoadTypeInfo(int id, bool RefCount)
{
	if(id < 0) return nullptr;

	if((uint)id > typeInfoLength())
		throw Exception::zZE_BadObjectAddress;

	auto typeId = m_asTypeIdFromStored[id];
	auto typeInfo = GetEngine()->GetTypeInfoById(typeId);

	if(typeInfo && RefCount)
		typeInfo->AddRef();

	return typeInfo;
}

int zCZodiacReader::LoadTypeId(int id)
{
	if(id < 0) return asTYPEID_VOID;

	if((uint)id > typeInfoLength())
		throw Exception::zZE_BadObjectAddress;

	return m_asTypeIdFromStored[id];
}


asIScriptFunction * zCZodiacReader::LoadFunction(int id)
{
	if(id < 0) return nullptr;
	if((uint)id > functionTableLength())
		throw Exception::zZE_BadObjectAddress;

	if(m_loadedFunctions == nullptr)
	{
		m_loadedFunctions.reset(new void*[functionTableLength()]);
		memset(&m_loadedFunctions[0], 0, sizeof(m_loadedFunctions[0]) * functionTableLength());
	}

	if(m_loadedFunctions[id] != nullptr)
	{
		asIScriptFunction * func = reinterpret_cast<asIScriptFunction*>(m_loadedFunctions[id]);
		func->AddRef();
		return func;
	}

	asIScriptFunction * func = nullptr;
	asITypeInfo * typeInfo =  LoadTypeInfo(m_functions[id].objectType, false);

	if(typeInfo)
	{
//should it be virtual???
		func = typeInfo->GetMethodByDecl(LoadString(m_functions[id].declaration));

	}
	else if(m_functions[id].module)
	{
		auto module = GetEngine()->GetModule(LoadString(m_functions[id].module), asGM_ONLY_IF_EXISTS);

		if(!module)
			throw Exception::zZE_ModuleDoesNotExist;

		func = module->GetFunctionByDecl(LoadString(m_functions[id].declaration));
	}
	else
	{
		func = GetEngine()->GetGlobalFunctionByDecl(LoadString(m_functions[id].declaration));
	}

	if(func == nullptr)
		throw Exception::zZE_BadObjectAddress;

//is it a delegate?
	if(!m_functions[id].delegateAddress)
		func->AddRef();
	else
	{
		void * delegateObject{};
		LoadScriptObject(&delegateObject, m_functions[id].delegateAddress);
		asIScriptFunction * delegate = GetEngine()->CreateDelegate(func, delegateObject);

		if(!delegate)
			throw Exception::zZE_BadFunctionInfo;
	}

	m_loadedFunctions[id] = func;
	++m_progress;
	return func;
}


asIScriptContext * zCZodiacReader::LoadContext(int id)
{
	if(id <= 0)
		return nullptr;
//object address 0 is nullptr so negative values aren't considered
	if((uint32_t)id >= addressTableLength())
		throw Exception::zZE_BadObjectAddress;

	if(m_loadedObjects[id].ptr != nullptr)
	{
		if(m_loadedObjects[id].zTypeId != zIZodiac::GetTypeId<asIScriptContext>())
			throw Exception::zZE_ObjectRestoreTypeMismatch;

		asIScriptContext * context = reinterpret_cast<asIScriptContext*>(m_loadedObjects[id].ptr);
		context->AddRef();
		return context;
	}

	asIScriptContext * context = GetEngine()->CreateContext();
	m_loadedObjects[id].ptr = context;
	m_loadedObjects[id].zTypeId = zIZodiac::GetTypeId<asIScriptContext>();

	zCEntry const* entry = &m_entries[id];
	zIFileDescriptor::ReadSubFile sub_file(m_file, entry->offset, entry->byteLength);

	int real_type{};
	ZodiacLoad(this, context, real_type);
	++m_progress;

	return context;
}

void * zCZodiacReader::LoadObject(int id, zLOAD_FUNC_t load_func, int & actualType)
{
	if(id <= 0)
		return nullptr;
//object address 0 is nullptr so negative values aren't considered
	if((uint32_t)id >= addressTableLength())
		throw Exception::zZE_BadObjectAddress;

	if(m_loadedObjects[id].ptr == nullptr && m_loadedObjects[id].zTypeId == 0)
	{
		zCEntry const* entry = &m_entries[id];
		zIFileDescriptor::ReadSubFile sub_file(m_file, entry->offset, entry->byteLength);

//ensure stack corruption if something goes wrong, (fail quickly if behavior is undefined)
		void * dst{};
		load_func(this, &dst, actualType);

		m_loadedObjects[id].ptr = dst;
		m_loadedObjects[id].zTypeId = actualType;
		m_loadedObjects[id].asTypeId = -1;

		++m_progress;
	}

	actualType = m_loadedObjects[id].zTypeId;
	return m_loadedObjects[id].ptr;
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
