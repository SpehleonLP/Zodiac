#ifndef ZODIACSTATE_H
#define ZODIACSTATE_H
#ifdef HAVE_ZODIAC
#include <zodiac.h>

namespace Zodiac
{

// On-disk format version. Bump whenever the wire format changes in a way that
// makes older images unreadable. Verify() rejects any image whose stored
// writerVersionId != this value with zE_BadFileType. There is no on-disk
// back-compat (no shipped saves), so a hard != reject is correct.
//   v1 (2026-07-01): string table entries are length-prefixed (uint32 byte
//                    count stored immediately before each string's data) so
//                    strings with embedded NUL bytes round-trip.
static constexpr uint zZODIAC_FORMAT_VERSION = 1;

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
	uint _module;
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

// A funcdef-handle global's `address` indexes the FUNCTION table (SaveScriptObject
// routes funcdefs to SaveFunction), not the object-address table. It is marked
// with this sentinel top bit so Verify() can bound it against the right table and
// the restore path can mask it back to a plain function index. Real function/
// object indices are bounded by RAM and never approach 2^31, so the top bit is a
// safe discriminator (asserted implicitly by the Verify range checks).
static const uint zGLOBAL_FUNCTION_ADDRESS = 0x80000000u;

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

struct zCTemplate
{
	uint name;
	uint nameSpace;
	uint _module;
	uint declaration;
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
	uint writerVersionId{zZODIAC_FORMAT_VERSION};

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

	uint templatesOffset{};
	uint templatesLength{};

	uint globalsOffset{};
	uint globalsLength{};
};



}

#endif
#endif // ZODIACSTATE_H
