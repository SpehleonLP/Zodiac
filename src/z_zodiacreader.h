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
	~zCZodiacReader();

	int GetModuleIndex(const char * name, uint32_t quickCheck = ~0u) const;
	zCGlobalInfo const* GetGlobalVar(uint32_t _module, const char * name, const char * nameSpace, uint32_t quickCheck = ~0u) const;
	zCTypeInfo const* GetTypeInfo(uint32_t _module, const char * name, const char * nameSpace, uint32_t * quickCheck = nullptr) const;
	void GetProperties(int typeId, zCProperty const*& begin, zCProperty const*& end) const;
	int  asGetProperty(asITypeInfo * typeInfo, const char * name, int * typeId, int from = 0) const;

	inline uint32_t saveDataByteLength()    const { return m_header->saveDataByteLength; }
	inline uint32_t stringAddressCount()	const { return m_header->stringAddressLength; }
	inline uint32_t stringTableLength()		const { return m_header->stringTableByteLength; }
	inline uint32_t addressTableLength()	const { return m_header->addressTableLength; }
	inline uint32_t moduleDataLength()		const { return m_header->moduleDataLength; }
	inline uint32_t typeInfoLength()		const { return m_header->typeInfoLength; }
	inline uint32_t globalsLength()			const { return m_header->globalsLength; }
	inline uint32_t propertiesLength()		const { return m_header->propertiesLength; }
	inline uint32_t functionTableLength()	const { return m_header->functionTableLength; }
	inline uint32_t templatesLength()		const { return m_header->templatesLength; }
	inline uint32_t typeTableLength()		const { return templatesLength()+typeInfoLength(); }

	inline uint32_t		 const * GetStringAddresses() const { return (uint32_t const*)(m_mmap.GetAddress() + m_header->stringAddressOffset); }
	inline char			 const * GetStringTable() const { return (char const *)(m_mmap.GetAddress() + m_header->stringTableOffset); }
	inline zCModule		 const * GetModules() const { return (zCModule const*)(m_mmap.GetAddress() + m_header->moduleDataOffset); }
	inline zCEntry		 const * GetEntries() const { return (zCEntry const*)(m_mmap.GetAddress() + m_header->addressTableOffset); }
	inline zCGlobalInfo  const * GetGlobals() const { return (zCGlobalInfo const*)(m_mmap.GetAddress() + m_header->globalsOffset); }
	inline zCTypeInfo    const * GetTypeInfo() const { return (zCTypeInfo const*)(m_mmap.GetAddress() + m_header->typeInfoOffset); }
	inline zCFunction    const * GetFunctions()  const { return (zCFunction const*)(m_mmap.GetAddress() + m_header->functionTableOffset); }
	inline zCTemplate    const * GetTemplates()  const { return (zCTemplate const*)(m_mmap.GetAddress() + m_header->templatesOffset); }

	zIFileDescriptor * GetFile() const override { return m_file; };
	asIScriptEngine * GetEngine() const override { return m_parent->zCZodiac::GetEngine(); }

	void LoadScriptObject(void *, int address, int asTypeId, bool isWeak=false) override;

	const char		*	LoadString(int id) const override { return  (uint32_t)id < stringTableLength()? &m_stringTable[id] : nullptr;  }
	asITypeInfo		*	LoadTypeInfo(int id, bool RefCount) override;
	int					LoadTypeId(int id) override;
	asIScriptFunction * LoadFunction(int id) override;
	asIScriptContext *  LoadContext(int id) override;
	void *				LoadObject(int id, zLOAD_FUNC_t, int & actualType) override;
	void				LoadObject(int id, void *, zLOAD_FUNC_t, int & actualType, bool isHandle) override;

	void Verify() const;

private:
friend class zCZodiac;
	void ProcessModules(asIScriptEngine *, bool loadedByteCode);
	void ReadSaveData(zREADER_FUNC_t, void *);
	void DocumentGlobalVariables(asIScriptEngine *);
	void RestoreGlobalVariables(asIScriptEngine *);
	bool LoadByteCode(asIScriptEngine * engine);
	void SolveTemplates(asIScriptEngine * engine);
	void PopulateTable(void * dst, uint32_t address, int typeId);

	static void RestorePrimitive(void *, int dstTypeId, const void * address, int srcTypeId);
//everything we can restore without updating the loading table
	bool RestoreNoCreate(void *, uint32_t address, int asTypeId);
	bool RestoreAppObject(void * dst, int address, int asTypeId);
	void RestoreScriptObject(void * dst, const void * src, uint asTypeId);
	bool RestoreFunction(void ** dst, uint32_t handle, asITypeInfo * typeInfo);

	template<typename T>
	void SolveTypeInfo(T * op, int i, uint32_t & quickCheck, asITypeInfo* (*)(T*, int), uint32_t N);

	template<typename T>
	void SolveTypeInfo(T * op, int i);

	zCZodiac * m_parent;
	zIFileDescriptor * m_file;

	zCMemoryMap	  m_mmap;

	zCHeader	  const * m_header;
//	uint32_t	  const * m_stringAddresses;
	char		  const * m_stringTable;
	zCModule	  const * m_modules;
	zCEntry		  const * m_entries;
	zCGlobalInfo  const * m_globals;
	zCTypeInfo    const * m_typeInfo;
//	zCFunction    const * m_functions;


	struct Property
	{
		uint32_t propertyId{~0u};
		uint32_t readOffset;
		int readType;
		int writeType;
	};

	std::vector<Property>		m_properties;

	struct LoadedInfo
	{
		void   * ptr;
		int asTypeId;
		int zTypeId;
		short needRelease;
		bool beingLoaded;
	};

	std::vector<int>		m_asTypeIdFromStored;
	std::unique_ptr<LoadedInfo[]>	m_loadedObjects;
	std::unique_ptr<void*[]>	m_loadedFunctions;

	std::atomic<int> & m_progress;
	std::atomic<int> & m_totalSteps;
};


}

#endif
#endif // Z_ZODIACREADER_H
