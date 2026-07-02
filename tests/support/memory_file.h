#ifndef ZODIAC_TESTS_SUPPORT_MEMORY_FILE_H
#define ZODIAC_TESTS_SUPPORT_MEMORY_FILE_H

#include <zodiac.h>
#include <vector>
#include <cstdint>

namespace zodiac_test
{

// A disk-free zIFileDescriptor backed by a std::vector<char>, mirroring the
// sub-file stack semantics of Zodiac::zCFile. GetFileDescriptor() returns -1
// (the malloc-fallback path in the reader's backing).
class zCMemoryFile : public Zodiac::zIFileDescriptor
{
public:
	zCMemoryFile();
	// Seeded read-only from an existing image (e.g. bytes produced by a save).
	explicit zCMemoryFile(std::vector<char> initial);
	~zCMemoryFile() override = default;

	// asIBinaryStream
	int Read(void * ptr, Zodiac::uint size) override;
	int Write(const void * ptr, Zodiac::uint size) override;

	// zIFileDescriptor
	void seek(int offset, Zodiac::Flags flags) override;
	Zodiac::uint tell() const override;
	Zodiac::uint SubFileOffset() const override;

	// Part B0 seam: not a real fd -> forces the malloc-fallback reader backing.
	int GetFileDescriptor() const { return -1; }

	const std::vector<char> & bytes() const { return m_buffer; }

protected:
	void PushSubFile(Zodiac::uint offset, Zodiac::uint byteLength) override;
	void PushSubFile(Zodiac::uint * byteLength) override;
	void PopSubFile() override;

private:
	struct StackFrame
	{
		uint32_t begin;
		uint32_t end;
		uint32_t restore;
		uint32_t * byteLength;
	};

	std::vector<char>       m_buffer;
	uint32_t                m_pos{0};   // absolute cursor into m_buffer
	std::vector<StackFrame> m_stack;
};

}

#endif // ZODIAC_TESTS_SUPPORT_MEMORY_FILE_H
