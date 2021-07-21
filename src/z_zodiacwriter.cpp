#include "z_zodiacwriter.h"
#ifdef HAVE_ZODIAC
#include "z_zodiac.h"
#include "z_zodiaccontext.h"
#include <cstring>
#include <cassert>


namespace Zodiac
{

zCZodiacWriter::zCZodiacWriter(zCZodiac * parent, zIFileDescriptor * file, std::atomic<int> & progress, std::atomic<int> & totalSteps) :
	m_parent(parent),
	m_file(file),
	m_progress(progress),
	m_totalSteps(totalSteps)
{
	Node n;
	memset(&n, 0, sizeof(n));
	m_stack.push_back(n);
}


int zCZodiacWriter::EnqueueNode(Node node)
{
	if(node.asTypeId > 0)
	{
		if(node.asTypeId & asTYPEID_OBJHANDLE)
		{
			node.address = *(void const**)node.address;
			node.asTypeId &= ~asTYPEID_OBJHANDLE;
			node.owner = nullptr;
		}

		if(node.asTypeId & asTYPEID_SCRIPTOBJECT)
		{
			auto ref = (asIScriptObject*)node.address;
			node.asTypeId = ref? ref->GetTypeId() : asTYPEID_VOID;
		}
	}

	assert(node.asTypeId < 0 || (node.asTypeId & asTYPEID_OBJHANDLE) == false);

	int address{};
	int closest{};

	if(!HaveAddress(node.address, &closest, &address))
	{
		if(node.asTypeId & asTYPEID_SCRIPTOBJECT)
			node.zTypeId = zIZodiac::GetTypeId<asIScriptObject>();

		m_addressIndex.insert(m_addressIndex.begin()+closest, {node.address, m_stack.size()});
		address = m_stack.size();

		m_stack.push_back(node);

#ifndef NDEBUG
		for(uint32_t i = 1; i < m_addressIndex.size(); ++i)
		{
			assert(m_addressIndex[i-1].first < m_addressIndex[i].first);
		}
#endif

		return m_stack.size()-1;
	}

	auto & stack = m_stack[address];


	if(node.zTypeId >= 0)
	{
		if(stack.zTypeId == -1)
			stack.zTypeId = node.zTypeId;
		else if(stack.zTypeId != node.zTypeId)
			throw Exception(InconsistentObjectType);
	}

	if(node.asTypeId >= 0)
	{
		if(stack.asTypeId == -1)
			stack.asTypeId = node.asTypeId;
		else if(stack.asTypeId != node.asTypeId)
		{
			if(!(node.asTypeId & asTYPEID_SCRIPTOBJECT)
			&  !(stack.asTypeId & asTYPEID_SCRIPTOBJECT))
			{
				HaveAddress(node.address, &closest, &address);
				throw Exception(InconsistentObjectType);
			}
		}
	}

	if(node.owner)
	{
		if(stack.owner == nullptr)
			stack.owner = node.owner;
		else if(node.owner != stack.owner)
			throw Exception(InconsistentObjectOwnership);
	}

	return address;
}

bool zCZodiacWriter::HaveAddress(void const* value, int * closest, int * address) const
{
	int32_t min = 0;
	int32_t max = m_addressIndex.size()-1;

	while(max - min > 8)
	{
		int avg = (min+max)/2 + 1;

		if(m_addressIndex[avg].first < value)
		{
			min = avg;
		}
		else if(m_addressIndex[avg].first > value)
		{
			max = avg;
		}
		else
		{
			if(closest) *closest = avg;
			if(address) *address = m_addressIndex[avg].second;
			return true;
		}
	}

	for(int32_t i = min; i <= max; ++i)
	{
		if(m_addressIndex[i].first >= value)
		{
			if(closest) *closest = i;
			if(address) *address = m_addressIndex[i].second;
			return  (m_addressIndex[i].first == value);
		}
	}

	if(closest) *closest = m_addressIndex.size();
	if(address) *address = m_addressIndex.size();

	return false;
}

void zCZodiacWriter::WriteScriptObject(void const* ref, int asTypeId)
{
//should be converted to a standard reference by this point
	assert((asTYPEID_OBJHANDLE & asTypeId) == false);

	if(WriteDelegate(ref, asTypeId))
		return;

	if(asTypeId & asTYPEID_SCRIPTOBJECT )
	{
		asIScriptObject *obj = (asIScriptObject *)ref;
		asITypeInfo *type = obj->GetObjectType();
//does this undo casting?
		asTypeId = obj->GetTypeId();

		auto typeInfo = SaveTypeId(asTypeId);

		auto pBegin = &m_propertiesList[m_typeInfo[typeInfo].propertiesBegin];
		auto pEnd   = pBegin + m_typeInfo[typeInfo].propertiesLength;
		bool isPropertiesSet = m_typeInfo[typeInfo].isPropertiesSet;
		m_typeInfo[typeInfo].isPropertiesSet = true;

		auto begin = m_file->tell();

		// Store children
		for( asUINT i = 0; i < type->GetPropertyCount(); i++ )
		{
			int childId;
			const char *childName;
			int offset;
			type->GetProperty(i, &childName, &childId, nullptr, nullptr, &offset);
			auto address = obj->GetAddressOfProperty(i);

			zCProperty * prevProp{};

			if(!isPropertiesSet)
			{
				for(auto p = pBegin; p != pEnd; ++p)
				{
					if(strcmp(childName, &stringContents[p->name]) == 0)
					{
						prevProp = p;
						assert((uint32_t)SaveTypeId(childId) == p->typeId);
						p->offset	  =  (m_file->tell() - begin);
					}
				}
			}

			if(childId <= asTYPEID_DOUBLE)
			{
				m_file->Write(address,  GetEngine()->GetSizeOfPrimitiveType(childId));
			}
			else if(childId & asTYPEID_SCRIPTOBJECT)
			{
				uint32_t id_no = SaveScriptObject(address, childId, obj);
				m_file->Write(&id_no, sizeof(uint32_t));
			}
			else if(childId & asTYPEID_APPOBJECT)
			{
				if(WriteDelegate(address, childId))
					continue;

				auto entry = m_parent->GetTypeEntryFromAsTypeId(childId);

				if(!entry)
				{
					throw Exception(UnknownEncodingProtocol);
				}

				if(!entry->onSave)
				{
					m_file->Write(ref, entry->byteLength);
				}
				else
				{
					Node node;
					node.address = address;
					node.asTypeId = childId;
					node.zTypeId = -1;
					node.owner = obj;
					node.save_func = entry->onSave;

					int id_no = EnqueueNode(node);
					m_file->Write(&id_no, sizeof(uint32_t));
				}
			}

			if(prevProp)
			{
				prevProp->byteLength =  (m_file->tell() - begin) - prevProp->offset;
			}
		}
	}
	else if(asTypeId <= asTYPEID_DOUBLE)
	{
		int size = GetEngine()->GetSizeOfPrimitiveType(asTypeId);
		m_file->Write(ref, size);
	}
	else
	{
		auto entry = m_parent->GetTypeEntryFromAsTypeId(asTypeId);

		if(!entry)	throw Exception(UnknownEncodingProtocol);

		if(!entry->onSave)
		{
			m_file->Write(ref, entry->byteLength);
		}
		else
		{
			int real_type;
			(entry->onSave)(this, ref, real_type);
		}
	}
}

bool zCZodiacWriter::WriteDelegate(void const* ptr, int typeId)
{
	if(typeId & asTYPEID_OBJHANDLE)
		ptr = *(const void **)ptr;

	if((typeId & asTYPEID_APPOBJECT) == false)
		return false;

	auto typeInfo = GetEngine()->GetTypeInfoById(typeId);

	if(typeInfo->GetFuncdefSignature())
	{
		int value = SaveFunction(reinterpret_cast<asIScriptFunction const*>(ptr));
		m_file->Write(&value, sizeof(value));
		return true;
	}

	return false;
}

void zCZodiacWriter::ProcessQueue()
{
	m_file->seek(0, zFILE_END);
	m_header.savedObjectOffset = m_file->tell();

	m_addressTable.reserve(m_stack.size());

	zCEntry buffer;

	for(uint32_t i = 0; i < m_stack.size(); ++i, ++m_progress)
	{
		buffer.offset = m_file->tell();
		buffer.typeId = SaveTypeId(m_stack[i].asTypeId);
		buffer.owner = 0;
		buffer.byteLength = 0;

		if(m_stack[i].address != nullptr)
			WriteObject(m_stack[i], buffer.offset, buffer.byteLength);

		m_addressTable.push_back(buffer);
	}

	m_file->seek(0, zFILE_END);
	m_header.savedObjectLength = m_file->tell() - m_header.savedObjectOffset;

	int address{};
	int closest{};

	for(uint32_t i = 0; i < m_stack.size(); ++i)
	{
		address = 0;

		if(m_stack[i].owner != nullptr
		&& !HaveAddress(m_stack[i].owner, &closest, &address))
			throw Exception(OwnerNotEncoded);

		m_addressTable[i].owner  = address;
	}

}

void zCZodiacWriter::WriteObject(Node & n, uint32_t & offset, uint32_t & byteLength)
{
	m_file->seek(0, zFILE_END);
	offset = m_file->tell();
	zIFileDescriptor::WriteSubFile sub_file(m_file, &byteLength);
	assert(offset == m_file->SubFileOffset());

	if(n.save_func)
	{
		int real_type;
		(n.save_func)(this, n.address, real_type);
	}
	else if(n.asTypeId > 0)
	{
		WriteScriptObject(n.address, n.asTypeId);
	}
//application object
	else
	{
//this excludes funcdefs
		auto entry = m_parent->GetTypeEntryFromZTypeId(n.zTypeId);

		if(!entry)
			throw Exception(UnknownEncodingProtocol);

		if(entry->onSave)
		{
			int real_type;
			(entry->onSave)(this, n.address, real_type);
		}
		else
		{
			m_file->Write(n.address, entry->byteLength);
		}
	}
}


void zCZodiacWriter::SaveModules(asIScriptEngine * engine, bool saveByteCode, bool stripDebugInfo)
{
	std::vector<zCModule> modules(engine->GetModuleCount()+1);
	memset(&modules.back(), 0, sizeof(modules[0]));

	for(uint32_t i = 0; i < engine->GetModuleCount(); ++i)
	{
		auto module = engine->GetModuleByIndex(i);
		modules[i].name = SaveString(module->GetName());
	}

	WriteByteCode(engine, modules, saveByteCode, stripDebugInfo);
	WriteTypeInfo(engine, modules);

	m_header.globalsOffset = m_file->tell();
	WriteGlobalVariables(engine, modules);
	m_header.globalsLength = (m_file->tell() - m_header.globalsOffset) / sizeof(zCGlobalInfo);

	m_header.moduleDataOffset = m_file->tell();
	m_file->Write(modules.data(), modules.size());
	m_header.moduleDataLength = (m_file->tell() - m_header.moduleDataOffset) / sizeof(zCModule);
}

void zCZodiacWriter::WriteProperties()
{
	m_header.propertiesOffset = m_file->tell();
	m_file->Write(m_propertiesList.data(), m_propertiesList.size());
	m_header.propertiesLength = (m_file->tell() - m_header.propertiesOffset) / sizeof(zCProperty);
}

uint32_t zCZodiacWriter::CountTypes(asIScriptEngine * engine)
{
	auto N = engine->GetModuleCount();
	uint32_t total =  engine->GetObjectTypeCount() + engine->GetFuncdefCount();

	for(uint32_t i = 0; i < N; ++i)
	{
		total += engine->GetModuleByIndex(i)->GetObjectTypeCount();
	}

	return total;
}

void zCZodiacWriter::WriteTypeInfo(asIScriptEngine * engine, std::vector<zCModule> & modules)
{
	for(uint32_t i = 0; i <= asTYPEID_DOUBLE; ++i)
	{
		m_typeList.push_back(i);
	}

	for(uint32_t i = 0; i < engine->GetObjectTypeCount(); ++i)
	{
		m_typeList.push_back(engine->GetObjectTypeByIndex(i)->GetTypeId());
	}

	for(uint32_t i = 0; i < engine->GetFuncdefCount(); ++i)
	{
		m_typeList.push_back(engine->GetFuncdefByIndex(i)->GetTypeId());
	}

	for(uint32_t i = 0; i < engine->GetModuleCount(); ++i)
	{
		auto mod = engine->GetModuleByIndex(i);

		for(uint32_t j = 0; j < mod->GetObjectTypeCount(); ++j)
		{
			m_typeList.push_back(mod->GetObjectTypeByIndex(j)->GetTypeId());
		}
	}

	m_header.propertiesOffset = m_file->tell();
	m_typeInfo = WriteProperties(engine, modules);
	m_header.propertiesLength = (m_file->tell() - m_header.propertiesOffset) / sizeof(zCProperty);

	for(uint32_t i = 0; i < modules.size(); ++i)
	{
		modules[i].typeInfoLength -= modules[i].beginTypeInfo;
	}

	m_header.typeInfoOffset = m_file->tell();
	m_file->Write(m_typeInfo.data(), m_typeInfo.size());
	m_header.typeInfoLength = (m_file->tell() - m_header.typeInfoOffset) / sizeof(zCTypeInfo);
}

std::vector<zCTypeInfo> zCZodiacWriter::WriteProperties(asIScriptEngine * engine, std::vector<zCModule> & modules)
{
	std::vector<zCTypeInfo> buffer;
	buffer.reserve(CountTypes(engine) + asTYPEID_DOUBLE);

//these need to match to something so just 0 it out.
	zCTypeInfo info;
	memset(&info, 0, sizeof(info));
	for(uint32_t i = 0; i <= asTYPEID_DOUBLE; ++i)
	{
		info.typeId = i;
		buffer.push_back(info);
	}

	modules.back().beginTypeInfo = buffer.size();

	for(uint32_t i = 0; i < engine->GetObjectTypeCount(); ++i)
	{
		buffer.push_back(WriteTypeInfo(engine, nullptr, engine->GetObjectTypeByIndex(i), false));
	}

	for(uint32_t i = 0; i < engine->GetFuncdefCount(); ++i)
	{
		buffer.push_back(WriteTypeInfo(engine, nullptr, engine->GetFuncdefByIndex(i), false));
	}

	modules.back().typeInfoLength = buffer.size();

	for(uint32_t i = 0; i < engine->GetModuleCount(); ++i)
	{
		modules[i].beginTypeInfo = buffer.size();

		auto mod = engine->GetModuleByIndex(i);

		for(uint32_t j = 0; j < mod->GetObjectTypeCount(); ++j)
		{
			buffer.push_back(WriteTypeInfo(engine, mod, mod->GetObjectTypeByIndex(j), false));
		}

		modules[i].typeInfoLength = buffer.size();
	}

#ifndef NDEBUG
	assert(buffer.size() == m_typeList.size());
	for(uint32_t i = 0; i < m_typeInfo.size(); ++i)
		assert(buffer[i].typeId == (uint32_t)m_typeList[i]);
#endif

	return buffer;
}

zCTypeInfo zCZodiacWriter::WriteTypeInfo(asIScriptEngine * engine, asIScriptModule * module, asITypeInfo * type, bool registered)
{
	zCTypeInfo info;

	auto func = type-> GetFuncdefSignature();

	info.name = SaveString(type->GetName());
	info.nameSpace = SaveString(type->GetNamespace());
	info.typeId    = type->GetTypeId();
	info.isFuncDef = func != nullptr;
	info.isRegistered = registered;
	info.isPropertiesSet = 0;
	info.propertiesBegin = m_propertiesList.size();

	assert(strcmp(&stringContents[info.name], type->GetName()) == 0);

	if(!registered && !func)
	{
		const char * name;
		int typeId;
		int offset;
		zCProperty buffer;

		for(uint32_t i = 0; i < type->GetPropertyCount(); ++i)
		{
			type->GetProperty(i, &name, &typeId, nullptr, nullptr, &offset);

			buffer.name = SaveString(name);
			buffer.typeId = SaveTypeId(typeId);
			buffer.offset = 0;
			buffer.byteLength = GetByteLengthOfType(engine, module, typeId);

			m_propertiesList.push_back(buffer);
		}
	}

	info.propertiesLength  = m_propertiesList.size() - info.propertiesBegin;

	return info;
}

uint32_t zCZodiacWriter::GetByteLengthOfType(asIScriptEngine * engine, asIScriptModule * , uint typeId)
{
	if(typeId & asTYPEID_OBJHANDLE)
		return sizeof(void*);
	if(typeId <=  asTYPEID_DOUBLE)
		return engine->GetSizeOfPrimitiveType(typeId);

	asITypeInfo *type = engine->GetTypeInfoById(typeId);

	if(type && (type->GetFlags() & asOBJ_POD) )
		return type->GetSize();

//what to do??
	return 0;
}

void zCZodiacWriter::WriteByteCode(asIScriptEngine * engine, std::vector<zCModule> & modules, bool saveByteCode, bool stripDebugInfo)
{
	m_header.byteCodeOffset = m_file->tell();

	for(uint32_t i = 0; i <  engine->GetModuleCount(); ++i)
	{
		modules[i].byteCodeOffset = m_file->tell();

		if(saveByteCode)
		{
			engine->GetModuleByIndex(i)->SaveByteCode(m_file, stripDebugInfo);
		}

		modules[i].byteCodeLength = m_file->tell() - modules[i].byteCodeOffset;
	}

	m_header.byteCodeByteLength = m_file->tell() - m_header.byteCodeOffset;
}

void zCZodiacWriter::WriteGlobalVariables(asIScriptEngine * engine, std::vector<zCModule> & modules)
{
	zCGlobalInfo buffer;
	const char * name;
	const char * nameSpace;
	int typeId;
	bool isConst;

	auto origin = m_file->tell();

	for(uint32_t i = 0; i <  engine->GetModuleCount(); ++i)
	{
		modules[i].beginGlobalInfo = m_file->tell();

		auto mod = engine->GetModuleByIndex(i);

		for(uint32_t j = 0; j < mod->GetGlobalVarCount(); ++j)
		{
			mod->GetGlobalVar(j, &name, &nameSpace, &typeId, &isConst);

			buffer.name = SaveString(name);
			buffer.nameSpace = SaveString(nameSpace);
			buffer.typeId   = typeId;
			buffer.address = SaveScriptObject(mod->GetAddressOfGlobalVar(j), typeId, nullptr);

			m_file->Write(&buffer);
		}

		modules[i].globalsLength = (m_file->tell() - modules[i].beginGlobalInfo) / sizeof(zCGlobalInfo);
	}

	modules.back().beginGlobalInfo = m_file->tell();

	for(uint32_t i = 0; i < engine->GetGlobalPropertyCount(); ++i)
	{
		void * address;
		engine->GetGlobalPropertyByIndex(i, &name, &nameSpace, &typeId, &isConst, nullptr, &address);

		zCGlobalInfo buffer;
		buffer.name = SaveString(name);
		buffer.nameSpace = SaveString(nameSpace);
		buffer.typeId   = typeId;
		buffer.address = SaveScriptObject(address, typeId, nullptr);

		m_file->Write(&buffer);
	}

	modules.back().globalsLength = (m_file->tell() - modules.back().beginGlobalInfo) / sizeof(zCGlobalInfo);

	for(auto & module : modules)
	{
		module.beginGlobalInfo = (module.beginGlobalInfo - origin) / sizeof(zCGlobalInfo);
	}
}

void zCZodiacWriter::WriteAddressTable()
{
	m_header.addressTableOffset = m_file->tell();
	m_file->Write(m_addressTable.data(), m_addressTable.size());
	m_header.addressTableLength = m_addressTable.size();
}

void zCZodiacWriter::WriteFunctionTable()
{
	m_header.functionTableOffset = m_file->tell();
	m_file->Write(m_functionList.data(), m_functionList.size());
	m_header.functionTableLength = m_functionList.size();
}

void zCZodiacWriter::WriteStringTable()
{
	m_header.stringAddressOffset = m_file->tell();
	m_header.stringAddressLength = stringAddress.size();

	m_file->Write(stringAddress.data(), stringAddress.size());

	m_header.stringTableOffset = m_file->tell();
	m_header.stringTableByteLength = stringContents.size();

	m_file->Write(stringContents.data(), stringContents.size());
}

void zCZodiacWriter::WriteSaveData(zWRITER_FUNC_t func, void * userData)
{
	m_header.saveDataByteOffset = m_file->tell();
	m_header.saveDataByteLength = 0;

	if(func)
	{
		zIFileDescriptor::WriteSubFile sub_file(m_file, &m_header.saveDataByteLength);
		func(this, userData);
	}
}

void zCZodiacWriter::WriteHeader()
{
	m_file->Write(&m_header);
}

int zCZodiacWriter::SaveString(const char * string)
{
	if(string == nullptr) return 0;

	int32_t min = 0;
	int32_t max = stringAddress.size()-1;

	while(max - min > 8)
	{
		int avg = (min+max)/2 + 1;

		int cmp = strcmp(&stringContents[stringAddress[avg]], string);

		if(cmp < 0)
			min = avg;
		else if(cmp > 0)
			max = avg;
		else
			return stringAddress[avg];
	}

	for(int32_t i = min; i <= max; ++i)
	{
		int cmp = strcmp(&stringContents[stringAddress[i]], string);

		if(cmp == 0)
			return stringAddress[i];
		else if(cmp > 0)
		{
			uint32_t address = InsertString(string);
			stringAddress.insert(stringAddress.begin()+i, address);
			return address;
		}
	}

	stringAddress.push_back(InsertString(string));

	return stringAddress.back();
}

uint32_t zCZodiacWriter::InsertString(const char * string)
{
	uint32_t address = stringContents.size();

	auto len = strlen(string);
	stringContents.resize(stringContents.size()+len+1);
	strncpy(&stringContents[address], string, len);
	stringContents[address+len] = 0;

	return address;
}

int zCZodiacWriter::SaveTypeId(int typeId)
{
	typeId &= ~asTYPEID_OBJHANDLE;

	if(typeId <= asTYPEID_DOUBLE)
		return typeId;

	if(asTYPEID_TEMPLATE & typeId)
	{
		auto typeInfo = GetEngine()->GetTypeInfoById(typeId);
		if(typeInfo == nullptr) throw Exception(BadTypeId);
		auto name = typeInfo->GetName();

		for(uint32_t i = asTYPEID_DOUBLE+1; i < m_typeList.size(); ++i)
		{
			if(!(m_typeList[i] & asTYPEID_TEMPLATE))
				continue;

			typeInfo = GetEngine()->GetTypeInfoById(m_typeList[i]);

			if(typeInfo && strcmp(typeInfo->GetName(), name) == 0)
				return i;
		}

		throw Exception(BadTypeId);
		return -1;
	}

	for(uint32_t i = asTYPEID_DOUBLE+1; i < m_typeList.size(); ++i)
	{
		if(m_typeList[i] == typeId)
		{
#ifndef NDEBUG
			if(m_typeInfo.size())
			{
				assert(m_typeInfo[i].typeId == (uint32_t)typeId);
				auto typeInfo = GetEngine()->GetTypeInfoById(typeId);
				auto _name = &stringContents[m_typeInfo[i].name];
				assert(typeInfo && strcmp(_name, typeInfo->GetName()) == 0);
			}
#endif
			return i;

		}
	}

	throw Exception(BadTypeId);
	return -1;
}

int zCZodiacWriter::SaveFunction(asIScriptFunction const* func)
{
	if(m_functionList.empty())
	{
		zCFunction function;
		memset(&function, 0, sizeof(function));
		m_functionList.push_back(function);
	}

	if(func == nullptr)	return 0;

//extract
	auto type = func->GetDelegateObjectType();
	void * obj = func->GetDelegateObject();
	auto delegate = func->GetDelegateFunction();
	auto module   = func->GetModule();

	if(delegate != nullptr)
		func = delegate;


	zCFunction function;

	function.delegateAddress = type? SaveScriptObject(obj, type->GetTypeId(), nullptr) : 0;
	function.delegateTypeId = SaveTypeInfo(type);
	function.module         = module? SaveString(module->GetName()) : 0;
	function.objectType     = SaveTypeInfo(func->GetObjectType());
	function.declaration    = SaveString(func->GetDeclaration(false, true, false));

	for(uint32_t i = 0; i < m_functionList.size(); ++i)
	{
		if(0 == memcmp(&function, &m_functionList[0], sizeof(function)))
			return i;
	}

	m_functionList.push_back(function);
	return m_functionList.size()-1;
}

int zCZodiacWriter::SaveContext(asIScriptContext const* id)
{
	if(id == nullptr)
		return 0;

	Node node;

	node.address = id;
	node.asTypeId = -1;
	node.zTypeId  = zIZodiac::GetTypeId<asIScriptContext>();
	node.owner    = nullptr;
	node.save_func = reinterpret_cast<zSAVE_FUNC_t>(ZODIAC_GETSAVEFUNC(asIScriptContext));

	return EnqueueNode(node);
}

int zCZodiacWriter::SaveScriptObject(void const* t, uint32_t asTypeId, void const* ownr)
{
	if(t == nullptr)
		return 0;

	Node node;

	node.address   = t;
	node.asTypeId  = asTypeId;
	node.zTypeId   = -1;
	node.owner     = ownr;
	node.save_func = nullptr;

	return EnqueueNode(node);
}

int  zCZodiacWriter::SaveObject(void const* ptr, int zTypeId, zSAVE_FUNC_t func)
{
	assert(zTypeId != zIZodiac::GetTypeId<asIScriptObject>());

	if(ptr == nullptr)
		return 0;

	Node node;

	node.address   = ptr;
	node.asTypeId  = -1;
	node.zTypeId   = zTypeId;
	node.owner     = nullptr;
	node.save_func = func;


	return EnqueueNode(node);
}



}

#endif
