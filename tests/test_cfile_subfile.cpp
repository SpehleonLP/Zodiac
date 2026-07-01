// Spec test 2: exercise the real zCFile over a tmpfile() (no AngelScript).
#include "zodiac.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>

using namespace Zodiac;

TEST(CFileSubFile, SeekTellRoundTrip)
{
	FILE * fp = tmpfile();
	ASSERT_NE(fp, nullptr);
	auto file = FromCFile(fp); // non-owning; we fclose below
	ASSERT_NE(file, nullptr);

	const int values[4] = {11, 22, 33, 44};
	for(int v : values)
		EXPECT_EQ(file->Write(&v), (int)sizeof(int));

	EXPECT_EQ(file->tell(), sizeof(values));

	file->seek(0, zFILE_BEGIN);
	EXPECT_EQ(file->tell(), 0u);

	// seek to the third int and read it back
	file->seek(2 * (int)sizeof(int), zFILE_BEGIN);
	EXPECT_EQ(file->tell(), 2 * sizeof(int));

	int got = 0;
	EXPECT_EQ(file->Read(&got), (int)sizeof(int));
	EXPECT_EQ(got, 33);
	EXPECT_EQ(file->tell(), 3 * sizeof(int));

	// seek to end via zFILE_END
	file->seek(0, zFILE_END);
	EXPECT_EQ(file->tell(), sizeof(values));

	fclose(fp);
}

TEST(CFileSubFile, WriteSubFileRestoresPosition)
{
	FILE * fp = tmpfile();
	ASSERT_NE(fp, nullptr);
	auto file = FromCFile(fp);
	ASSERT_NE(file, nullptr);

	int a = 1;
	EXPECT_EQ(file->Write(&a), (int)sizeof(int)); // file is now 4 bytes, pos == 4

	const uint before = file->tell();
	EXPECT_EQ(before, sizeof(int));

	uint subLen = 0;
	{
		zIFileDescriptor::WriteSubFile sub(file.get(), &subLen);
		int x = 99, y = 88;
		EXPECT_EQ(file->Write(&x), (int)sizeof(int));
		EXPECT_EQ(file->Write(&y), (int)sizeof(int));
	} // PopSubFile: writes subLen, restores position

	EXPECT_EQ(subLen, 2 * sizeof(int));
	EXPECT_EQ(file->tell(), before);

	fclose(fp);
}

TEST(CFileSubFile, ReadPastSubFileEndTruncates)
{
	FILE * fp = tmpfile();
	ASSERT_NE(fp, nullptr);
	auto file = FromCFile(fp);
	ASSERT_NE(file, nullptr);

	unsigned char data[20];
	for(int i = 0; i < 20; ++i)
		data[i] = (unsigned char)i;
	EXPECT_EQ(file->Write(data, 20), 20);

	{
		// sub-file window is [4, 12): offset 4, length 8
		zIFileDescriptor::ReadSubFile sub(file.get(), 4, 8);
		file->seek(0, zFILE_BEGIN); // begin of the sub-file (absolute 4)

		unsigned char buf[100];
		memset(buf, 0xAA, sizeof(buf));

		// ask for far more than the sub-file holds -> must truncate to 8
		int n = file->Read(buf, (int)sizeof(buf));
		EXPECT_EQ(n, 8);

		for(int i = 0; i < 8; ++i)
			EXPECT_EQ(buf[i], data[4 + i]);
	}

	fclose(fp);
}
