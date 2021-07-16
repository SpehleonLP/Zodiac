#ifndef Z_ZODIACREADER_H
#define Z_ZODIACREADER_H
#ifdef HAVE_ZODIAC
#include "z_zodiacstate.h"
#include "z_memorymap.h"
#include "z_zodiac.h"
#include "zodiac.h"
#include <memory>
#include <atomic>

namespace Zodiac
{

class zCZodiac;

class zCZodiacReader : public zIZodiacReader
{
public:
	zCZodiacReader(zCZodiac * parent, zIFileDescriptor * file, std::atomic<int> & progress, std::atomic<int> & total_steps);

	int GetModuleIndex(const char * name, uint32_t quickCheck = ~0u) const;
	zCGlobalInfo const* GetGlobalVar(uint32_t module, const char * name, const char * nameSpace, uint32_t quickCheck = ~0u) const;
	zCTypeInfo const* GetTypeInfo(uint32_t module, const char * name, const char * nameSpace, uint32_t quickCheck = ~0u) const;
	void GetProperties(int typeId, zCProperty const*& begin, zCProperty const*& end) const;
	int  asGetProperty(asITypeInfo * typeInfo, const char * name, int * typeId, int from = 0) const;

	uint32_t saveDataByteLength()   const { return m_header->saveDataByteLength; }
	uint32_t stringAddressCount()	const { return m_header->stringAddressLength; }
	uint32_t stringTableLength()	const { return m_header->stringTableByteLength; }
	uint32_t addressTableLength()	const { return m_header->addressTableLength; }
	uint32_t moduleDataLength()		const { return m_header->moduleDataLength; }
	uint32_t typeInfoLength()		const { return m_header->typeInfoLength; }
	uint32_t globalsLength()		const { return m_header->globalsLength; }
	uint32_t propertiesLength()		const { return m_header->propertiesLength; }
	uint32_t functionTableLength()	const { return m_header->functionTableLength; }

	zIFileDescriptor * GetFile() const override { return m_file; };
	asIScriptEngine * GetEngine() const override { return m_parent->zCZodiac::GetEngine(); }

	void LoadScriptObject(void *, int address, uint asTypeId) override;
//if typeID is an object/handle then the next thing read should be an address, otherwise it should be a value type block.
	void LoadScriptObject(void *, uint asTypeId) override;

	const char * LoadString(int id) const override { return  (uint32_t)id < stringAddressCount()? &m_stringTable[m_stringAddresses[id]] : nullptr;  }
	asITypeInfo * LoadTypeInfo(int id, bool RefCount) override;
	asIScriptFunction * LoadFunction(int id) override;
	asIScriptContext * LoadContext(int id) override;
	void * LoadObject(int id, zLOAD_FUNC_t, int & actualType) override;

	const char * Verify() const;

private:
friend class zCZodiac;
	const char * LoadModules(asIScriptEngine *);
	void ReadSaveData(zREADER_FUNC_t, void *);
	void RestoreGlobalVariables(asIScriptEngine *);

	static void RestorePrimitive(void *, int dstTypeId, const void * address, int srcTypeId);
//everything we can restore without updating the loading table
	bool RestoreNoCreate(void *, uint32_t address, int asTypeId);
	bool RestoreAppObject(void * dst, int address, uint asTypeId);
	void RestoreScriptObject(void * dst, const void * src, uint asTypeId);
	bool RestoreFunction(void ** dst, uint32_t handle, asITypeInfo * typeInfo);

	zCZodiac * m_parent;
	zIFileDescriptor * m_file;

	zCMemoryMap	  m_mmap;

	zCHeader	  const * m_header;
	uint32_t	  const * m_stringAddresses;
	char		  const * m_stringTable;
	zCModule	  const * m_modules;
	zCEntry		  const * m_entries;
	zCGlobalInfo  const * m_globals;
	zCTypeInfo    const * m_typeInfo;
	zCFunction    const * m_functions;

	struct Property
	{
		uint32_t propertyId{~0u};
		uint32_t readOffset;
		uint32_t readType;
		uint32_t writeType;
	};

	std::unique_ptr<Property[]>		m_properties;

	struct LoadedInfo
	{
		void   * ptr;
		int asTypeId;
		int zTypeId;
		int needRelease;
	};

	std::unique_ptr<int[]>		m_asTypeIdFromStored;
	std::unique_ptr<LoadedInfo[]>	m_loadedObjects;
	std::unique_ptr<void*[]>	m_loadedFunctions;

	std::atomic<int> & m_progress;
	std::atomic<int> & m_totalSteps;
};

}

#endif
#endif // Z_ZODIACREADER_H
