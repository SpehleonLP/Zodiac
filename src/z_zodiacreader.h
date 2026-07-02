#ifndef Z_ZODIACREADER_H
#define Z_ZODIACREADER_H
#ifdef HAVE_ZODIAC
#include "z_zodiacstate.h"
#include "z_memorymap.h"
#include "z_zodiac.h"
#include "zodiac.h"
#include <memory>
#include <atomic>
#include <cstring>

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

	const char		*	LoadString(int id, uint32_t * outLen = nullptr) const override
	{
		if((uint32_t)id >= stringTableLength())
		{
			if(outLen) *outLen = 0;
			return nullptr;
		}

		if(outLen)
		{
			const uint32_t avail = stringTableLength() - (uint32_t)id;
			uint32_t len;
			if((uint32_t)id >= sizeof(uint32_t))
			{
				// Length prefix sits immediately before the data (see the
				// writer's InsertString). Read it unaligned-safe via memcpy.
				memcpy(&len, m_stringTable + id - sizeof(uint32_t), sizeof(uint32_t));
				// Never trust the stored length past the table end (corrupt
				// input): clamp so a length-driven read can't over-run.
				if(len > avail) len = (uint32_t)strnlen(&m_stringTable[id], avail);
			}
			else
			{
				// No room for a prefix (the empty-string sentinel at id 0, or a
				// corrupt sub-prefix offset): fall back to a bounded strlen.
				len = (uint32_t)strnlen(&m_stringTable[id], avail);
			}
			*outLen = len;
		}

		return &m_stringTable[id];
	}
	asITypeInfo		*	LoadTypeInfo(int id, bool RefCount) override;
	int					LoadTypeId(int id) override;
	asIScriptFunction * LoadFunction(int id) override;
	asIScriptContext *  LoadContext(int id) override;
	void *				LoadObject(int id, zLOAD_FUNC_t, int & actualType) override;
	void				LoadObject(int id, void *, zLOAD_FUNC_t, int & actualType, bool isHandle) override;

	void Verify() const;

private:
friend class zCZodiac;
	// true iff [offset, offset + count*elemSize) lies fully within [0, fileLen),
	// with no arithmetic overflow. All inputs are widened to 64-bit; because every
	// caller's offset/count come from 32-bit header fields and elemSize is a small
	// sizeof, count*elemSize and offset+bytes can never overflow uint64_t.
	static bool InFile(uint64_t offset, uint64_t count, uint64_t elemSize, uint64_t fileLen);
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
	char		  const * m_stringTable;
	zCModule	  const * m_modules;
	zCEntry		  const * m_entries;
	zCGlobalInfo  const * m_globals;
	zCTypeInfo    const * m_typeInfo;


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
