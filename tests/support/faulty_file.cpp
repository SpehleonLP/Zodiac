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

int zCFaultyFile::Read(void * ptr, uint size)
{
	// Truncate to the read cap first (a short/zero count), then delegate to the
	// base, which further clamps to the current sub-file end.
	if(m_readCapActive)
	{
		uint32_t avail = (m_read < m_readCap) ? (m_readCap - m_read) : 0u;
		if(size > avail) size = avail;
	}
	int r = zCMemoryFile::Read(ptr, size);
	m_read += (uint32_t)r;
	return r;
}

}
