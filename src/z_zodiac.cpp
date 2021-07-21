#include "z_zodiac.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacwriter.h"
#include "z_zodiacreader.h"
#include <algorithm>
#include <cstring>
#include <cassert>

namespace Zodiac
{

int zIZodiac::typeIdCounter{1};

std::unique_ptr<zIZodiac> zCreateZodiac(asIScriptEngine * engine)
{
	return std::unique_ptr<zIZodiac>(new zCZodiac(engine));
}

zCZodiac::zCZodiac(asIScriptEngine * engine) :
	m_engine(engine)
{
	engine->AddRef();
}

zCZodiac::~zCZodiac()
{
	m_engine->Release();
}

void    zCZodiac::SaveToFile(zIFileDescriptor * file)
{
	if(m_inProgress.exchange(true))
	{
		throw std::runtime_error("Saving while in progress...");
		return;
	}

	ClearBoolOnDestruct clearer(m_inProgress);

	SortTypeList();

	zCZodiacWriter writer(this, file, m_progress, m_totalSteps);

	if(m_preSavingCallback)
	{
		(m_preSavingCallback)(m_userData);
	}

	writer.SaveModules(m_engine, GetProperty(zZP_SAVE_BYTECODE), GetProperty(zZP_STRIP_DEBUGINFO));

	writer.WriteSaveData(m_saveDataWriteCallback, m_userData);

	writer.ProcessQueue();

	writer.WriteProperties();
	writer.WriteFunctionTable();
	writer.WriteAddressTable();
	writer.WriteStringTable();


	writer.WriteHeader();
	writer.Finish();

	if(m_postSavingCallback)
	{
		(m_postSavingCallback)(m_userData);
	}
}

bool zCZodiac::LoadFromFile(zIFileDescriptor * file)
{
	if(m_inProgress.exchange(true))
	{
		throw std::runtime_error("Loading while in progress");
		return false;
	}

	ClearBoolOnDestruct clearer(m_inProgress);
	std::unique_ptr<zCZodiacReader> reader;

	try
	{
		SortTypeList();
		reader.reset(new zCZodiacReader(this, file, m_progress, m_totalSteps));
	}
	catch(Exception & e)
	{
		error_string = std::move(e.text);
		error_code   = e.code;
		return false;
	}

	if(m_preRestoreCallback)
	{
		(m_preRestoreCallback)(m_userData);
	}

	reader->LoadModules(m_engine);
	reader->ReadSaveData(m_saveDataReadCallback, m_userData);
	reader->RestoreGlobalVariables(m_engine);

	if(m_postRestoreCallback)
	{
		(m_postRestoreCallback)(m_userData);
	}

	return true;
}

int  zCZodiac::RegisterTypeCallback(uint32_t zTypeId, uint32_t byteLength, const char * name, zSAVE_FUNC_t onSave, zLOAD_FUNC_t onLoad, const char * nameSpace, bool isValueType)
{
	if(name == nullptr) name = "";
	if(nameSpace == nullptr) nameSpace = "";

	for(auto & c : m_typeList)
	{
		if(c.name == name && c.nameSpace == nameSpace)
			return asALREADY_REGISTERED;
	}

	TypeEntry entry;
	entry.asTypeId = -1;
	entry.zTypeId  = zTypeId;
	entry.byteLength = byteLength;
	entry.name   = name;
	entry.isValueType = isValueType;
	entry.nameSpace = nameSpace;
	entry.subTypeId = 0;
	entry.onSave = onSave;
	entry.onLoad = onLoad;

	m_typeList.push_back(entry);

	return asSUCCESS;
}

int   zCZodiac::GetAsTypeIdFromZTypeId(int zTypeId) const
{
	for(auto & c : m_typeList)
	{
		if(c.asTypeId == zTypeId)
			return c.asTypeId;
	}

	return -1;
}

int  zCZodiac::GetZTypeIdFromAsTypeId(int asTypeId) const
{

	for(auto & c : m_typeList)
	{
		if(c.asTypeId == asTypeId)
			return c.zTypeId;
	}

	return -1;
}

zCZodiac::TypeEntry const* zCZodiac::GetTypeEntryFromAsTypeId(int asTypeId) const
{
	asTypeId &= ~zTYPEID_OBJHANDLE;

	if(asTypeId & asTYPEID_TEMPLATE)
	{
		auto typeInfo = m_engine->GetTypeInfoById(asTypeId);

		if(typeInfo == nullptr)
			return nullptr;

		for(auto & c : m_typeList)
		{
			if(strcmp(typeInfo->GetName(), c.name) == 0)
				return &c;
		}

		return nullptr;
	}

	for(auto & c : m_typeList)
	{
		if(c.asTypeId == asTypeId)
			return &c;
	}

	return nullptr;
}

zCZodiac::TypeEntry const* zCZodiac::GetTypeEntryFromZTypeId(int zTypeId) const
{
	for(auto & c : m_typeList)
	{
		if(c.zTypeId == zTypeId)
			return &c;
	}

	return nullptr;
}

zCZodiac::TypeEntry * zCZodiac::MatchType(asITypeInfo * typeInfo, int quickCheck)
{
	TypeEntry * r{};

	if((uint32_t)quickCheck < m_typeList.size())
	{
		if(strcmp(m_typeList[quickCheck].name,	   typeInfo->GetName())      == 0
		&& strcmp(m_typeList[quickCheck].nameSpace, typeInfo->GetNamespace()) == 0)
		{
			r = &m_typeList[quickCheck];
			if(r->asTypeId == -1) return r;
		}
	}

	for(uint32_t j = 0; j < m_typeList.size(); ++j)
	{
		if(strcmp(m_typeList[j].name,	   typeInfo->GetName())      == 0
		&& strcmp(m_typeList[j].nameSpace, typeInfo->GetNamespace()) == 0)
		{
			r = &m_typeList[j];
			if(r->asTypeId == -1) return r;
		}
	}

	return r;
}

void zCZodiac::SortTypeList()
{
	for(auto & entry : m_typeList)
	{
		entry.asTypeId = -1;
	}

	auto N = m_engine->GetObjectTypeCount();
	bool needSort = false;

	for(uint32_t i = 0; i < N; ++i)
	{
		auto typeId = m_engine->GetObjectTypeByIndex(i);
		bool isValueType = typeId->GetFlags() & asOBJ_VALUE;

		auto entry = MatchType(typeId, i);

		if(entry)
		{
			entry->asTypeId   = typeId->GetTypeId();
			entry->byteLength = typeId->GetSize();
			entry->subTypeId  = typeId->GetSubTypeId();
			assert(entry->isValueType == isValueType);
			continue;
		}
		else
		{
			if(0 == (typeId->GetFlags() & asOBJ_POD))
			{
				if(typeId->GetNamespace())
					throw std::runtime_error(std::string("Missing entry for loading/saving ") + typeId->GetNamespace() + "::" + typeId->GetName());
				else
					throw std::runtime_error(std::string("Missing entry for loading/saving ") + typeId->GetName());
			}

			TypeEntry entry;
			entry.asTypeId = typeId->GetTypeId();
			entry.byteLength = typeId->GetSize();
			entry.name   = typeId->GetName();
			entry.nameSpace = typeId->GetNamespace();
			entry.isValueType = true;
			entry.subTypeId   = 0;
			entry.onSave = nullptr;
			entry.onLoad = nullptr;

			m_typeList.push_back(entry);
		}
	}

	if(needSort)
	{
		std::sort(m_typeList.begin(), m_typeList.end());
	}

}





}

#endif
