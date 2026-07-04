#ifndef Z_MEMORYFILE_H
#define Z_MEMORYFILE_H

#ifdef HAVE_ZODIAC

#include <zodiac.h>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace Zodiac
{

// An in-process zIFileDescriptor backed by a single anonymous memory
// reservation (mmap on POSIX / VirtualAlloc on Windows) — NOT a real file.
//
// Address space is reserved up front (cheap on 64-bit); physical pages are
// committed lazily by the kernel on first touch, so the reservation costs no
// RAM until written. The base address is stable for the descriptor's life, so
// there is never a realloc/copy (unlike a growing std::vector) and sub-file
// length back-patching is a plain in-place store.
//
// Sub-file stack semantics mirror Zodiac::zCFile exactly (writes append at the
// logical EOF; reads clamp/short at the sub-file end; a write past the sub-file
// end throws). GetFileDescriptor() returns -1 so the reader's backing takes its
// not-a-real-file (malloc/slurp) path.
//
// Growth past the reservation is a hard error (zE_BufferOverrun), never a
// crash: choose a reserve larger than any expected image. The default is
// generous because the reservation is address space, not committed RAM.
//
// NOTE: distinct from the vector-backed zodiac_test::zCMemoryFile in
// tests/support — that one keeps ASan redzone coverage for descriptor fuzzing;
// this one is the production RAM-handoff descriptor used by the hot-reload
// driver. The sub-file logic is intentionally duplicated (two backings).
class zCMemoryFile : public zIFileDescriptor
{
public:
	static constexpr std::size_t kDefaultReserve = std::size_t(256) << 20; // 256 MiB

	explicit zCMemoryFile(std::size_t reserveBytes = kDefaultReserve);
	// Seeded from an existing image (e.g. bytes produced by a prior save).
	explicit zCMemoryFile(const std::vector<char> & initial,
	                      std::size_t reserveBytes = kDefaultReserve);

	zCMemoryFile(zCMemoryFile const&) = delete;
	zCMemoryFile(zCMemoryFile &&) = delete;
	zCMemoryFile & operator=(zCMemoryFile const&) = delete;
	~zCMemoryFile() override;

	int Read(void * ptr, uint size) override;
	int Write(const void * ptr, uint size) override;

	void seek(int offset, Flags flags) override;
	uint tell() const override;
	uint SubFileOffset() const override;

	int GetFileDescriptor() const override { return -1; }

	// Copy of the logical [0, size) image written so far.
	std::vector<char> bytes() const;

protected:
	void PushSubFile(uint offset, uint byteLength) override;
	void PushSubFile(uint * byteLength) override;
	void PopSubFile() override;

	struct StackFrame
	{
		uint32_t begin;
		uint32_t end;
		uint32_t restore;
		uint32_t * byteLength;
	};

	void Reserve(std::size_t reserveBytes);

	uint8_t *               m_base{};     // stable reservation base
	std::size_t             m_reserve{};  // reserved address-space bytes
	uint32_t                m_pos{0};     // absolute cursor
	uint32_t                m_size{0};    // logical EOF (high-water of writes)
	std::vector<StackFrame> m_stack;
};

}

#endif

#endif // Z_MEMORYFILE_H
