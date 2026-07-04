#include "z_memoryfile.h"

#ifdef HAVE_ZODIAC

#include "z_zodiacexception.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <sys/mman.h>
	#ifndef MAP_ANONYMOUS
		#ifdef MAP_ANON
			#define MAP_ANONYMOUS MAP_ANON
		#endif
	#endif
#endif

namespace Zodiac
{

// Public factory, declared in zodiac.h next to FromCFile. Returns nullptr on a
// reservation failure so the caller can handle it like FromCFile's null-FILE
// path (the hot-reload driver maps null -> SerializeFailed) rather than seeing
// an exception escape before its serialize try-block.
std::unique_ptr<zIFileDescriptor> FromMemory(std::size_t reserveBytes)
{
	try { return std::unique_ptr<zIFileDescriptor>(new zCMemoryFile(reserveBytes)); }
	catch(...) { return nullptr; }
}

void zCMemoryFile::Reserve(std::size_t reserveBytes)
{
	if(reserveBytes == 0) reserveBytes = kDefaultReserve;

#if defined(_WIN32)
	// Best-effort Windows path (engine ships Windows; dev/test is Linux). Commit
	// is demand-zero — physical pages are only faulted in on first touch.
	void * p = VirtualAlloc(nullptr, reserveBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if(p == nullptr) throw Exception(zE_BufferOverrun);
#else
	// Reserve address space only; MAP_NORESERVE + demand paging means no RAM (or
	// swap accounting) is committed until pages are actually written.
	void * p = mmap(nullptr, reserveBytes, PROT_READ | PROT_WRITE,
	                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if(p == MAP_FAILED) throw Exception(zE_BufferOverrun);
#endif

	m_base    = static_cast<uint8_t*>(p);
	m_reserve = reserveBytes;
}

zCMemoryFile::zCMemoryFile(std::size_t reserveBytes)
{
	Reserve(reserveBytes);
	m_stack.push_back(StackFrame{0u, ~0u, 0u, nullptr});
}

zCMemoryFile::zCMemoryFile(const std::vector<char> & initial, std::size_t reserveBytes)
{
	if(reserveBytes < initial.size()) reserveBytes = initial.size();
	Reserve(reserveBytes);
	if(!initial.empty())
		std::memcpy(m_base, initial.data(), initial.size());
	m_size = static_cast<uint32_t>(initial.size());
	m_stack.push_back(StackFrame{0u, ~0u, 0u, nullptr});
}

zCMemoryFile::~zCMemoryFile()
{
	if(m_base == nullptr) return;
#if defined(_WIN32)
	VirtualFree(m_base, 0, MEM_RELEASE);
#else
	munmap(m_base, m_reserve);
#endif
}

int zCMemoryFile::Read(void * ptr, uint size)
{
	const StackFrame & top = m_stack.back();

	// R2: clamp to the current sub-file end. A short read at a sub-file boundary
	// is a legitimate end-of-record (mirrors zCFile::Read) — never throws.
	if(static_cast<uint64_t>(m_pos) + size > top.end)
		size = (top.end > m_pos) ? (top.end - m_pos) : 0u;

	// Clamp to what has actually been written (mirrors fread's EOF short-count;
	// pages past m_size are zero-filled reservation, not real data).
	uint available = (m_pos < m_size) ? (m_size - m_pos) : 0u;
	uint n = std::min<uint>(size, available);

	if(n)
		std::memcpy(ptr, m_base + m_pos, n);
	m_pos += n;
	return static_cast<int>(n);
}

int zCMemoryFile::Write(const void * ptr, uint size)
{
	const StackFrame & top = m_stack.back();

	// W2: crossing a sub-file boundary on write is a logic error, not a
	// recoverable clamp — abort the save (mirrors zCFile::Write).
	if(static_cast<uint64_t>(m_pos) + size > top.end)
		throw Exception(zE_BadSubFileAddress);

	// Overrunning the reservation is a hard error surfaced as a Code at the
	// SaveToFile boundary — never a wild write. Grow the reserve if this fires
	// on real payloads.
	if(static_cast<uint64_t>(m_pos) + size > m_reserve)
		throw Exception(zE_BufferOverrun);

	if(size)
	{
		std::memcpy(m_base + m_pos, ptr, size);
		m_pos += size;
		if(m_pos > m_size) m_size = m_pos;   // extend logical EOF
	}
	return static_cast<int>(size);
}

void zCMemoryFile::seek(int offset, Flags flags)
{
	const StackFrame & top = m_stack.back();

	switch(flags)
	{
	case Flags::zFILE_BEGIN:
		offset += top.begin;
		break;
	case Flags::zFILE_CUR:
		offset += static_cast<int>(m_pos);
		break;
	case Flags::zFILE_END:
	{
		uint32_t back = static_cast<uint32_t>(std::min<uint64_t>(top.end, m_size));
		offset += static_cast<int>(back);
	}	break;
	default:
		break;
	}

	offset = std::max<int64_t>(top.begin, std::min<int64_t>(top.end, offset));
	m_pos = static_cast<uint32_t>(offset);
}

uint zCMemoryFile::tell() const { return m_pos - m_stack.back().begin; }

uint zCMemoryFile::SubFileOffset() const { return m_stack.back().begin; }

std::vector<char> zCMemoryFile::bytes() const
{
	const char * b = reinterpret_cast<const char*>(m_base);
	return std::vector<char>(b, b + m_size);
}

void zCMemoryFile::PushSubFile(uint offset, uint byteLength)
{
	uint32_t restore = m_pos;

	// read-type set to end of file (mirrors zCFile): resolve the root frame's
	// end to the logical size the first time a sub-file is entered on read.
	if(m_stack[0].end == ~0u)
		m_stack[0].end = m_size;

	byteLength = offset + byteLength;   // absolute end

	// <= (not <) so a record ending exactly at EOF or a zero-length record is
	// valid — matches zCFile::PushSubFile.
	if(!(offset <= byteLength && byteLength <= m_stack[0].end))
		throw Exception(zE_BadSubFileAddress);

	m_stack.push_back(StackFrame{offset, byteLength, restore, nullptr});
	m_pos = offset;
}

void zCMemoryFile::PushSubFile(uint * byteLength)
{
	uint32_t restore = m_pos;
	uint32_t at = m_size;   // seek END — write sub-files append at the logical EOF

	m_stack.push_back(StackFrame{at, ~0u, restore, byteLength});
	m_pos = at;
}

void zCMemoryFile::PopSubFile()
{
	if(m_stack.size() <= 1)
		return;

	StackFrame & top = m_stack.back();

	if(top.byteLength != nullptr)
		*top.byteLength = m_size - top.begin;   // final length of the appended record

	m_pos = top.restore;
	m_stack.pop_back();
}

}

#endif
