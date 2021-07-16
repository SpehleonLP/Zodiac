#include "z_zodiacwriter.h"
#ifdef HAVE_ZODIAC
#include "z_zodiac.h"
#include "z_zodiaccontext.h"
#include <cstring>
#include <cassert>


namespace Zodiac
{

void ZodiacSave(zIZodiacWriter *, asIScriptContext *, int *);
void ZodiacSave(zIZodiacWriter *, asIScriptObject *, int *);

zCZodiacWriter::zCZodiacWriter(zCZodiac * parent, zIFileDescriptor * file, std::atomic<int> & progress, std::atomic<int> & totalSteps) :
	m_parent(parent),
	m_file(file),
	m_progress(progress),
	m_totalSteps(totalSteps)
{
}


int zCZodiacWriter::EnqueueNode(Node node)
{
	int address{};
	int closest{};

	if(!HaveAddress(node.address, &closest, &address))
	{
		if(node.asTypeId & asTYPEID_SCRIPTOBJECT)
			node.zTypeId = zIZodiac::GetTypeId<asIScriptObject>();

		m_addressIndex.insert(m_addressIndex.begin()+closest, {node.address, m_stack.size()});
		m_stack.push_back(node);

		return m_stack.size()-1;
	}

	auto & stack = m_stack[address];


	if(node.zTypeId >= 0)
	{
		if(stack.zTypeId == -1)
			stack.zTypeId = node.zTypeId;
		else if(stack.zTypeId != node.zTypeId)
			throw Exception::zZE_InconsistentObjectType;
	}

	if(node.asTypeId >= 0)
	{
		if(stack.asTypeId == -1)
			stack.asTypeId = node.asTypeId;
		else if(stack.asTypeId != node.asTypeId)
		{
			if(!(node.asTypeId & asTYPEID_SCRIPTOBJECT)
			&  !(stack.asTypeId & asTYPEID_SCRIPTOBJECT))
				throw Exception::zZE_InconsistentObjectType;
		}
	}

	if(node.owner)
	{
		if(stack.owner == nullptr)
			stack.owner = node.owner;
		else if(node.owner != stack.owner)
			throw Exception::zZE_InconsistentObjectOwnership;
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
			*address = m_addressIndex[avg].second;
			return true;
		}
	}

	for(int32_t i = min; i <= max; ++i)
	{
		if(m_addressIndex[i].first >= value)
		{
			if(closest) *closest = i;
			if(address) *address = m_addressIndex.size();
			return  (m_addressIndex[i].first == value);
		}
	}

	if(closest) *closest = 0;
	if(address) *address = 0;

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

		// Store children
		for( asUINT i = 0; i < type->GetPropertyCount(); i++ )
		{
			int childId;
			const char *childName;
			type->GetProperty(i, &childName, &childId);
			auto address = obj->GetAddressOfProperty(i);

			if(childId <= asTYPEID_DOUBLE)
			{
				m_file->Write(address,  GetEngine()->GetSizeOfPrimitiveType(childId));
			}
			else if(childId & asTYPEID_SCRIPTOBJECT)
			{
				uint32_t id_no = SaveScriptObject(address, childId, this);
				m_file->Write(&id_no, sizeof(uint32_t));
			}
			else if(childId & asTYPEID_APPOBJECT)
			{
				if(WriteDelegate(address, childId))
					continue;

				auto entry = m_parent->GetTypeEntryFromAsTypeId(childId);

				if(!entry)	throw Exception::zZE_UnknownEncodingProtocol;

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
					node.owner = this;
					node.save_func = entry->onSave;

					int id_no = EnqueueNode(node);
					m_file->Write(&id_no, sizeof(uint32_t));
				}
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

		if(!entry)	throw Exception::zZE_UnknownEncodingProtocol;

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
	m_addressTable.reserve(m_stack.size());

	zCEntry buffer;

	for(uint32_t i = 0; i < m_stack.size(); ++i, ++m_progress)
	{
		buffer.offset = m_file->tell();
		buffer.typeId = SaveTypeId(m_stack[i].asTypeId);
		buffer.owner = 0;
		WriteObject(m_stack[i], &buffer.byteLength);

		m_addressTable.push_back(buffer);
	}

	int address{};
	int closest{};

	for(uint32_t i = 0; i < m_stack.size(); ++i)
	{
		address = 0;

		if(m_stack[i].owner != nullptr
		&& !HaveAddress(m_stack[i].owner, &closest, &address))
			throw Exception::zZE_OwnerNotEncoded;

		m_addressTable[i].owner  = address;
	}
}

void zCZodiacWriter::WriteObject(Node & n, uint32_t * byteLength)
{
	m_file->seek(0, zFILE_END);
	zIFileDescriptor::WriteSubFile sub_file(m_file, byteLength);

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
			throw Exception::zZE_UnknownEncodingProtocol;

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

	WriteByteCode(engine, modules, saveByteCode, stripDebugInfo);

	m_header.typeInfoOffset = m_file->tell();
	WriteTypeInfo(engine, modules);
	m_header.typeInfoLength = (m_file->tell() - m_header.typeInfoOffset) / sizeof(zCTypeInfo);

	m_header.globalsOffset = m_file->tell();
	WriteGlobalVariables(engine, modules);
	m_header.globalsLength = (m_file->tell() - m_header.globalsOffset) / sizeof(zCGlobalInfo);

	m_header.moduleDataOffset = m_file->tell();
	m_file->Write(modules.data(), modules.size());
	m_header.moduleDataLength = (m_file->tell() - m_header.moduleDataOffset) / sizeof(zCModule);
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
	std::vector<zCTypeInfo> buffer(CountTypes(engine));
	zCTypeInfo * p = buffer.data();

//these need to match to something so just 0 it out.
	zCTypeInfo info;
	memset(&info, 0, sizeof(info));
	for(uint32_t i = 0; i <= asTYPEID_DOUBLE; ++i)
	{
		m_file->Write(&info);
		m_typeList.push_back(i);
	}

	for(uint32_t i = 0; i < engine->GetModuleCount(); ++i)
	{
		modules[i].beginTypeInfo = p - buffer.data();

		auto mod = engine->GetModuleByIndex(i);

		for(uint32_t j = 0; j < mod->GetObjectTypeCount(); ++j, ++p)
		{
			*p = WriteTypeInfo(engine, mod, mod->GetObjectTypeByIndex(j), false);
		}

		modules[i].typeInfoLength = (p - buffer.data()) - modules[i].beginTypeInfo;
	}

	modules.back().beginTypeInfo = p - buffer.data();

	for(uint32_t i = 0; i < engine->GetObjectTypeCount(); ++i, ++p)
	{
		*p = WriteTypeInfo(engine, nullptr, engine->GetObjectTypeByIndex(i), false);
	}

	for(uint32_t i = 0; i < engine->GetFuncdefCount(); ++i, ++p)
	{
		*p = WriteTypeInfo(engine, nullptr, engine->GetFuncdefByIndex(i), false);
	}

	modules.back().typeInfoLength = p - buffer.data();

	assert(p == buffer.data() + buffer.size());

	uint32_t begin = m_file->tell();

	for(uint32_t i = 0; i < modules.size(); ++i)
	{
		modules[i].typeInfoLength -= modules[i].beginTypeInfo;
//convert to byte offset
		modules[i].beginTypeInfo   = modules[i].beginTypeInfo * sizeof(zCTypeInfo) + begin;
	}


	m_file->Write(buffer.data(), buffer.size());
}

zCTypeInfo zCZodiacWriter::WriteTypeInfo(asIScriptEngine * engine, asIScriptModule * module, asITypeInfo * type, bool registered)
{
	m_typeList.push_back(type->GetTypeId());

	zCTypeInfo info;

	auto func = type-> GetFuncdefSignature();

	info.name = SaveString(type->GetName());
	info.nameSpace = SaveString(type->GetNamespace());
	info.typeId    = type->GetTypeId();
	info.isFuncDef = func != nullptr;
	info.isRegistered = registered;
	info.propertiesBegin = m_file->tell();

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
			buffer.typeId = typeId;
			buffer.offset = offset;
			buffer.byteLength = GetByteLengthOfType(engine, module, typeId);

			m_file->Write(&buffer);
		}
	}

	info.propertiesLength   = (m_file->tell() - info.propertiesBegin) / sizeof(zCProperty);

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
	for(uint32_t i = 0; i <  engine->GetModuleCount(); ++i)
	{
		modules[i].byteCodeOffset = m_file->tell();

		if(saveByteCode)
		{
			engine->GetModuleByIndex(i)->SaveByteCode(m_file, stripDebugInfo);
		}

		modules[i].byteCodeLength = m_file->tell() - modules[i].byteCodeOffset;
	}
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

	for(uint32_t i = 0; i < engine->GetGlobalPropertyCount(); ++i)
	{
		modules.back().beginGlobalInfo = m_file->tell();

		void * address;
		engine->GetGlobalPropertyByIndex(i, &name, &nameSpace, &typeId, &isConst, nullptr, &address);

		zCGlobalInfo buffer;
		buffer.name = SaveString(name);
		buffer.nameSpace = SaveString(nameSpace);
		buffer.typeId   = typeId;
		buffer.address = SaveScriptObject(address, typeId, nullptr);

		modules.back().globalsLength = (m_file->tell() - modules.back().beginGlobalInfo) / sizeof(zCGlobalInfo);
	}

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
			stringAddress.insert(stringAddress.begin()+i, stringContents.size());

			auto len = strlen(string);
			stringContents.resize(stringContents.size()+len);
			strncpy(&stringContents[stringAddress.back()], string, len);

			return stringAddress.back();
		}
	}

	stringAddress.push_back(stringContents.size());

	auto len = strlen(string);
	stringContents.resize(stringContents.size()+len);
	strncpy(&stringContents[stringAddress.back()], string, len);

	return stringAddress.back();
}

int zCZodiacWriter::SaveTypeId(int typeId)
{
	if(typeId <= asTYPEID_DOUBLE)
		return typeId;

	for(uint32_t i = asTYPEID_DOUBLE+1; i < m_typeList.size(); ++i)
	{
		if(m_typeList[i] ==  typeId)
			return i;
	}

	throw Exception::zZE_BadTypeId;
	return -1;
}

int zCZodiacWriter::SaveFunction(asIScriptFunction const* func)
{
	if(func == nullptr) return -1;

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
	node.save_func = ZODIAC_GETSAVEFUNC(asIScriptContext);

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
