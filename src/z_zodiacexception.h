#ifndef Z_ZODIACEXCEPTION_H
#define Z_ZODIACEXCEPTION_H
#include "zodiac.h"
#include <exception>
#include <string>

namespace Zodiac
{

class Exception : public std::exception
{
public:
	static const char * ToString(int code)
	{
		if((uint)code >= Code::zE_Total)
			return "";

		static const char * strings[] = {
			"None"
			"Casting Exception",
			"Buffer Overrun",
			"Bad Object Address",
			"Bad Function Info",

			"DuplicateObject Address",
			"Object Restore Type Mismatch",
			"Object Unserializable",
			"Bad Type Id",
			"Inconsistent Object Type",
			"Inconsistent Object Ownership",
			"Owner Not Encoded",
			"Unknown Encoding Protocol",
			"Module Does Not Exist",
			"Double Load",
			"Cant Save Context Without Bytecode",
			"Context Not Suspended",
			"Duplicate Property Address",
			"Unable To Restore Property",
			"Bad Sub File Address",
			"Bad File Type"
		};

		return strings[code];
	}


	Exception(Code code) :	text(ToString(-code)), code(code) { }
	Exception(std::string const& text, Code code) :	text(ToString(code) + (": " + text)), code(code) { }
	const char * what() const noexcept { return text.c_str(); }

	std::string text;
	const Code code;
};

}

#endif // Z_ZODIACEXCEPTION_H
