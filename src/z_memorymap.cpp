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

	m_contents = std::malloc(m_length);
	descriptor->Read(m_contents, m_length);
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
