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

	int  Write(const void * ptr, Zodiac::uint size) override;
	bool Flush() override;

private:
	uint32_t m_written{0};
	uint32_t m_writeCap{0};
	bool     m_capActive{false};
	bool     m_failFlush{false};
};

}

#endif // ZODIAC_TESTS_SUPPORT_FAULTY_FILE_H
