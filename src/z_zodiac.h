#ifndef Z_ZODIAC_H
#define Z_ZODIAC_H
#ifdef HAVE_ZODIAC
#include "zodiac.h"
#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace Zodiac
{

class zCZodiac : public zIZodiac
{
public:
struct TypeEntry;
	zCZodiac(asIScriptEngine * engine);
	virtual ~zCZodiac();

	asIScriptEngine * GetEngine() const override { return m_engine; }

	bool    IsInProgress() const override { return m_inProgress; }
	float   Progress() const override { return m_progress / (float)m_totalSteps; }

	void    SetProperty(zZodiacProp property, bool value) override { if(value) m_options |= (1 << property); else m_options &= ~(1 << property); }
	bool    GetProperty(zZodiacProp property) const override { return m_options & (1 << property); }

	void    SetSaveScope(const char * moduleName) override;
	void    SetModuleRemap(const char * savedName, const char * liveName) override;
	void    SetPrototypeProvider(zPROTO_FUNC_t p) override { m_protoProvider = p; }

	// Concrete-only accessor (not on the zIZodiac interface): the writer and reader
	// hold a zCZodiac* and read the installed provider through this. Null = feature
	// off (no prototype table written / no prototype merge on load).
	zPROTO_FUNC_t GetPrototypeProvider() const { return m_protoProvider; }

	Code    SaveToFile(zIFileDescriptor *)	 override;
	Code	LoadFromFile(zIFileDescriptor *) override;

	void  SetPreRestoreCallback(zFUNCTION_t cb)			 override { m_preRestoreCallback = cb;  }
	void  SetPreSavingCallback(zFUNCTION_t cb)			 override { m_preSavingCallback = cb;  }

	void  SetReadSaveDataCallback(zREADER_FUNC_t cb)	 override { m_saveDataReadCallback = cb;  }
	void  SetWriteSaveDataCallback(zWRITER_FUNC_t cb)	override { m_saveDataWriteCallback = cb;  }

	void  SetPostRestoreCallback(zFUNCTION_t cb)		 override { m_postRestoreCallback = cb;  }
	void  SetPostSavingCallback(zFUNCTION_t cb)			 override { m_postSavingCallback = cb;  }

	void SetUserData(void * d) override { m_userData = d; };
	void * GetUserData() const override { return m_userData; };

	int   RegisterTypeCallback(uint32_t zTypeId, uint32_t byteLength, const char * name, zSAVE_FUNC_t, zLOAD_FUNC_t, const char * nameSpace, bool isValueType) override;

	int   GetAsTypeIdFromZTypeId(int zTypeId) const override;
	int   GetZTypeIdFromAsTypeId(int asTypeId) const override;

	TypeEntry const* GetTypeEntryFromAsTypeId(int asTypeId) const;
	TypeEntry const* GetTypeEntryFromZTypeId(int zTypeId) const;

	const char * GetErrorString() override { return error_string.data(); }
	Code		 GetErrorCode() override { return error_code; }

private:
	zCZodiac::TypeEntry * MatchType(asITypeInfo * typeInfo, int quickCheck);
	void SortTypeList();

	ulong 		    m_options{};
	asIScriptEngine * m_engine{};

	zFUNCTION_t m_preRestoreCallback{};
	zFUNCTION_t m_preSavingCallback{};

	zREADER_FUNC_t m_saveDataReadCallback{};
	zWRITER_FUNC_t m_saveDataWriteCallback{};

	zFUNCTION_t m_postRestoreCallback{};
	zFUNCTION_t m_postSavingCallback{};

	void * 	m_userData{};
	std::atomic<int>	m_progress{};
	std::atomic<int>	m_totalSteps{};
	std::atomic<bool>   m_inProgress{};
	std::atomic<bool>   m_loaded{};

	std::vector<TypeEntry> m_typeList;

	// Empty = whole-engine save. Non-empty = restrict SaveToFile to the named
	// module (see zIZodiac::SetSaveScope).
	std::string m_saveScope;

	// Prototype provider (see zIZodiac::SetPrototypeProvider / zPROTO_FUNC_t).
	// Null (default) = no prototype table is written and load does no prototype
	// work. Lives on the facade so it survives the whole Save/LoadFromFile call;
	// the writer and reader read it through GetPrototypeProvider().
	zPROTO_FUNC_t m_protoProvider{};

	// Load-side savedName -> liveName module rename table (see SetModuleRemap).
	// Empty = no remap (every module resolves under its recorded name). Passed
	// by pointer to the reader so ResolveModuleName can consult it. Lives on the
	// facade so it survives for the whole LoadFromFile call.
	std::map<std::string, std::string> m_moduleRemap;

//the beauty of these things is that they avoid clunky try/catch statements
	struct ClearBoolOnDestruct
	{
		ClearBoolOnDestruct(std::atomic<bool> & it) : _it_(it) { }
		~ClearBoolOnDestruct() {_it_ = false; }
		std::atomic<bool> & _it_;
	};

	std::string error_string;
	Code		error_code{zE_Success};
};

struct zCZodiac::TypeEntry
{
	int			 asTypeId;
	bool		 isValueType : 1;
	int			 byteLength : 31;
	int			 zTypeId;
	int			 subTypeId;
	const char * name;
	const char * nameSpace;
	zSAVE_FUNC_t onSave;
	zLOAD_FUNC_t onLoad;

	bool operator<(TypeEntry const& b) { return asTypeId < b.asTypeId; }
};


}

#endif
#endif // Z_ZODIAC_H
