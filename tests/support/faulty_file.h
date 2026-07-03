#ifndef ZODIAC_TESTS_SUPPORT_FAULTY_FILE_H
#define ZODIAC_TESTS_SUPPORT_FAULTY_FILE_H

#include "memory_file.h"

namespace zodiac_test
{

// A fault-injecting zIFileDescriptor. Subclasses zCMemoryFile so it inherits
// the vector-backed store and sub-file stack semantics, then intercepts Write
// and Flush to throw the SAME Zodiac Codes that production zCFile now throws
// (zE_IOError / zE_BadSubFileAddress). A test written against this double
// therefore matches production behavior.
class zCFaultyFile : public zCMemoryFile
{
public:
	using zCMemoryFile::zCMemoryFile;

	// After a cumulative n bytes have been accepted by Write, the next Write
	// that would exceed n throws zE_IOError (a short/failed write).
	void failWriteAfterBytes(uint32_t n) { m_writeCap = n; m_capActive = true; }
	// Flush() throws zE_IOError.
	void failOnFlush()                   { m_failFlush = true; }

	// After a cumulative n bytes have been returned by Read, every further Read
	// truncates (returns a short/zero count) — simulating a truncated stream.
	// Read never throws (mirrors production zCFile::Read); the CALLER's
	// sizeof()!=Read() check is what turns the short count into zE_EndOfFile.
	void failReadAfterBytes(uint32_t n) { m_readCap = n; m_readCapActive = true; }
	// Cumulative bytes returned by Read so far (lets a test arm the read-fault
	// exactly at a chosen load phase, e.g. right before LoadContext).
	uint32_t bytesRead() const { return m_read; }

	// seek()/tell() throw zE_IOError (mirrors a non-seekable/broken descriptor).
	void failSeek() { m_failSeek = true; }

	int  Write(const void * ptr, Zodiac::uint size) override;
	bool Flush() override;
	int  Read(void * ptr, Zodiac::uint size) override;

	void         seek(int offset, Zodiac::Flags flags) override;
	Zodiac::uint tell() const override;

private:
	uint32_t m_written{0};
	uint32_t m_writeCap{0};
	bool     m_capActive{false};
	bool     m_failFlush{false};
	uint32_t m_read{0};
	uint32_t m_readCap{0};
	bool     m_readCapActive{false};
	bool     m_failSeek{false};
};

}

#endif // ZODIAC_TESTS_SUPPORT_FAULTY_FILE_H
