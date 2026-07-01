#include "z_memorymap.h"
#ifdef HAVE_ZODIAC
#include "zodiac.h"

#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

namespace Zodiac
{

// Map the WHOLE underlying file read-only. mmap offsets must be page-aligned,
// so we always map from offset 0 and expose the base via GetAddress(); the
// reader indexes everything relative to that base (and finds the trailer header
// via GetLength()). A demand-paged read-only map faults in only the pages that
// are actually touched, which suits the trailer-header + offset-chasing access
// pattern far better than copying the whole file up front. When the descriptor
// is not a real file (fd < 0) or mmap fails, fall back to malloc + slurp.
zCMemoryMap::zCMemoryMap(zIFileDescriptor * descriptor)
{
	descriptor->seek(0, Flags::zFILE_END);
	m_length = descriptor->tell();
	descriptor->seek(0, Flags::zFILE_BEGIN);

	int fd = descriptor->GetFileDescriptor();

	if(fd >= 0 && m_length > 0)
	{
		void * addr = mmap(nullptr, m_length, PROT_READ, MAP_PRIVATE, fd, 0);
		if(addr != MAP_FAILED)
		{
			m_contents = addr;
			m_isMapped = true;
			return;
		}
	}

	// Malloc-fallback path (in-memory descriptor, or mmap unavailable/failed).
	// Sanity-cap the length so a corrupt/hostile tell() can't request an absurd
	// allocation, and reject a failed malloc or a short read rather than handing
	// Verify() a partially-filled buffer to walk. The cap also keeps m_length
	// inside the int taken by Read().
	constexpr unsigned long long kMaxFileLength = 512ull * 1024 * 1024;
	if(m_length > kMaxFileLength)
		throw zE_BufferOverrun;

	m_contents = std::malloc(m_length);
	if(m_contents == nullptr && m_length != 0)
		throw zE_BufferOverrun;

	if(descriptor->Read(m_contents, (int)m_length) != (int)m_length)
	{
		std::free(m_contents);
		m_contents = nullptr;
		throw zE_EndOfFile;
	}
}

zCMemoryMap::~zCMemoryMap()
{
	if(m_isMapped)
		munmap(m_contents, m_length);
	else
		std::free(m_contents);
}

}

#endif
