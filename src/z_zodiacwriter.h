#ifndef Z_ZODIACWRITER_H
#define Z_ZODIACWRITER_H
#ifdef HAVE_ZODIAC
#include "zodiac.h"
#include "z_zodiacstate.h"
#include "z_zodiac.h"
#include <vector>

namespace Zodiac
{

class zCZodiac;

class zCZodiacWriter : public zIZodiacWriter
{
public:
	static uint32_t CountTypes(asIScriptEngine * engine);

	zCZodiacWriter(zCZodiac * parent, zIFileDescriptor * file, std::atomic<int> & progress, std::atomic<int> & totalSteps);
	virtual ~zCZodiacWriter() = default;

	void SaveModules(asIScriptEngine * _module, bool saveByteCode, bool stripDebugInfo);

	void ProcessQueue();

	void WriteProperties();
	void WriteFunctionTable();
	void WriteAddressTable();
	void WriteStringTable();


	void WriteSaveData(zWRITER_FUNC_t, void * userData);

	void WriteHeader();

	void Finish() {}

	zIFileDescriptor * GetFile() const override { return m_file; }
	asIScriptEngine * GetEngine() const override { return m_parent->zCZodiac::GetEngine(); };
	bool SaveByteCode() const override { return m_parent->GetProperty(zZP_SAVE_BYTECODE); }

	int SaveString(const char *) override;
	int SaveTypeId(int id) override;
	int SaveFunction(asIScriptFunction const* id) override;
	int SaveContext(asIScriptContext const* id) override;
	int SaveScriptObject(void const* t, uint32_t asTypeId, void const* ownr = nullptr) override;

private:
typedef std::pair<void const*, int> VoidIntPair;

	int  SaveObject(void const* ptr, int zTypeId, zSAVE_FUNC_t) override;

	struct Node
	{
		void const*	 address;
		int			 asTypeId;
		int			 zTypeId;
		void const*	 owner;
		zSAVE_FUNC_t save_func;

		int operator<(const Node& n) const { return address < n.address; }
	};

	bool HaveAddress(const void * value, int * closest, int * address) const;
	int EnqueueNode(Node node);
	void WriteObject(Node & n, uint32_t & offset, uint32_t & byteLength);
	bool WriteDelegate(const void * ptr, int typeId);

	void WriteByteCode(asIScriptEngine * engine, std::vector<zCModule> & modules, bool saveByteCode, bool stripDebugInfo);
	void WriteGlobalVariables(asIScriptEngine * engine, std::vector<zCModule> & modules);
	void WriteTypeInfo(asIScriptEngine * engine, std::vector<zCModule> & modules);
	std::vector<zCTypeInfo> WriteProperties(asIScriptEngine * engine, std::vector<zCModule> & modules);

	zCTypeInfo WriteTypeInfo(asIScriptEngine * engine, asIScriptModule * _module, asITypeInfo * type, bool registered);
	void WriteScriptObject(const void * ptr, int typeId);
	uint32_t GetByteLengthOfType(asIScriptEngine * engine, asIScriptModule * _module, uint32_t typeId);
	uint32_t InsertString(const char * string);

	zCZodiac		 *    m_parent;
	zIFileDescriptor *    m_file;
	zCHeader			  m_header;

	std::vector<zCEntry>		m_addressTable{0};
	std::vector<VoidIntPair>	m_addressIndex{{0,0}};
	std::vector<int>			m_typeList;
	std::vector<zCFunction>		m_functionList;
	std::vector<zCProperty>     m_propertiesList;
	std::vector<zCTypeInfo>     m_typeInfo;
	std::vector<zCTemplate> m_templates;
	std::vector<int>                m_ttypeList;


	std::vector<uint32_t> stringAddress{0};
	std::vector<char>     stringContents{0};

	std::vector<Node>	  m_stack;


	std::atomic<int> & m_progress;
	std::atomic<int> & m_totalSteps;
};
}

#endif
#endif // Z_ZODIACWRITER_H
