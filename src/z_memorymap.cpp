#include "z_memorymap.h"
#ifdef HAVE_ZODIAC
#include "zodiac.h"

#include <sys/mman.h>

namespace Zodiac
{

zCMemoryMap::zCMemoryMap(zIFileDescriptor * descriptor)
{
	descriptor->seek(0, Flags::zFILE_END);
	m_length = descriptor->tell();
	descriptor->seek(0, Flags::zFILE_BEGIN);

	m_addr = mmap(nullptr, m_length, PROT_READ, MAP_PRIVATE, descriptor->GetFileDescriptor(), 0);
}

zCMemoryMap::~zCMemoryMap()
{
	munmap(m_addr, m_length);
}

}

#endif
