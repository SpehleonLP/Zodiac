#include "memory_file.h"
#include "z_zodiacexception.h"

#include <algorithm>
#include <cstring>

using Zodiac::Flags;
using Zodiac::uint;

namespace zodiac_test
{

zCMemoryFile::zCMemoryFile()
{
	m_stack.push_back(StackFrame{0u, ~0u, 0u, nullptr});
}

zCMemoryFile::zCMemoryFile(std::vector<char> initial)
	: m_buffer(std::move(initial))
{
	m_stack.push_back(StackFrame{0u, ~0u, 0u, nullptr});
}

int zCMemoryFile::Read(void * ptr, uint size)
{
	StackFrame & top = m_stack[m_stack.size() - 1];

	// clamp to the current sub-file end (mirrors zCFile::Read)
	if((uint64_t)m_pos + size > top.end)
		size = (top.end > m_pos) ? (top.end - m_pos) : 0u;

	// clamp to what actually exists in the buffer
	uint available = (m_pos < m_buffer.size()) ? (uint)(m_buffer.size() - m_pos) : 0u;
	uint n = std::min<uint>(size, available);

	if(n)
		memcpy(ptr, m_buffer.data() + m_pos, n);

	m_pos += n;
	return (int)n;
}

int zCMemoryFile::Write(const void * ptr, uint size)
{
	StackFrame & top = m_stack[m_stack.size() - 1];

	// clamp to the current sub-file end (mirrors zCFile::Write)
	if((uint64_t)m_pos + size > top.end)
		size = (top.end > m_pos) ? (top.end - m_pos) : 0u;

	if(size)
	{
		if((uint64_t)m_pos + size > m_buffer.size())
			m_buffer.resize((size_t)m_pos + size);
		memcpy(m_buffer.data() + m_pos, ptr, size);
		m_pos += size;
	}

	return (int)size;
}

void zCMemoryFile::seek(int offset, Flags flags)
{
	StackFrame & top = m_stack[m_stack.size() - 1];

	switch(flags)
	{
	case Flags::zFILE_BEGIN:
		offset += top.begin;
		break;
	case Flags::zFILE_CUR:
		offset += m_pos;
		break;
	case Flags::zFILE_END:
	{
		uint32_t back = std::min<uint64_t>(top.end, m_buffer.size());
		offset += back;
	}	break;
	default:
		break;
	}

	offset = std::max<int64_t>(top.begin, std::min<int64_t>(top.end, offset));
	m_pos = (uint32_t)offset;
}

uint zCMemoryFile::tell() const
{
	return m_pos - m_stack[m_stack.size() - 1].begin;
}

uint zCMemoryFile::SubFileOffset() const
{
	return m_stack[m_stack.size() - 1].begin;
}

void zCMemoryFile::PushSubFile(uint offset, uint byteLength)
{
	uint32_t restore = m_pos;

	// read-type set to end of file (mirrors zCFile)
	if(m_stack[0].end == ~0u)
		m_stack[0].end = (uint32_t)m_buffer.size();

	byteLength = offset + byteLength;

	if(!(offset < byteLength && byteLength < m_stack[0].end))
		throw Zodiac::Exception(Zodiac::zE_BadSubFileAddress);

	m_stack.push_back(StackFrame{offset, byteLength, restore, nullptr});
	m_pos = offset;
}

void zCMemoryFile::PushSubFile(uint * byteLength)
{
	uint32_t restore = m_pos;
	uint32_t at = (uint32_t)m_buffer.size();   // seek END

	m_stack.push_back(StackFrame{at, ~0u, restore, byteLength});
	m_pos = at;
}

void zCMemoryFile::PopSubFile()
{
	if(m_stack.size() <= 1)
		return;

	StackFrame & top = m_stack[m_stack.size() - 1];

	if(top.byteLength != nullptr)
		*top.byteLength = (uint32_t)m_buffer.size() - top.begin;

	m_pos = top.restore;
	m_stack.pop_back();
}

}
