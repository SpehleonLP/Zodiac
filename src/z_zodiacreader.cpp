#include "z_zodiacreader.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacwriter.h"
#include "z_zodiac.h"
#include "z_zodiaccontext.h"
#include "z_zodiacexception.h"
#include <stdexcept>
#include <cstring>
#include <cassert>

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
	// Trust boundary. The header lives in the file's trailer; a file shorter than
	// the header would make the pointer computation below underflow, so guard it
	// BEFORE forming m_header (fixes the short-file pointer underflow).
	if(m_mmap.GetLength() < sizeof(zCHeader))
		throw Exception("file smaller than header", zE_BadFileType);

	m_header = (zCHeader const*)(m_mmap.GetAddress() + (m_mmap.GetLength() - sizeof(zCHeader)));

	// Verify() validates every file-supplied offset, length and index field
	// before any table is walked. It uses only the Get*() accessors (which
	// recompute from m_header), so it is safe to run before the member table
	// pointers below are formed.
	Verify();

	// Every table offset is now proven in-range; forming the base pointers is safe.
	m_stringTable		= GetStringTable();
	m_entries			= GetEntries();
	m_modules			= GetModules();
	m_typeInfo			= GetTypeInfo();
	m_globals			= GetGlobals();

	m_loadedObjects.reset(new LoadedInfo[addressTableLength()]);
	memset(&m_loadedObjects[0], 0, addressTableLength() * sizeof(LoadedInfo));

	m_progress   = 0;
	m_totalSteps = addressTableLength();
}

zCZodiacReader::~zCZodiacReader()
{
	//return;
	if(m_loadedObjects != nullptr)
	{
		auto engine = GetEngine();

		for(uint32_t i = 0; i < addressTableLength(); ++i)
		{
			if(!m_loadedObjects[i].needRelease) continue;

			auto typeInfo = engine->GetTypeInfoById(m_loadedObjects[i].asTypeId);

			while(m_loadedObjects[i].needRelease-- > 0)
			{
				engine->ReleaseScriptObject(m_loadedObjects[i].ptr, typeInfo);
			}
		}
	}

	// Each populated m_loadedFunctions slot holds exactly one reference: LoadFunction
	// stores a function it AddRef'd (or a delegate it created, which is born with a
	// ref). Callers that received the function got their own AddRef, so releasing the
	// cached slot once balances the creation ref (Part C #11 — was leaked).
	if(m_loadedFunctions != nullptr)
	{
		for(uint32_t i = 0; i < functionTableLength(); ++i)
		{
			if(m_loadedFunctions[i] != nullptr)
				reinterpret_cast<asIScriptFunction*>(m_loadedFunctions[i])->Release();
		}
	}
}

bool zCZodiacReader::InFile(uint64_t offset, uint64_t count, uint64_t elemSize, uint64_t fileLen)
{
	// count and elemSize both originate from 32-bit header fields / small sizeofs,
	// so bytes and offset+bytes cannot overflow uint64_t.
	uint64_t bytes = count * elemSize;
	return offset <= fileLen && bytes <= fileLen - offset;
}

void zCZodiacReader::Verify() const
{
	const uint64_t fileLen = m_mmap.GetLength();

	// A byte offset into the string table is valid iff it is a real index; the
	// terminal-NUL guarantee below then bounds the string that starts there.
	// (>= rejection: an index == length is out of range.)
	auto badStr = [&](uint32_t idx) { return idx >= stringTableLength(); };

//-----------------------
//  HEADER SELF-CONSISTENCY
//-----------------------
	zCHeader check;

	if(strncmp(check.magic, m_header->magic, sizeof(check.magic)) != 0)
		throw Exception("Not a zodiac file.", zE_BadFileType);

	// On-disk format version: no back-compat (no shipped saves), so any image
	// whose version does not match the current writer's is rejected outright.
	if(m_header->writerVersionId != zZODIAC_FORMAT_VERSION)
		throw Exception("incompatible zodiac format version", zE_BadFileType);

	if(m_header->pointerSize != check.pointerSize
	|| m_header->boolSize    != check.boolSize
	|| m_header->isBigEndian != check.isBigEndian)
		throw Exception("incompatible file format (pointer/bool size or endianness)", zE_BadFileType);

//-----------------------
//  TABLE RANGES (start AND end validated, overflow-safe)
//-----------------------
	if(!InFile(m_header->saveDataByteOffset,  saveDataByteLength(),   1,                   fileLen))
		throw Exception("save data", zE_BufferOverrun);
	if(!InFile(m_header->stringAddressOffset, stringAddressCount(),   sizeof(uint32_t),    fileLen))
		throw Exception("string addresses", zE_BufferOverrun);
	if(!InFile(m_header->stringTableOffset,   stringTableLength(),    1,                   fileLen))
		throw Exception("string table", zE_BufferOverrun);
	if(!InFile(m_header->addressTableOffset,  addressTableLength(),   sizeof(zCEntry),     fileLen))
		throw Exception("address table", zE_BufferOverrun);
	if(!InFile(m_header->savedObjectOffset,   m_header->savedObjectLength, 1,              fileLen))
		throw Exception("saved objects", zE_BufferOverrun);
	if(!InFile(m_header->moduleDataOffset,    moduleDataLength(),     sizeof(zCModule),    fileLen))
		throw Exception("module table", zE_BufferOverrun);
	if(!InFile(m_header->typeInfoOffset,      typeInfoLength(),       sizeof(zCTypeInfo),  fileLen))
		throw Exception("type info", zE_BufferOverrun);
	if(!InFile(m_header->propertiesOffset,    propertiesLength(),     sizeof(zCProperty),  fileLen))
		throw Exception("properties", zE_BufferOverrun);
	if(!InFile(m_header->globalsOffset,       globalsLength(),        sizeof(zCGlobalInfo),fileLen))
		throw Exception("globals", zE_BufferOverrun);
	if(!InFile(m_header->functionTableOffset, functionTableLength(),  sizeof(zCFunction),  fileLen))
		throw Exception("function table", zE_BufferOverrun);
	if(!InFile(m_header->templatesOffset,     templatesLength(),      sizeof(zCTemplate),  fileLen))
		throw Exception("templates", zE_BufferOverrun);
	if(!InFile(m_header->byteCodeOffset,      m_header->byteCodeByteLength, 1,             fileLen))
		throw Exception("byte code", zE_BufferOverrun);

//-----------------------
//  STRING TABLE MUST BE NUL-TERMINATED AT ITS FINAL BYTE
//  (guarantees every string that starts at a valid index ends inside the table)
//-----------------------
	if(stringTableLength() != 0 && GetStringTable()[stringTableLength() - 1] != '\0')
		throw Exception("string table not NUL-terminated", zE_BufferOverrun);

//-----------------------
//  AT LEAST ONE MODULE (the last entry is the engine's global module; the module
//  loops index moduleDataLength()-1, which underflows if the count is zero)
//-----------------------
	if(moduleDataLength() == 0)
		throw Exception("no module records", zE_BufferOverrun);

//-----------------------
//  CHECK STRING ADDRESSES
//-----------------------
	for(uint32_t i = 0; i < stringAddressCount(); ++i)
	{
		if(badStr(GetStringAddresses()[i]))
			throw Exception("string address", zE_BufferOverrun);
	}

//-----------------------
//  CHECK MODULES
//-----------------------
	for(uint32_t i = 0; i < moduleDataLength(); ++i)
	{
		auto & _module = GetModules()[i];

		if(badStr(_module.name))
			throw Exception("name in module", zE_BufferOverrun);

		if(m_header->byteCodeOffset > _module.byteCodeOffset
		|| (uint64_t)_module.byteCodeOffset + _module.byteCodeLength
		     > (uint64_t)m_header->byteCodeOffset + m_header->byteCodeByteLength)
			throw Exception("byte code in module", zE_BufferOverrun);

		if((uint64_t)_module.beginTypeInfo + _module.typeInfoLength > typeInfoLength())
			throw Exception("type info in module", zE_BufferOverrun);

		if((uint64_t)_module.beginGlobalInfo + _module.globalsLength > globalsLength())
			throw Exception("globals in module", zE_BufferOverrun);
	}

//-----------------------
//  CHECK Functions
//-----------------------
	for(uint32_t i = 0; i < functionTableLength(); ++i)
	{
		auto & function = GetFunctions()[i];

		if(function.delegateAddress >= addressTableLength())
			throw Exception("entry id in function", zE_BufferOverrun);

		if(function.delegateTypeId >= typeTableLength())
			throw Exception("delegate id in function", zE_BufferOverrun);

		if(badStr(function._module))
			throw Exception("module id in function", zE_BufferOverrun);

		if(function.objectType >= typeTableLength())
			throw Exception("typeId in function", zE_BufferOverrun);

		if(badStr(function.declaration))
			throw Exception("declaration in function", zE_BufferOverrun);
	}

//-----------------------
//  CHECK Globals
//-----------------------
	for(uint32_t i = 0; i < globalsLength(); ++i)
	{
		auto & global = GetGlobals()[i];

		if(badStr(global.name))
			throw Exception("name in global", zE_BufferOverrun);

		if(badStr(global.nameSpace))
			throw Exception("namespace in global", zE_BufferOverrun);

		if(global.address & zGLOBAL_FUNCTION_ADDRESS)
		{
			// funcdef-handle global: address indexes the FUNCTION table
			if((global.address & ~zGLOBAL_FUNCTION_ADDRESS) >= functionTableLength())
				throw Exception("function id in global", zE_BufferOverrun);
		}
		else if(global.address >= addressTableLength())
			throw Exception("entry id in global", zE_BufferOverrun);
	}

//-----------------------
//  CHECK TypeInfo
//-----------------------
	for(uint32_t i = 0; i < typeInfoLength(); ++i)
	{
		auto & typeInfo = GetTypeInfo()[i];

		if(badStr(typeInfo.name))
			throw Exception("name in typeInfo", zE_BufferOverrun);

		if(badStr(typeInfo.nameSpace))
			throw Exception("namespace in typeInfo", zE_BufferOverrun);

		if((uint64_t)typeInfo.propertiesBegin + typeInfo.propertiesLength > propertiesLength())
			throw Exception("properties in typeInfo", zE_BufferOverrun);
	}

//-----------------------
//  CHECK Properties
//-----------------------
	zCProperty const* pBegin, * pEnd;
	GetProperties(-1, pBegin, pEnd);

	for(auto p = pBegin; p < pEnd; ++p)
	{
		if(badStr(p->name))
			throw Exception("name in property", zE_BufferOverrun);

		if( p->typeId & asTYPEID_MASK_OBJECT
		&& (p->typeId & asTYPEID_MASK_SEQNBR) >= typeTableLength())
			throw Exception("typeId in property", zE_BufferOverrun);

		if(p->byteLength < 0)
			throw Exception("negative property byte length", zE_BufferOverrun);
	}

//-----------------------
//  CHECK Templates
//-----------------------
	for(uint32_t i = 0; i < templatesLength(); ++i)
	{
		auto & _template = GetTemplates()[i];

		if(badStr(_template.name))
			throw Exception("name in template", zE_BufferOverrun);

		if(badStr(_template.nameSpace))
			throw Exception("namespace in template", zE_BufferOverrun);

		if(badStr(_template._module))
			throw Exception("module in template", zE_BufferOverrun);

		if(badStr(_template.declaration))
			throw Exception("declaration in template", zE_BufferOverrun);
	}

//-----------------------
//  CHECK ENTRIES (done last: the per-property read-offset containment relies on
//  the already-validated typeInfo and properties tables)
//-----------------------
	const uint64_t regionEnd = (uint64_t)m_header->savedObjectOffset + m_header->savedObjectLength;

	for(uint32_t i = 0; i < addressTableLength(); ++i)
	{
		auto & entry = GetEntries()[i];

		// entry payload [offset, offset+byteLength) within the savedObject region
		if(entry.offset < m_header->savedObjectOffset)
			throw Exception("entry save location", zE_BufferOverrun);
		if((uint64_t)entry.offset + entry.byteLength > regionEnd)
			throw Exception("entry save size", zE_BufferOverrun);

		// owner is an entry index (0 = "no owner", which is also entry 0 == nullptr)
		if(entry.owner >= addressTableLength())
			throw Exception("owner in entry", zE_BufferOverrun);

		// entry.typeId is a STORED typeId: it indexes the zCTypeInfo table for
		// script/registered types, but template-typed entries (array<T>, grid<T>,
		// weakref<T>) and funcdef entries legitimately index the overflow region
		// past typeInfoLength() (see typeTableLength() == typeInfo + templates, and
		// the identical typeTableLength() bound used for functions at :198,:204).
		if(entry.typeId >= typeTableLength())
			throw Exception("typeId in entry", zE_BufferOverrun);

		// Property records only exist for zCTypeInfo entries; template/funcdef
		// overflow slots have none, and RestoreAppObject handles them without ever
		// indexing m_typeInfo[typeId]. Only script/registered typeInfo entries run
		// the per-property containment checks below.
		if(entry.typeId >= typeInfoLength())
			continue;

		// Every property of this entry's type is read out of the entry payload.
		// The restore loop reads an unconditional 4-byte handle slot at readOffset
		// (reader.cpp:748), plus a byteLength-sized copy. Bound both.
		auto & typeInfo = GetTypeInfo()[entry.typeId];
		const uint32_t pb = typeInfo.propertiesBegin;
		const uint32_t pl = typeInfo.propertiesLength;
		for(uint32_t k = 0; k < pl; ++k)
		{
			auto & p = pBegin[pb + k];        // pb+pl <= propertiesLength() (checked above)

			// 4-byte handle read stays inside the file buffer
			if((uint64_t)entry.offset + p.offset + sizeof(uint32_t) > fileLen)
				throw Exception("property read offset", zE_BufferOverrun);

			// full property copy stays inside this entry's payload
			if((uint64_t)p.offset + (uint32_t)p.byteLength > entry.byteLength)
				throw Exception("property extent in entry", zE_BufferOverrun);
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

			if(global)
				PopulateTable( mod->GetAddressOfGlobalVar(j), global->address & ~zGLOBAL_FUNCTION_ADDRESS, typeId);
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

			if(!global)
				continue;

			void * slot = mod->GetAddressOfGlobalVar(j);

			// A handle global may already hold an object the application created
			// while (re)building the module -- e.g. the bytecode-less restore path,
			// where the app rebuilds module + runs its init before LoadFromFile.
			// DocumentGlobalVariables/PopulateTable cannot register handle globals
			// (dst is the pointer slot, not the object), so LoadScriptObject below
			// creates a fresh object and overwrites the slot. Release the prior
			// value first, or that app-created object (and the type it pins) leaks.
			// The slot is a real, initialized global (null or a live handle), never
			// the uninitialized stack storage that context-var slots can be, so a
			// release here is safe -- which is why this cannot live in
			// LoadScriptObject itself.
			if((typeId & asTYPEID_OBJHANDLE) && *(void**)slot)
			{
				engine->ReleaseScriptObject(*(void**)slot, engine->GetTypeInfoById(typeId));
				*(void**)slot = nullptr;
			}

			LoadScriptObject(slot, global->address & ~zGLOBAL_FUNCTION_ADDRESS, typeId);
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
	bool loadedByteCode = false;

	for(uint32_t i = 0; i < moduleDataLength() - 1; ++i)
	{
//		bool created = false;
		auto moduleName = LoadString(m_modules[i].name);
		asIScriptModule * _module = engine->GetModule(moduleName, asGM_ONLY_IF_EXISTS);

		if(!_module && m_modules[i].byteCodeLength != 0)
		{
			_module = engine->GetModule(moduleName, asGM_ALWAYS_CREATE);

			zIFileDescriptor::ReadSubFile sub_file(m_file, m_modules[i].byteCodeOffset, m_modules[i].byteCodeLength);
			_module->LoadByteCode(m_file, nullptr);

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
		asIScriptModule * _module = engine->GetModule(LoadString(m_modules[i].name), asGM_ONLY_IF_EXISTS);
		if(!_module) throw Exception(zE_ModuleDoesNotExist);
		SolveTypeInfo(_module, i);
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

		asIScriptModule * _module = nullptr;
		if(ti->_module)
		{
			_module = engine->GetModule(LoadString(ti->_module), asGM_ONLY_IF_EXISTS);
			if(!_module) throw Exception(zE_ModuleDoesNotExist);
		}

		if(!_module)
			typeInfo = engine->GetTypeInfoByDecl(LoadString(ti->declaration));
		else
			typeInfo = _module->GetTypeInfoByDecl(LoadString(ti->declaration));

//Funcdefs share this overflow table but a bare funcdef name is not a valid type
//declaration for GetTypeInfoByDecl, so fall back to by-name resolution (module
//first, then engine — script funcdefs live on the engine even when declared in a
//module).
		if(!typeInfo)
		{
			const char * name = LoadString(ti->name);
			if(_module) typeInfo = _module->GetTypeInfoByName(name);
			if(!typeInfo) typeInfo = engine->GetTypeInfoByName(name);
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

zCGlobalInfo const* zCZodiacReader::GetGlobalVar(uint32_t _module, const char * name, const char * nameSpace, uint32_t quickCheck) const
{
	auto globals	= (zCGlobalInfo const*)(m_globals + m_modules[_module].beginGlobalInfo);
	auto global_end = globals + m_modules[_module].globalsLength;

	if(quickCheck < m_modules[_module].globalsLength)
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

zCTypeInfo const* zCZodiacReader::GetTypeInfo(uint32_t _module, const char * name, const char * nameSpace, uint32_t * quickCheck) const
{
	auto typeInfo	= (m_typeInfo + m_modules[_module].beginTypeInfo);
	auto End		= typeInfo + m_modules[_module].typeInfoLength;

	if(nameSpace == nullptr) nameSpace = "";

	if(quickCheck && *quickCheck < m_modules[_module].typeInfoLength)
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
		begin = (zCProperty const*)(m_mmap.GetAddress() + m_header->propertiesOffset) + typeInfo.propertiesBegin;
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

	// Expose the enclosing owner (already restored) to the entry's onLoad, so an
	// app entry can attach the object it builds to the object that owns it. Owner
	// index 0 == none (same sentinel as :929). The guard save/restores across both
	// onLoad branches AND across a nested load re-entering RestoreAppObject, and is
	// exception-safe (the ref branch below can throw).
	uint32_t ownerAddr = m_entries[address].owner;
	void * savedOwner = m_currentOwner;
	m_currentOwner = ownerAddr ? m_loadedObjects[ownerAddr].ptr : nullptr;
	struct OwnerGuard { void *& slot; void * prev; ~OwnerGuard() { slot = prev; } }
		ownerGuard{ m_currentOwner, savedOwner };

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
		assert(m_loadedObjects[address].asTypeId == stored_id);
	}

	return true;

}

void zCZodiacReader::PopulateTable(void * dst, uint32_t address, int typeId)
{
	if(!(typeId & asTYPEID_SCRIPTOBJECT) || (typeId & asTYPEID_OBJHANDLE))
	{
		return;
	}


	if(address >= addressTableLength())
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

//Verify() skips per-property validation for typeIds in the template band
//[typeInfoLength, typeTableLength) on the promise they never index m_typeInfo. Such
//an entry.typeId would misread a zCTypeInfo-shaped slice of another table and walk
//m_properties out of bounds; reject it before it is used to index m_typeInfo.
	if(m_entries[address].typeId >= typeInfoLength())
		throw Exception(zE_BadTypeId);

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
//The object-graph walk is iterative: LoadScriptObjectImpl does the synchronous
//node visit (create/register/alias) and pushes each object's contents-restore onto
//m_restoreWork instead of recursing. Only the OUTERMOST call drains the worklist,
//so an arbitrarily deep handle chain is bounded by heap, not the C stack. Nested
//(re-entrant) calls -- owner restoration, handle properties, addon onLoad glue --
//see m_draining==true and merely enqueue, letting this loop drain them.
	bool top = !m_draining;
	m_draining = true;

	LoadScriptObjectImpl(dst, address, asTypeId, isWeak);

	if(top)
	{
		try
		{
			while(!m_restoreWork.empty())
			{
				RestoreWork item = m_restoreWork.back();
				m_restoreWork.pop_back();
				RestoreScriptObjectContents(item.ptr, item.address);
			}
		}
		catch(...)
		{
			m_restoreWork.clear();
			m_draining = false;
			throw;
		}
		m_draining = false;
	}
}

void zCZodiacReader::LoadScriptObjectImpl(void * dst, int address, int asTypeId, bool isWeak)
{
//Funcdef handles (member or global) are stored as FUNCTION-table indices, not
//object-address indices, so they must be resolved through the function table and
//never run the object-address machinery below (whose guard/entry lookups would
//misinterpret the index).
	if(asTypeId & asTYPEID_APPOBJECT)
	{
		auto ti = GetEngine()->GetTypeInfoById(asTypeId);
		if(ti && ti->GetFuncdefSignature())
		{
			if(address == 0)
				*(void**)dst = nullptr;
			else
				RestoreFunction((void**)dst, address, ti);
			return;
		}
	}

//A null handle (object id 0) to an app/template ref type restores to null. The
//scriptobject-handle path handles this later (via the loaded==null branch), but
//app/template handles otherwise fall into RestoreAppObject -> RestoreFunction,
//which asserts address != 0. Short-circuit all null handles uniformly here.
	if(address == 0 && (asTypeId & asTYPEID_OBJHANDLE))
	{
		*(void**)dst = nullptr;
		return;
	}

//object address 0 is nullptr so negative values aren't considered
	if((uint32_t)address >= addressTableLength())
		throw Exception(zE_BadObjectAddress);

//if the owner is non-zero restore the owner
	if(m_entries[address].owner && asTypeId > asTYPEID_DOUBLE)
	{
		void * ptr{};
		auto ownr = m_entries[address].owner;

//A self-owning entry (owner == its own address) would re-enter LoadScriptObjectImpl
//on the same, not-yet-registered index below and recurse until the C stack overflows.
//Verify() only bounds owner < addressTableLength(); it never rejects self-reference.
//Reject it here as malformed input rather than crash.
		if(ownr == (uint32_t)address)
			throw Exception(zE_BadObjectAddress);
		auto ownrTypeId =  LoadTypeId(m_entries[ownr].typeId);

//A mutual/longer owner cycle (A owns B, B owns A; or A->B->C->A) isn't caught by the
//self-owner guard above, and beingLoaded isn't set during this owner walk (only inside
//RestoreAppObject), so it would ping-pong through LoadScriptObjectImpl until the C stack
//overflows. ownerResolving marks addresses on the *current* owner-resolve chain; re-entering
//one rejects the cycle regardless of its length.
		if(m_loadedObjects[ownr].ownerResolving)
			throw Exception(zE_BadObjectAddress);

//load owner if it isn't loaded (check to avoid addreffing it i guess)
		if(!m_loadedObjects[ownr].ptr && !m_loadedObjects[ownr].beingLoaded)
		{
			m_loadedObjects[address].ownerResolving = true;
			LoadScriptObject(&ptr, ownr, ownrTypeId | asTYPEID_OBJHANDLE, false);
			m_loadedObjects[address].ownerResolving = false;
		}
	}

	auto & loaded = m_loadedObjects[address];

//it is a handle i suppose
	if(loaded.ptr && dst != loaded.ptr)
	{
		assert(dst && *(void**)dst == nullptr);
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
			m_loadedObjects[address].needRelease += isWeak;
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

//an enum value is just its underlying integer, stored inline like a primitive
	if(auto enumType = GetEngine()->GetTypeInfoById(asTypeId); enumType && (enumType->GetFlags() & asOBJ_ENUM))
	{
		memcpy(dst, src, enumType->GetSize());
		return;
	}

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
//The object now exists and is registered in m_loadedObjects (above), which is what
//breaks cycles: any back-edge reaching this address finds loaded.ptr set and aliases
//it (the branch at the top) rather than descending again. Its own property contents
//are DEFERRABLE -- enqueue them for the top-level drain instead of recursing, so a
//deep chain does not grow the C stack. dst here points at the actual script object
//(handles were dereferenced above).
	m_restoreWork.push_back(RestoreWork{ dst, (uint32_t)address });
}

//Restore one already-registered script object's property contents. Split out of
//LoadScriptObjectImpl's tail so it can be driven from the explicit work-stack; the
//two loops are byte-for-byte the original inline logic.
void zCZodiacReader::RestoreScriptObjectContents(void * dst, uint32_t address)
{
	void const* src = m_mmap.GetAddress() + m_entries[address].offset;

//Reject a template-band entry.typeId that Verify() admitted without property
//validation before it is used to index m_typeInfo/m_properties (see PopulateTable).
	if(m_entries[address].typeId >= typeInfoLength())
		throw Exception(zE_BadTypeId);

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

//A primitive member whose stored (source) width differs from the live (destination)
//width must be CONVERTED src->dst, like top-level primitives (RestorePrimitive), not
//memcpy'd at the destination width -- copying the wider destination width out of a
//narrower stored slot over-reads the source buffer. Non-primitives keep the original
//path (their src type is an address index, not an inline value).
		if(typeId <= asTYPEID_DOUBLE)
			RestorePrimitive(offset, typeId, read, p->readType);
		else
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

//an enum value is just its underlying integer, stored inline like a primitive
		if(typeInfo && (typeInfo->GetFlags() & asOBJ_ENUM))
		{
			memcpy(dst, src, typeInfo->GetSize());
			return;
		}

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
	if((uint)id >= functionTableLength())
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
	else if(function._module)
	{
		auto moduleName = LoadString(function._module);
		auto _module = GetEngine()->GetModule(moduleName, asGM_ONLY_IF_EXISTS);

		if(!_module)
			throw Exception(zE_ModuleDoesNotExist);

		func = _module->GetFunctionByDecl(declaration);
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
		auto delegateType = LoadTypeId(function.delegateTypeId);
		LoadScriptObject(&delegateObject, function.delegateAddress, delegateType | asTYPEID_OBJHANDLE);
		asIScriptFunction * delegate = GetEngine()->CreateDelegate(func, delegateObject);

//The handle load above AddRef'd delegateObject for this local (untracked by the
//loaded-objects needRelease accounting); CreateDelegate takes its own reference,
//so release ours or the delegate object leaks for the reader's lifetime.
		if(delegateObject)
			GetEngine()->ReleaseScriptObject(delegateObject, GetEngine()->GetTypeInfoById(delegateType));

		if(!delegate)
			throw Exception(zE_BadFunctionInfo);

		func = delegate;
	}

//At this point `func` carries exactly one reference (the AddRef above for a
//plain function, or CreateDelegate's initial count). The function table RETAINS
//that reference — ~zCZodiacReader releases every populated slot — so the caller
//must get its OWN reference, exactly as the cached-slot path (above) does. Without
//this the caller's slot and the dtor would each release one AddRef → under-ref /
//use-after-free (acute for delegates, whose only ref would be the table's).
	m_loadedFunctions[id] = func;
	func->AddRef();
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
