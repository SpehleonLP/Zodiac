#include "z_cfile.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacexception.h"
#include <stdexcept>
#include <cassert>
#include <cstdio>

namespace Zodiac
{

// S1: fseek/ftell are unchecked on a non-seekable stream (pipe/FIFO/socket) —
// every call fails and Zodiac would otherwise write garbage offsets while
// reporting success (ftell's -1L folds into unsigned arithmetic). Route every
// seek/tell in this TU through these guards so a hard I/O error throws a Code
// that surfaces at the SaveToFile/LoadFromFile boundary.
static long CheckedTell(FILE * f)
{
	long t = ftell(f);
	if(t == -1L) throw Exception(zE_IOError);
	return t;
}

static void CheckedSeek(FILE * f, long off, int whence)
{
	if(fseek(f, off, whence) != 0) throw Exception(zE_IOError);
}

std::unique_ptr<zIFileDescriptor> FromCFile(FILE * fp) { return zCFile::Factory(fp); }
std::unique_ptr<zIFileDescriptor> FromCFile(FILE ** _it_) { return zCFile::Factory(_it_); }

std::unique_ptr<zIFileDescriptor> zCFile::Factory(FILE * fp)
{
	if(fp == nullptr) return nullptr;
	return std::unique_ptr<zIFileDescriptor>(new zCFile(fp, false));
}

std::unique_ptr<zIFileDescriptor> zCFile::Factory(FILE ** _it_)
{
	FILE * fp = *_it_;
	*_it_ = nullptr;

	if(fp == nullptr) return nullptr;
	return std::unique_ptr<zIFileDescriptor>(new zCFile(fp, true));
}

zCFile::zCFile(FILE* file, bool ownsFile) :
	file(file), ownsFile(ownsFile)
{
	stackSize = 4;
	stackPos = 0;
	stack = (StackFrame*)malloc(sizeof(StackFrame)*stackSize);
	// M1: guard the ctor malloc. The dtor will NOT run for a throwing ctor, so
	// close the owned FILE* here before throwing to avoid leaking it.
	if(stack == nullptr)
	{
		if(ownsFile) fclose(file);
		throw Exception(zE_BufferOverrun);
	}
	stack[0].begin = 0;
	stack[0].end = ~0u;
	stack[0].restore = 0u;
	stack[0].byteLength = nullptr;
}

zCFile::~zCFile()
{
	free(stack);

	// F2: best-effort close. Finish() already flushed-and-checked via Flush()
	// inside the SaveToFile try-block, so any real ENOSPC/EIO already surfaced
	// there as a Code. The destructor runs after SaveToFile returned and cannot
	// report an error, so we do not check fclose's result here.
	if(ownsFile)
		fclose(file);
}


int zCFile::Read(void *ptr, uint size)
{
	// R2: unlike Write (which THROWS on a sub-file overrun, W2), Read returns a
	// possibly-SHORT count BY DESIGN. A short read at a sub-file boundary can be a
	// legitimate end-of-record, so this must not throw. Callers that need an exact
	// count must verify it via the `sizeof(x) != Read(&x)` idiom (as the context
	// loader in z_zodiaccontext.cpp now does). Do NOT "fix" this into a throw — it
	// would break intended partial reads.
	// A hard ftell FAILURE here is a genuine I/O error and throws — this is
	// DISTINCT from the by-design short-count/sub-file clamp below (R2): short
	// reads still return a short count; only a broken descriptor throws.
	long pos = CheckedTell(file);
	auto end = size + pos;

	if(end > stack[stackPos].end)
	{
		errno = EIO;
		size  = stack[stackPos].end - CheckedTell(file);
	}

	int r = fread(ptr, 1, size, file);

	return r;
}

int zCFile::Write(const void *ptr, uint size)
{
	auto end = size + CheckedTell(file);

	// W2: crossing a sub-file boundary on WRITE is a logic error, not a
	// recoverable clamp. Abort the save rather than silently truncating.
	if(end > stack[stackPos].end)
	{
		throw Exception(zE_BadSubFileAddress);
	}

	int r = fwrite(ptr, 1, size, file);

	// W1: a short or failed write must abort the save (mapped to a Code at the
	// SaveToFile boundary), never silently drop bytes.
	if((uint)r != size || ferror(file))
	{
		throw Exception(zE_IOError);
	}

	return r;
}

bool zCFile::Flush()
{
	// F1: flush buffered output to the OS and surface any deferred ENOSPC/EIO.
	// Throws (rather than returning false) so it maps to a Code through the
	// SaveToFile try-block, consistent with Write; returns true on success to
	// honor the bool contract for any caller that checks it.
	if(fflush(file) != 0 || ferror(file))
	{
		throw Exception(zE_IOError);
	}

	return true;
}

void zCFile::seek(int offset, Flags flags)
{
	switch(flags)
	{
	case Flags::zFILE_BEGIN:
		offset += stack[stackPos].begin;
		break;
	case Flags::zFILE_CUR:
		offset += CheckedTell(file);
		break;
	case Flags::zFILE_END:
	{
		CheckedSeek(file, 0, SEEK_END);
		size_t back = std::min<size_t>(stack[stackPos].end, CheckedTell(file));
		offset += back;
	}	break;
	default:
		break;
	}

	offset = std::max(stack[stackPos].begin, std::min<uint32_t>(stack[stackPos].end, offset));
	CheckedSeek(file, offset, SEEK_SET);
}

uint zCFile::tell() const
{
	long t = CheckedTell(file);
	return (uint)((uint32_t)t - stack[stackPos].begin);
}

uint zCFile::SubFileOffset() const { return stack[stackPos].begin; }

int zCFile::GetFileDescriptor() const { return file ? fileno(file) : -1; }

void zCFile::PushSubFile(uint offset, uint byteLength)
{
	auto restore = CheckedTell(file);

//read type set to end of file
	if(stack[0].end == ~0u)
	{
		CheckedSeek(file, 0, SEEK_END);
		stack[0].end = CheckedTell(file);
	}

	byteLength = offset+byteLength;

//need to check becuase of overflows
	if(!(offset <= byteLength && byteLength <= stack[0].end))
	{
		throw Exception(zE_BadSubFileAddress);
	}

	if(stackPos+1 >= stackSize)
	{
		// M2: realloc-to-temp so a failure neither leaks the old block nor
		// null-derefs. Commit stackSize only after success so a throw leaves
		// stack/stackSize consistent for the dtor's free.
		uint32_t newSize = stackSize + 16;
		StackFrame * grown = (StackFrame*)realloc(stack, sizeof(StackFrame)*newSize);
		if(grown == nullptr) throw Exception(zE_BufferOverrun);  // old stack still valid; dtor frees it
		stack     = grown;
		stackSize = newSize;
	}

	stackPos++;
	stack[stackPos].begin	= offset;
	stack[stackPos].end		= byteLength;
	stack[stackPos].restore = restore;
	stack[stackPos].byteLength = nullptr;

	CheckedSeek(file, offset, SEEK_SET);
}

void zCFile::PushSubFile(uint * byteLength)
{
	if(stackPos+1 >= stackSize)
	{
		// M2: realloc-to-temp (see PushSubFile(uint,uint)); commit stackSize
		// only after a successful realloc.
		uint32_t newSize = stackSize + 4;
		StackFrame * grown = (StackFrame*)realloc(stack, sizeof(StackFrame)*newSize);
		if(grown == nullptr) throw Exception(zE_BufferOverrun);  // old stack still valid; dtor frees it
		stack     = grown;
		stackSize = newSize;
	}

	auto restore = CheckedTell(file);
	CheckedSeek(file, 0, SEEK_END);

	stackPos++;
	stack[stackPos].begin	=  CheckedTell(file);
	stack[stackPos].end		= ~0;
	stack[stackPos].restore = restore;
	stack[stackPos].byteLength = byteLength;
}

void zCFile::PopSubFile()
{
	if(stackPos > 0)
	{
		if(stack[stackPos].byteLength != nullptr)
		{
			CheckedSeek(file, 0, SEEK_END);
			*stack[stackPos].byteLength = CheckedTell(file) - stack[stackPos].begin;
		}


		CheckedSeek(file, stack[stackPos].restore, SEEK_SET);
		--stackPos;
	}
}



}

#endif
