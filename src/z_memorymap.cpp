#include "z_memorymap.h"
#ifdef HAVE_ZODIAC
#include "zodiac.h"

#include <cstdlib>

namespace Zodiac
{

/* Random reads worse than sequential so just copy it
 *
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
}*/

zCMemoryMap::zCMemoryMap(zIFileDescriptor * descriptor)
{
	descriptor->seek(0, Flags::zFILE_END);
	m_length = descriptor->tell();
	descriptor->seek(0, Flags::zFILE_BEGIN);

	m_contents = std::malloc(m_length);
	descriptor->Read(m_contents, m_length);
}

zCMemoryMap::~zCMemoryMap()
{
	std::free(m_contents);
}

}

#endif
