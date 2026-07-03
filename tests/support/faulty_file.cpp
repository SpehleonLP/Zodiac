#include "faulty_file.h"
#include "z_zodiacexception.h"

using Zodiac::uint;

namespace zodiac_test
{

int zCFaultyFile::Write(const void * ptr, uint size)
{
	// Once the running total plus this request would exceed the cap, throw
	// before delegating so no bytes past the threshold are stored.
	if(m_capActive && (uint64_t)m_written + size > m_writeCap)
		throw Zodiac::Exception(Zodiac::zE_IOError);

	int r = zCMemoryFile::Write(ptr, size);
	m_written += (uint32_t)r;
	return r;
}

bool zCFaultyFile::Flush()
{
	if(m_failFlush)
		throw Zodiac::Exception(Zodiac::zE_IOError);

	return true;
}

}
