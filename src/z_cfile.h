#ifndef Z_CFILE_H
#define Z_CFILE_H

#ifdef HAVE_ZODIAC

#include <zodiac.h>
#include <memory>

namespace Zodiac
{

class zCFile : public zIFileDescriptor
{
public:
	static std::unique_ptr<zIFileDescriptor> Factory(FILE *);
	static std::unique_ptr<zIFileDescriptor> Factory(FILE *&&);

	zCFile() = delete;
	zCFile(zCFile const&) = delete;
	zCFile(zCFile &&) = delete;
	virtual ~zCFile();

	int Read(void *ptr, uint size) override;
	int Write(const void *ptr, uint size) override;

	void seek(int, Flags) override;
	uint tell() const override;
	int      GetFileDescriptor() override;

protected:
friend zIFileDescriptor * FromCFile(FILE *);
friend zIFileDescriptor * FromCFile(FILE **);
	zCFile(FILE*, bool owns);

	void PushSubFile(uint offset, uint byteLength) override;
	void PushSubFile(uint offset, uint * byteLength) override;
	void PopSubFile() override;

	struct StackFrame {
		uint32_t begin;
		uint32_t end;
		uint32_t restore;
		uint32_t * byteLength;
	};

	FILE * file{};
	bool   ownsFile{};
	uint8_t stackSize{};
	uint8_t stackPos{};
	uint streamPos{};
	StackFrame * stack{};
};


}

#endif

#endif // Z_CFILE_H
