#include "z_zodiacreader.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacwriter.h"
#include "z_zodiac.h"
#include "z_zodiaccontext.h"
#include "z_zodiacexception.h"
#include <stdexcept>
#include <cstring>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include "add_on/scriptdictionary/scriptdictionary.h"

namespace Zodiac
{

zCZodiacReader::zCZodiacReader(zCZodiac * parent, zIFileDescriptor * file, std::atomic<int> & progress, std::atomic<int> & total_steps) :
	m_parent(parent),
	m_file(file),
	m_mmap(file),
	m_progress(progress),
	m_totalSteps(total_steps)
{
	m_header = (zCHeader const*)(m_mmap.GetAddress() + (m_mmap.GetLength() - sizeof(zCHeader)));

	m_stringTable		= (char const *)(m_mmap.GetAddress() + m_header->stringTableOffset);
	m_entries			= (zCEntry const*)(m_mmap.GetAddress() + m_header->addressTableOffset);
	m_modules			= (zCModule const*)(m_mmap.GetAddress() + m_header->moduleDataOffset);
	m_typeInfo			= (zCTypeInfo const*)(m_mmap.GetAddress() + m_header->typeInfoOffset);
	m_globals			= (zCGlobalInfo const*)(m_mmap.GetAddress() + m_header->globalsOffset);

	Verify();

	m_loadedObjects.reset(new LoadedInfo[addressTableLength()]);
	memset(&m_loadedObjects[0], 0, addressTableLength() * sizeof(LoadedInfo));

	m_progress   = 0;
	m_totalSteps = addressTableLength();
}

zCZodiacReader::~zCZodiacReader()
{
	return;
	if(m_loadedObjects != nullptr)
	{
		auto engine = GetEngine();

		for(uint32_t i = 0; i < addressTableLength(); ++i)
		{
			while(m_loadedObjects[i].needRelease--)
			{
				engine->ReleaseScriptObject(m_loadedObjects[i].ptr, engine->GetTypeInfoById(m_loadedObjects[i].asTypeId));
			}
		}
	}
}

void zCZodiacReader::Verify() const
{
	void * end = (void*)(m_mmap.GetAddress() + m_mmap.GetLength());

	zCHeader check;

	if(strncmp(check.magic, m_header->magic, sizeof(check.magic)) != 0)
		throw Exception("Not a zodiac file.", zE_BadFileType);

	if(m_header->saveDataByteOffset + saveDataByteLength() >  m_mmap.GetLength())
		throw Exception("save data", zE_BufferOverrun);

//-----------------------
//  CHECK STRINGS
//----------------------
	if(GetStringAddresses() + stringAddressCount() > end)
		throw Exception("string entry", zE_BufferOverrun);

	if(m_stringTable + stringTableLength() > end)
		throw Exception("string table", zE_BufferOverrun);

	for(uint32_t i = 0; i < stringAddressCount(); ++i)
	{
		if(GetStringAddresses()[i] > stringTableLength())
		{
			throw Exception("string address", zE_BufferOverrun);
		}
	}

//-----------------------
//  CHECK ENTRIES
//----------------------
	if(m_entries + addressTableLength() > end)
		throw Exception("address table", zE_BufferOverrun);

	for(uint32_t i = 0; i < addressTableLength(); ++i)
	{
		if(m_header->savedObjectOffset > m_entries[i].offset)
		{
			throw Exception("entry save location", zE_BufferOverrun);
		}

		uint32_t entry_end = (m_entries[i].offset + m_entries[i].byteLength) - m_header->savedObjectOffset;

		if(entry_end > m_header->savedObjectLength)
		{
			throw Exception("entry save size", zE_BufferOverrun);
		}
	}

//-----------------------
//  CHECK MODULES
//----------------------
	if(m_modules + moduleDataLength() > end)
		throw Exception("module table", zE_BufferOverrun);

	for(uint32_t i = 0; i < moduleDataLength(); ++i)
	{
		if(m_modules[i].name > stringTableLength())
		{
			throw Exception("name in module", zE_BufferOverrun);
		}

		if(m_header->byteCodeOffset > m_modules[i].byteCodeOffset || m_modules[i].byteCodeOffset + m_modules[i].byteCodeLength > m_header->byteCodeOffset  + m_header->byteCodeByteLength)
		{
			throw Exception("byte code in module", zE_BufferOverrun);
		}

		if(m_modules[i].beginTypeInfo + m_modules[i].typeInfoLength > typeInfoLength())
		{
			throw Exception("asTypeInfo in module", zE_BufferOverrun);
		}

		if(m_modules[i].beginGlobalInfo + m_modules[i].globalsLength > globalsLength())
		{
			throw Exception("global variables in module", zE_BufferOverrun);
		}
	}

//-----------------------
//  CHECK Functions
//----------------------
	if(GetFunctions() + functionTableLength() > end)
		throw Exception("function table", zE_BufferOverrun);

	for(uint32_t i = 0; i < functionTableLength(); ++i)
	{
		auto & function = GetFunctions()[i];
		if(function.delegateAddress > addressTableLength())
		{
			throw Exception("entry id in function", zE_BufferOverrun);
		}

		if(function.delegateTypeId > typeInfoLength())
		{
			throw Exception("delegate id in function", zE_BufferOverrun);
		}

		if(function.module > stringTableLength())
		{
			throw Exception("module id in function", zE_BufferOverrun);
		}

		if(function.objectType > typeTableLength())
		{
			throw Exception("typeId in function", zE_BufferOverrun);
		}

		if(function.declaration > stringTableLength())
		{
			throw Exception("declaration in function", zE_BufferOverrun);
		}

	}

//-----------------------
//  CHECK Globals
//----------------------
	if(m_globals + globalsLength() > end)
		throw Exception("global variables", zE_BufferOverrun);

	for(uint32_t i = 0; i < globalsLength(); ++i)
	{
		if(m_globals[i].name > stringTableLength())
		{
			throw Exception("name in global", zE_BufferOverrun);
		}

		if(m_globals[i].nameSpace > stringTableLength())
		{
			throw Exception("namespace in global", zE_BufferOverrun);
		}

		if(m_globals[i].address > addressTableLength())
		{
			throw Exception("entry id in global", zE_BufferOverrun);
		}
	}

//-----------------------
//  CHECK TypeInfo
//----------------------
	if(m_typeInfo + typeInfoLength() > end)
		throw Exception("type info", zE_BufferOverrun);

	for(uint32_t i = 0; i < typeInfoLength(); ++i)
	{
		auto & typeInfo = m_typeInfo[i];

		if(typeInfo.name > stringTableLength())
		{
			throw Exception("name in typeInfo", zE_BufferOverrun);
		}

		if(typeInfo.nameSpace > stringTableLength())
		{
			throw Exception("namespace in typeInfo", zE_BufferOverrun);
		}

		if(typeInfo.propertiesBegin + typeInfo.propertiesLength > propertiesLength())
		{
			throw Exception("properties in typeInfo", zE_BufferOverrun);
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
			throw Exception("name in property", zE_BufferOverrun);
		}

		if( p->typeId & asTYPEID_MASK_OBJECT
		&& (p->typeId & asTYPEID_MASK_SEQNBR) > typeTableLength())
		{
			throw Exception("typeId in property", zE_BufferOverrun);
		}
	}

//-----------------------
//  CHECK Templates
//----------------------
	if(GetTemplates() + templatesLength() > end)
		throw Exception("templates", zE_BufferOverrun);

	for(uint32_t i = 0; i < templatesLength(); ++i)
	{
		auto & _template = GetTemplates()[i];

		if(_template.name > stringTableLength())
		{
			throw Exception("name in template", zE_BufferOverrun);
		}

		if(_template.nameSpace > stringTableLength())
		{
			throw Exception("namespace in template", zE_BufferOverrun);
		}

		if(_template.module > stringTableLength())
		{
			throw Exception("module in template", zE_BufferOverrun);
		}

		if(_template.declaration > stringTableLength())
		{
			throw Exception("declaration in template", zE_BufferOverrun);
		}
	}
}


void zCZodiacReader::ReadSaveData(zREADER_FUNC_t func, void * userData)
{
	if(func)
	{
		zIFileDescriptor::ReadSubFile sub_file(m_file, m_header->saveDataByteOffset, m_header->saveDataByteLength);
		func(this, userData);
	}
}

void zCZodiacReader::DocumentGlobalVariables(asIScriptEngine * engine)
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

			PopulateTable( mod->GetAddressOfGlobalVar(j), global->address, typeId);
		}
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

template<typename T>
static asITypeInfo * GetEnumByIndex(T * t, int i) { return t->GetEnumByIndex(i); }

template<typename T>
static asITypeInfo * GetObjectTypeByIndex(T * t, int i) { return t->GetObjectTypeByIndex(i); }

template<typename T>
static asITypeInfo * GetTypedefByIndex(T * t, int i) { return t->GetTypedefByIndex(i); }

static asITypeInfo * GetFuncdefByIndex(asIScriptEngine * t, int i) { return t->GetFuncdefByIndex(i); }

template<typename T>
inline void zCZodiacReader::SolveTypeInfo(T * op, int i, uint32_t & quickCheck, asITypeInfo* (GetterFunc)(T*, int), uint32_t N)
{
	for(uint32_t j = 0; j < N; ++j)
	{
		auto typeInfo = GetterFunc(op, j);
		auto type = GetTypeInfo(i, typeInfo->GetName(), typeInfo->GetNamespace(), &quickCheck);

		if(type != nullptr)
		{
			assert(m_asTypeIdFromStored[type - m_typeInfo] == 0);
			m_asTypeIdFromStored[type - m_typeInfo] = typeInfo->GetTypeId();
		}
	}
}

template<typename T>
inline void zCZodiacReader::SolveTypeInfo(T * op, int i)
{
	uint32_t quickCheck{};
	SolveTypeInfo(op, i, quickCheck, &GetObjectTypeByIndex<T>, op->GetObjectTypeCount());
	SolveTypeInfo(op, i, quickCheck, &GetEnumByIndex<T>, op->GetEnumCount());
}

bool zCZodiacReader::LoadByteCode(asIScriptEngine * engine)
{
	bool loadedByteCode;

	for(uint32_t i = 0; i < moduleDataLength() - 1; ++i)
	{
//		bool created = false;
		auto moduleName = LoadString(m_modules[i].name);
		asIScriptModule * module = engine->GetModule(moduleName, asGM_ONLY_IF_EXISTS);

		if(!module && m_modules[i].byteCodeLength != 0)
		{
			module = engine->GetModule(moduleName, asGM_ALWAYS_CREATE);

			zIFileDescriptor::ReadSubFile sub_file(m_file, m_modules[i].byteCodeOffset, m_modules[i].byteCodeLength);
			module->LoadByteCode(m_file, nullptr);

			loadedByteCode = true;
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

	return loadedByteCode;
}

void zCZodiacReader::ProcessModules(asIScriptEngine * engine, bool loadedByteCode)
{
	(void)loadedByteCode;

	m_asTypeIdFromStored.resize(typeTableLength());

	for(int i = 0; i <= asTYPEID_DOUBLE; ++i)
		m_asTypeIdFromStored[i] = i;

//solve typeinfo
	for(uint32_t i = 0; i < moduleDataLength() - 1; ++i)
	{
		asIScriptModule * module = engine->GetModule(LoadString(m_modules[i].name), asGM_ONLY_IF_EXISTS);
		if(!module) throw Exception(zE_ModuleDoesNotExist);
		SolveTypeInfo(module, i);
	}

	SolveTypeInfo(engine, moduleDataLength()-1);
	uint32_t quickCheck{};
	SolveTypeInfo(engine, moduleDataLength()-1, quickCheck, &GetFuncdefByIndex, engine->GetFuncdefCount());

	SolveTemplates(engine);

	zCProperty const* pBegin{}, * pEnd{};
	GetProperties(-1, pBegin, pEnd);

	m_properties.resize(pEnd-pBegin);

//create property conversion table
	void const* end = m_typeInfo + typeInfoLength();
	for(auto ti = m_typeInfo; ti < end; ++ti)
	{
		if(ti->typeId <= asTYPEID_DOUBLE) continue;

		auto typeInfo = engine->GetTypeInfoById(m_asTypeIdFromStored[ti - m_typeInfo]);
		if(typeInfo == nullptr)	throw Exception(zE_BadTypeId);

		int asTypeId{};
		auto N = ti->propertiesBegin + ti->propertiesLength;

		uint32_t start{};
		for(uint32_t i = ti->propertiesBegin; i < N; ++i)
		{
			if(m_properties[i].propertyId != ~0u)
				throw Exception(zE_DuplicatePropertyAddress);

			auto prop = asGetProperty(typeInfo,LoadString(pBegin[i].name), &asTypeId, start);

			if(prop == -1)
				throw Exception(zE_UnableToRestoreProperty);

			start = prop+1;

			m_properties[i].propertyId = prop;
			m_properties[i].readOffset = pBegin[i].offset;
			m_properties[i].readType   = LoadTypeId(pBegin[i].typeId);
			m_properties[i].writeType  = asTypeId;

			assert(!loadedByteCode || (m_properties[i].readType&zTYPEID_OBJECT) == (m_properties[i].writeType&zTYPEID_OBJECT));
		}
	}

	DocumentGlobalVariables(engine);
}

void  zCZodiacReader::SolveTemplates(asIScriptEngine * engine)
{
	int index = typeInfoLength();
	void const* end = GetTemplates() + templatesLength();
	for(auto ti = GetTemplates(); ti < end; ++ti, ++index)
	{
		asITypeInfo * typeInfo{};

		if(!ti->module)
			typeInfo = engine->GetTypeInfoByDecl(LoadString(ti->declaration));
		else
		{
			asIScriptModule * module = engine->GetModule(LoadString(ti->module), asGM_ONLY_IF_EXISTS);
			if(!module) throw Exception(zE_ModuleDoesNotExist);
			typeInfo = module->GetTypeInfoByDecl(LoadString(ti->declaration));
		}

		if(!typeInfo)
			throw Exception(zE_BadTypeId);

		m_asTypeIdFromStored[index] = typeInfo->GetTypeId();
	}
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

zCTypeInfo const* zCZodiacReader::GetTypeInfo(uint32_t module, const char * name, const char * nameSpace, uint32_t * quickCheck) const
{
	auto typeInfo	= (m_typeInfo + m_modules[module].beginTypeInfo);
	auto End		= typeInfo + m_modules[module].typeInfoLength;

	if(nameSpace == nullptr) nameSpace = "";

	if(quickCheck && *quickCheck < m_modules[module].typeInfoLength)
	{
		auto p = &typeInfo[*quickCheck];

		auto _name = LoadString(p->name);
		auto _nameSpace = LoadString(p->nameSpace);

		if(strcmp(_name, name)  == 0
		&& strcmp(_nameSpace, nameSpace)  == 0)
		{
			*quickCheck += 1;
			return p;
		}
	}

	for(auto p = typeInfo; p < End; ++p)
	{
		auto _name = LoadString(p->name);
		auto _nameSpace = LoadString(p->nameSpace);

		if(strcmp(_name, name)  == 0
		&& strcmp(_nameSpace, nameSpace)  == 0)
		{
			if(quickCheck) *quickCheck = (p - typeInfo) + 1;
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


bool zCZodiacReader::RestoreAppObject(void * dst, int address, int asTypeId)
{
	if(!(asTypeId & asTYPEID_APPOBJECT || asTypeId & asTYPEID_TEMPLATE))
		return false;

	auto typeInfo = GetEngine()->GetTypeInfoById(asTypeId);
	if(RestoreFunction((void**)dst, address, typeInfo))
		return true;

///registered thing?
	auto entry = m_parent->GetTypeEntryFromAsTypeId(asTypeId);

	if(!entry)
		throw Exception(zE_UnknownEncodingProtocol);

	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

//POD
	if(!entry->onLoad)
		memcpy(dst, src, entry->byteLength);
	else if(typeInfo->GetFlags() & asOBJ_VALUE)
	{
		assert(LoadTypeId(m_entries[address].typeId) == asTypeId);

		assert(entry->isValueType);
		zIFileDescriptor::ReadSubFile sub_file(m_file, m_entries[address].offset, m_entries[address].byteLength);
		(entry->onLoad)(this, dst, m_loadedObjects[address].zTypeId, asTypeId & asTYPEID_OBJHANDLE);
	}
	else
	{
		auto stored_id = LoadTypeId(m_entries[address].typeId);
		assert((stored_id & asTYPEID_TEMPLATE) == (asTypeId & asTYPEID_TEMPLATE));
		assert(((asTypeId&zTYPEID_OBJECT) == stored_id));

		assert(!entry->isValueType);
		assert(!m_loadedObjects[address].beingLoaded);

		m_loadedObjects[address].zTypeId  = entry->zTypeId;
		m_loadedObjects[address].asTypeId = stored_id;
		m_loadedObjects[address].beingLoaded = true;
		m_loadedObjects[address].needRelease = 0;

		zIFileDescriptor::ReadSubFile sub_file(m_file, m_entries[address].offset, m_entries[address].byteLength);

		try
		{
			if(asTypeId & asTYPEID_OBJHANDLE)
			{
				(entry->onLoad)(this, &m_loadedObjects[address].ptr, m_loadedObjects[address].zTypeId, asTypeId & asTYPEID_OBJHANDLE);
				*((void**)dst) = m_loadedObjects[address].ptr;
			}
			else
			{
				(entry->onLoad)(this, &dst, m_loadedObjects[address].zTypeId, asTypeId & asTYPEID_OBJHANDLE);
				m_loadedObjects[address].ptr = dst;
			}
		}
		catch(Code & c)
		{
			throw Exception(c);
		}

		m_loadedObjects[address].beingLoaded = false;
		assert(m_loadedObjects[address].zTypeId  == entry->zTypeId);
		assert(m_loadedObjects[address].asTypeId = stored_id);
	}

	return true;

}

void zCZodiacReader::PopulateTable(void * dst, uint32_t address, int typeId)
{
	if(!(typeId & asTYPEID_SCRIPTOBJECT) || (typeId & asTYPEID_OBJHANDLE))
	{
		return;
	}


	if(address > addressTableLength())
		throw Exception(zE_BadObjectAddress);

	if(m_loadedObjects[address].ptr)
	{
		if(m_loadedObjects[address].ptr == dst)
			return;

		throw Exception(zE_DoubleLoad);
	}

	m_loadedObjects[address].ptr	  = dst;
	m_loadedObjects[address].zTypeId  = zIZodiac::GetTypeId<asIScriptObject>();
	m_loadedObjects[address].asTypeId = typeId;
	++m_progress;

	if(!(typeId & asTYPEID_SCRIPTOBJECT))
		return;

	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

	auto & zTypeInfo = m_typeInfo[m_entries[address].typeId];
	asIScriptObject * ref = (asIScriptObject*)dst;

	const auto begin = &m_properties[zTypeInfo.propertiesBegin];
	const auto end  = begin + zTypeInfo.propertiesLength;

//first loop populate lookup table
	for(auto p = begin; p < end; ++p)
	{
		auto typeId   = p->writeType;
		auto offset   = ref->GetAddressOfProperty(p->propertyId);
		uint32_t read = *(uint32_t*)((uint8_t*)src + p->readOffset);
		assert(p->writeType == ref->GetPropertyTypeId(p->propertyId));

//app objects don't have an owner so it shouldn't cause an infinite loop
		if((typeId & asTYPEID_SCRIPTOBJECT) && !(typeId & asTYPEID_OBJHANDLE))
		{
			PopulateTable(offset, read, p->writeType);
		}
	}
}

void zCZodiacReader::LoadScriptObject(void * dst, int address, int asTypeId, bool isWeak)
{
//object address 0 is nullptr so negative values aren't considered
	if((uint32_t)address >= addressTableLength())
		throw Exception(zE_BadObjectAddress);

//if the owner is non-zero restore the owner
	if(m_entries[address].owner && asTypeId > asTYPEID_DOUBLE)
	{
		void * ptr{};
		auto ownr = m_entries[address].owner;
		auto ownrTypeId =  LoadTypeId(m_entries[ownr].typeId);

//load owner if it isn't loaded (check to avoid addreffing it i guess)
		if(!m_loadedObjects[ownr].ptr && !m_loadedObjects[ownr].beingLoaded)
		{
			LoadScriptObject(&ptr, ownr, ownrTypeId | asTYPEID_OBJHANDLE, false);
		}
	}

	auto & loaded = m_loadedObjects[address];

//it is a handle i suppose
	if(loaded.ptr && dst != loaded.ptr)
	{
		assert((asTypeId & asTYPEID_OBJHANDLE) || (dst && *(void**)dst == nullptr));
		assert(dst != nullptr);
		assert(*(void**)dst == nullptr);
		assert(loaded.asTypeId & (asTYPEID_MASK_OBJECT));

		auto engine = GetEngine();
		auto from_type = engine->GetTypeInfoById(loaded.asTypeId);
		auto to_type   = engine->GetTypeInfoById(asTypeId);

		if(to_type->GetFuncdefSignature())
		{
			if(RestoreAppObject(dst, address, asTypeId))
				return;
		}

		assert(to_type != nullptr);

		if(asTypeId == loaded.asTypeId && asTypeId & asTYPEID_SCRIPTOBJECT)
		{
			assert(m_loadedObjects[address].zTypeId == zIZodiac::GetTypeId<asIScriptObject>());
			auto obj = (asIScriptObject *)m_loadedObjects[address].ptr;
			*(asIScriptObject**)dst = obj;
			if(!isWeak)	obj->AddRef();
		}
		else
		{
			engine->RefCastObject(m_loadedObjects[address].ptr, from_type, to_type, (void**)dst);
		}

		if(*(void**)dst == nullptr)
			throw Exception(zE_ObjectRestoreTypeMismatch);

		return;
	}
//it loaded as nullptr
	else if(loaded.asTypeId & asTYPEID_OBJHANDLE && (asTypeId & asTYPEID_OBJHANDLE))
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
	assert(asTypeId & asTYPEID_SCRIPTOBJECT);

//---------------------------------------------------------
// Script Object
//---------------------------------------------------------
	auto _typeId   = LoadTypeId(m_entries[address].typeId);
	auto _typeInfo = GetEngine()->GetTypeInfoById(_typeId);
	auto typeInfo = _typeId == asTypeId? _typeInfo : GetEngine()->GetTypeInfoById(asTypeId);

	if(asTypeId & asTYPEID_OBJHANDLE || *(void**)dst == nullptr)
	{
//impossible??
//		assert(typeInfo->GetFactoryCount() != 0);
		assert(asTypeId & asTYPEID_SCRIPTOBJECT);

		if(address == 0)
		{
			*(void**)dst = nullptr;
			return;
		}

		m_loadedObjects[address].asTypeId	 = _typeId & ~zTYPEID_OBJHANDLE;
		m_loadedObjects[address].zTypeId	 = zIZodiac::GetTypeId<asIScriptObject>();
		m_loadedObjects[address].ptr		 = GetEngine()->CreateUninitializedScriptObject(_typeInfo);
		m_loadedObjects[address].needRelease = 1 + isWeak;
		++m_progress;

		GetEngine()->RefCastObject(m_loadedObjects[address].ptr, _typeInfo, typeInfo, (void**)dst);

		if(*(void**)dst == nullptr)
			throw Exception(zE_ObjectRestoreTypeMismatch);

//dereference to set up script object...
		dst = *(void**)dst;
	}
	else if(loaded.ptr)
		assert(loaded.ptr == dst);
	else
	{
//assure table is populated
		m_loadedObjects[address].asTypeId	 = _typeId & ~zTYPEID_OBJHANDLE;
		m_loadedObjects[address].zTypeId	 = zIZodiac::GetTypeId<asIScriptObject>();
		m_loadedObjects[address].ptr		 = dst;
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
#ifndef NDEBUG
		auto name     = ref->GetPropertyName(p->propertyId);
#endif

		auto typeId   = p->writeType;
		auto offset   = ref->GetAddressOfProperty(p->propertyId);
		uint32_t read = *(uint32_t*)((uint8_t*)src + p->readOffset);
		assert(p->writeType == ref->GetPropertyTypeId(p->propertyId));

//app objects don't have an owner so it shouldn't cause an infinite loop
		PopulateTable(offset, read, typeId);
	}

//Restore object contents
	for(auto p = begin; p < end; ++p)
	{
#ifndef NDEBUG
		auto name     = ref->GetPropertyName(p->propertyId);
#endif

		auto offset   = ref->GetAddressOfProperty(p->propertyId);
		auto typeId   = ref->GetPropertyTypeId(p->propertyId);

		void * read = ((uint8_t*)src + p->readOffset);

		assert(p->writeType == typeId);

		if(*(uint32_t*)read == 31)
		{
			int break_point = 0;
			++break_point;
		}


//app objects don't have an owner so it shouldn't cause an infinite loop
		RestoreScriptObject(offset, read, typeId);
	}
}

bool zCZodiacReader::RestoreFunction(void ** dst, uint32_t handle, asITypeInfo * typeInfo)
{
	assert(handle != 0);

	if(!(typeInfo && typeInfo->GetFuncdefSignature()))
		return false;

	asIScriptFunction * delegate = LoadFunction(handle);

	if(!delegate->IsCompatibleWithTypeId(typeInfo->GetTypeId()))
		throw Exception(zE_ObjectRestoreTypeMismatch);

	*dst = delegate;

	return true;
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

		if(!entry)	throw Exception(zE_UnknownEncodingProtocol);

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

	auto typeId = LoadTypeId(id);
	auto typeInfo = GetEngine()->GetTypeInfoById(typeId);

	if(typeInfo && RefCount)
		typeInfo->AddRef();

	return typeInfo;
}

int zCZodiacReader::LoadTypeId(int id)
{
	if(id <= asTYPEID_DOUBLE) return id;

	if((uint32_t)id >= typeTableLength())
		throw Exception(zE_BadTypeId);

	return m_asTypeIdFromStored[id];
}


asIScriptFunction * zCZodiacReader::LoadFunction(int id)
{
	if(id <= 0) return nullptr;
	if((uint)id > functionTableLength())
		throw Exception(zE_BadObjectAddress);

	auto & function = GetFunctions()[id];
	auto declaration = LoadString(function.declaration);

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
	asITypeInfo * typeInfo =  LoadTypeInfo(function.objectType, false);

	if(typeInfo)
	{
//should it be virtual???
		func = typeInfo->GetMethodByDecl(declaration);

	}
	else if(function.module)
	{
		auto moduleName = LoadString(function.module);
		auto module = GetEngine()->GetModule(moduleName, asGM_ONLY_IF_EXISTS);

		if(!module)
			throw Exception(zE_ModuleDoesNotExist);

		func = module->GetFunctionByDecl(declaration);
	}
	else
	{
		func = GetEngine()->GetGlobalFunctionByDecl(declaration);
	}

	if(func == nullptr)
		throw Exception(zE_BadObjectAddress);

//is it a delegate?
	if(!function.delegateAddress)
		func->AddRef();
	else
	{
		void * delegateObject{};
		LoadScriptObject(&delegateObject, function.delegateAddress, LoadTypeId(function.delegateTypeId) | asTYPEID_OBJHANDLE);
		asIScriptFunction * delegate = GetEngine()->CreateDelegate(func, delegateObject);

		if(!delegate)
			throw Exception(zE_BadFunctionInfo);

		func = delegate;
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
		throw Exception(zE_BadObjectAddress);

	if(m_loadedObjects[id].ptr != nullptr)
	{
		if(m_loadedObjects[id].zTypeId != zIZodiac::GetTypeId<asIScriptContext>())
			throw Exception(zE_ObjectRestoreTypeMismatch);

		asIScriptContext * context = reinterpret_cast<asIScriptContext*>(m_loadedObjects[id].ptr);
		context->AddRef();
		return context;
	}

	asIScriptContext * context{};

	zCEntry const* entry = &m_entries[id];
	zIFileDescriptor::ReadSubFile sub_file(m_file, entry->offset, entry->byteLength);

	int real_type{};
	ZodiacLoad(this, &context, real_type);
	++m_progress;

	m_loadedObjects[id].ptr = context;
	m_loadedObjects[id].zTypeId = zIZodiac::GetTypeId<asIScriptContext>();

	return context;
}

void * zCZodiacReader::LoadObject(int id, zLOAD_FUNC_t load_func, int & actualType)
{
	if(id <= 0)
		return nullptr;
//object address 0 is nullptr so negative values aren't considered
	if((uint32_t)id >= addressTableLength())
		throw Exception(zE_BadObjectAddress);

	if(m_loadedObjects[id].ptr == nullptr && m_loadedObjects[id].zTypeId == 0)
	{
		zCEntry const* entry = &m_entries[id];
		zIFileDescriptor::ReadSubFile sub_file(m_file, entry->offset, entry->byteLength);

//ensure stack corruption if something goes wrong, (fail quickly if behavior is undefined)
		void * dst{};
		load_func(this, &dst, actualType, false);

		m_loadedObjects[id].ptr = dst;
		m_loadedObjects[id].zTypeId = actualType;
		m_loadedObjects[id].asTypeId = -1;

		++m_progress;
	}

	actualType = m_loadedObjects[id].zTypeId;
	return m_loadedObjects[id].ptr;
}

void	 zCZodiacReader::LoadObject(int id, void * dst, zLOAD_FUNC_t load_func, int & actualType, bool isHandle)
{
//object address 0 is nullptr so negative values aren't considered
	if(id == 0 || (uint32_t)id >= addressTableLength())
		throw Exception(zE_BadObjectAddress);

	zCEntry const* entry = &m_entries[id];
	zIFileDescriptor::ReadSubFile sub_file(m_file, entry->offset, entry->byteLength);

	load_func(this, dst, actualType, isHandle);
}

template<typename T>
static T ReadPrimitive2(void const* address, int asTypeId)
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
template<typename T>
static T ReadPrimitive(void const* address, int asTypeId)
{
	auto eax = ReadPrimitive2<T>(address, asTypeId);
	return eax;
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
