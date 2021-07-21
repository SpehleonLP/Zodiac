#ifndef ZODIACSTATE_H
#define ZODIACSTATE_H
#ifdef HAVE_ZODIAC
#include <zodiac.h>

namespace Zodiac
{

struct zCEntry
{
	uint typeId;
	uint offset;
	uint byteLength;
	uint owner;
};

struct zCFunction
{
	uint delegateAddress;
	uint delegateTypeId;
	uint module;
	uint objectType;
	uint declaration;
//	uint nameSpace;
};

struct zCModule
{
	uint name;
	uint byteCodeOffset;
	uint byteCodeLength;
	uint beginTypeInfo;
	uint typeInfoLength;
	uint beginGlobalInfo;
	uint globalsLength;
};

struct zCGlobalInfo
{
	uint name;
	uint nameSpace;
	uint address;
	uint typeId; // doesn't do anything but maybe good to have for debugging
};

struct zCTypeInfo
{
	uint name;
	uint nameSpace;
	uint typeId;
	ubyte  isFuncDef;
	ubyte  isRegistered;
	ubyte  isPropertiesSet;
	uint propertiesBegin;
	uint propertiesLength;
};

struct zCProperty
{
	uint name;
	uint typeId;
	uint offset;
	int  byteLength;
};

struct zCHeader
{
	zCHeader()
	{
		int i=1;
		isBigEndian = ! *((char *)&i);
	}

	char  magic[16]{"rR6qXMF8q@#agBZ"};

	ubyte  pointerSize{sizeof(void*)};
	ubyte  boolSize{sizeof(bool)};
	ubyte  isBigEndian{};
	ubyte  pad11{};
	uint asVersion{ANGELSCRIPT_VERSION};
	uint writerVersionId{};

	uint saveDataByteOffset{};
	uint saveDataByteLength{};

//strings
	uint stringAddressOffset{};
	uint stringAddressLength{};
	uint stringTableOffset{};
	uint stringTableByteLength{};

//objects
	uint addressTableOffset{};
	uint addressTableLength{};
	uint savedObjectOffset{};
	uint savedObjectLength{};

//delegates
	uint functionTableOffset{};
	uint functionTableLength{};

//typeInfo
	uint typeInfoOffset{};
	uint typeInfoLength{};
	uint propertiesOffset{};
	uint propertiesLength{};

//modules
	uint moduleDataOffset{};
	uint moduleDataLength{};
	uint byteCodeOffset{};
	uint byteCodeByteLength{};

	uint globalsOffset{};
	uint globalsLength{};
};



}

#endif
#endif // ZODIACSTATE_H
