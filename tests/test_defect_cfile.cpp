// Defect regression tests for the zCFile sub-file stack (z_cfile.cpp).
//
//   * PushSubFile bounds off-by-one (z_cfile.cpp:127): the window check uses a
//     strict `offset < end && end < fileSize`, which rejects a sub-file whose end
//     abuts EOF and any zero-length sub-file. Both are legitimate views.
//   * uint8_t stack-counter wrap (z_cfile.cpp:132 / z_cfile.h:50): stackSize and
//     stackPos are 8-bit; `stackSize += 16` wraps past 255 (244+16 == 260 -> 4),
//     so a deep nest reallocs the frame array SMALLER while stackPos is large and
//     the next frame write runs off the end of the heap block.
//
// The bounds cases are red on a stock build (they throw where they should not).
// The stack-wrap case is a heap-buffer-overflow, so it is red only under an
// ASan-instrumented library (cmake -DZODIAC_INSTRUMENT_LIB=ON), where the bad
// write aborts; on a stock build the corruption is silent. Both assert the
// correct post-fix behavior, so they go green once the arithmetic is fixed.
#include "zodiac.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>

using namespace Zodiac;

namespace
{
// Write `n` deterministic bytes into a fresh tmpfile and hand back a zCFile over it.
std::unique_ptr<zIFileDescriptor> MakeFileOfSize(FILE *& fp, int n)
{
	fp = tmpfile();
	auto file = FromCFile(fp);
	if(file)
	{
		std::vector<unsigned char> data((size_t)n);
		for(int i = 0; i < n; ++i) data[i] = (unsigned char)(i & 0xFF);
		file->Write(data.data(), (uint)n);
		file->seek(0, zFILE_BEGIN);
	}
	return file;
}

// Recursively open `depth` nested read sub-files, each a valid window inside the
// root, keeping every frame alive on the C stack simultaneously.
void NestReadSubFiles(zIFileDescriptor * file, int depth)
{
	if(depth == 0) return;
	zIFileDescriptor::ReadSubFile sub(file, 0, 8); // 0 < 8 < fileSize: a valid window
	NestReadSubFiles(file, depth - 1);
}
}

// #8 — a sub-file whose end lands exactly on EOF is a legal whole-region view; the
// strict `end < fileSize` check rejects it. Pre-fix: ReadSubFile ctor throws.
TEST(CFileSubFileBounds, SubFileEndingAtEofAccepted)
{
	FILE * fp = nullptr;
	auto file = MakeFileOfSize(fp, 16);
	ASSERT_NE(file, nullptr);

	// Window [0, 16) covers the whole 16-byte file and ends exactly at EOF.
	EXPECT_NO_THROW({
		zIFileDescriptor::ReadSubFile sub(file.get(), 0, 16);
		file->seek(0, zFILE_BEGIN);
		unsigned char buf[16];
		int n = file->Read(buf, sizeof(buf));
		EXPECT_EQ(n, 16);
	}) << "a sub-file ending exactly at EOF must be accepted";

	fclose(fp);
}

// #8 — a zero-length sub-file (e.g. an empty table section) is legal; the strict
// `offset < end` check with end == offset rejects it. Pre-fix: ctor throws.
TEST(CFileSubFileBounds, ZeroLengthSubFileAccepted)
{
	FILE * fp = nullptr;
	auto file = MakeFileOfSize(fp, 16);
	ASSERT_NE(file, nullptr);

	// Empty window [8, 8): offset in range, length 0.
	EXPECT_NO_THROW({
		zIFileDescriptor::ReadSubFile sub(file.get(), 8, 0);
	}) << "a zero-length sub-file must be accepted";

	fclose(fp);
}

// #10 — 260 nested sub-files drive the 8-bit stackSize past 255. Post-fix this
// completes cleanly; pre-fix, under -DZODIAC_INSTRUMENT_LIB=ON, the wrapped realloc
// + out-of-range frame write is an ASan heap-buffer-overflow that aborts here.
TEST(CFileSubFileBounds, DeepNestingDoesNotOverflowFrameStack)
{
	FILE * fp = nullptr;
	auto file = MakeFileOfSize(fp, 100);
	ASSERT_NE(file, nullptr);

	EXPECT_NO_THROW({
		NestReadSubFiles(file.get(), 260);
	}) << "deep sub-file nesting must not overflow the frame stack";

	fclose(fp);
}
