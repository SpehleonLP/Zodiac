#include "z_cfile.h"
#ifdef HAVE_ZODIAC
#include "z_zodiacexception.h"
#include <stdexcept>
#include <cassert>

namespace Zodiac
{

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
	stack[0].begin = 0;
	stack[0].end = ~0u;
	stack[0].restore = 0u;
	stack[0].byteLength = nullptr;
}

zCFile::~zCFile()
{
	if(ownsFile)
		fclose(file);
}


int zCFile::Read(void *ptr, uint size)
{
	auto end = size + ftell(file);

	if(end > stack[stackPos].end)
	{
		errno = EIO;
		size  = stack[stackPos].end - ftell(file);
	}

	int r = fread(ptr, 1, size, file);

	return r;
}

int zCFile::Write(const void *ptr, uint size)
{
	auto end = size + ftell(file);

	if(end > stack[stackPos].end)
	{
		errno = EIO;
		size  = std::max<int64_t>((int64_t)stack[stackPos].end - ftell(file), 0);
	}

	int r = fwrite(ptr, 1, size, file);

	return r;
}

void zCFile::seek(int offset, Flags flags)
{
	switch(flags)
	{
	case Flags::zFILE_BEGIN:
		offset += stack[stackPos].begin;
		break;
	case Flags::zFILE_CUR:
		offset += ftell(file);
		break;
	case Flags::zFILE_END:
	{
		fseek(file, 0, SEEK_END);
		size_t back = std::min<size_t>(stack[stackPos].end, ftell(file));
		offset += back;
	}	break;
	default:
		break;
	}

	offset = std::max(stack[stackPos].begin, std::min<uint32_t>(stack[stackPos].end, offset));
	fseek(file, offset, SEEK_SET);
}

uint zCFile::tell() const
{
	return ftell(file) - stack[stackPos].begin;
}

uint zCFile::SubFileOffset() const { return stack[stackPos].begin; }

void zCFile::PushSubFile(uint offset, uint byteLength)
{
	auto restore = ftell(file);

//read type set to end of file
	if(stack[0].end == ~0u)
	{
		fseek(file, 0, SEEK_END);
		stack[0].end = ftell(file);
	}

	byteLength = offset+byteLength;

//need to check becuase of overflows
	if(!(offset < byteLength && byteLength < stack[0].end))
	{
		throw Exception(zE_BadSubFileAddress);
	}

	if(stackPos+1 >= stackSize)
	{
		stackSize += 16;
		stack	   = (StackFrame*)realloc(stack, sizeof(StackFrame)*stackSize);
	}

	stackPos++;
	stack[stackPos].begin	= offset;
	stack[stackPos].end		= byteLength;
	stack[stackPos].restore = restore;
	stack[stackPos].byteLength = nullptr;

	fseek(file, offset, SEEK_SET);
}

void zCFile::PushSubFile(uint * byteLength)
{
	if(stackPos+1 >= stackSize)
	{
		stackSize += 4;
		stack	   = (StackFrame*)realloc(stack, sizeof(StackFrame)*stackSize);
	}

	auto restore = ftell(file);
	fseek(file, 0, SEEK_END);

	stackPos++;
	stack[stackPos].begin	=  ftell(file);
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
			fseek(file, 0, SEEK_END);
			*stack[stackPos].byteLength = ftell(file) - stack[stackPos].begin;
		}


		fseek(file, stack[stackPos].restore, SEEK_SET);
		--stackPos;
	}
}



}

#endif
