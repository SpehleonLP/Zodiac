#include "z_cfile.h"
#ifdef HAVE_ZODIAC
#include <stdexcept>
#include <cassert>

namespace Zodiac
{

zIFileDescriptor * FromCFile(FILE * fp)
{
	if(fp == nullptr) return nullptr;
	return new zCFile(fp, false);
}

zIFileDescriptor * FromCFile(FILE ** _it_)
{
	FILE * fp = *_it_;
	*_it_ = nullptr;

	if(fp == nullptr) return nullptr;
	return new zCFile(fp, true);
}

zCFile::zCFile(FILE* file, bool ownsFile) :
	file(file), ownsFile(ownsFile)
{
	streamPos = ftell(file);

	stackSize = 4;
	stackPos = 0;
	stack = (StackFrame*)malloc(sizeof(StackFrame)*stackSize);
	stack[0].begin = 0;
	stack[1].end = ~0u;
	stack[2].restore = 0u;
}

zCFile::~zCFile()
{
	if(ownsFile)
		fclose(file);
}


int zCFile::Read(void *ptr, uint size)
{
	auto end = size + streamPos;

	if(end > stack[stackPos].end)
	{
		errno = EIO;
		size  = stack[stackPos].end - streamPos;
	}

	streamPos += size;
	int r = fread(ptr, 1, size, file);

	if((uint32_t)r != size)	streamPos = ftell(file);

	return r;
}

int zCFile::Write(const void *ptr, uint size)
{
	auto end = size + streamPos;

	if(end > stack[stackPos].end)
	{
		errno = EIO;
		size  = stack[stackPos].end - streamPos;
	}

	streamPos += size;
	int r = fwrite(ptr, 1, size, file);

	if((uint32_t)r != size)	streamPos = ftell(file);

	return r;
}

void zCFile::seek(int offset, Flags flags)
{
	switch(flags)
	{
	case Flags::zFILE_BEGIN:
		offset += stack[stackPos].begin;

		if((uint32_t)offset < stack[stackPos].begin) offset = stack[stackPos].begin;
		if((uint32_t)offset > stack[stackPos].end  ) offset = stack[stackPos].end;

		fseek(file, offset, SEEK_SET);
		break;
	case Flags::zFILE_CUR:
		offset += streamPos;

		if((uint32_t)offset < stack[stackPos].begin) offset = stack[stackPos].begin;
		if((uint32_t)offset > stack[stackPos].end  ) offset = stack[stackPos].end;

		fseek(file, offset, SEEK_CUR);
		break;
	case Flags::zFILE_END:
		offset += stack[stackPos].end;

		if((uint32_t)offset < stack[stackPos].begin) offset = stack[stackPos].begin;
		if((uint32_t)offset > stack[stackPos].end  ) offset = stack[stackPos].end;

		fseek(file, offset, SEEK_END);
		break;
	default:
		break;
	}

	streamPos = offset;
}

uint zCFile::tell() const
{
	assert(streamPos == ftell(file));
	return streamPos - stack[stackPos].begin;
}

int      zCFile::GetFileDescriptor()
{
	return fileno(file);
}

void zCFile::PushSubFile(uint offset, uint byteLength)
{
	offset = offset+stack[stackPos].begin;
	byteLength = offset+byteLength;

//need to check becuase of overflows
	if(!(offset < byteLength
	&& offset < stack[stackPos].begin
	&& byteLength < stack[stackPos].end))
	{
		throw std::logic_error("bad sub file address");
	}

	if(stackPos+1 >= stackSize)
	{
		stackSize += 4;
		stack	   = (StackFrame*)realloc(stack, sizeof(StackFrame)*stackSize*3);
	}

	assert(ftell(file) == streamPos + stack[stackPos].begin);

	stackPos++;
	stack[stackPos].begin	= offset;
	stack[stackPos].end		= byteLength;
	stack[stackPos].restore = streamPos + stack[stackPos-1].begin;
	stack[stackPos].byteLength = nullptr;

	fseek(file, offset, SEEK_SET);
	streamPos = offset;
}

void zCFile::PushSubFile(uint offset, uint * byteLength)
{
	offset = offset+stack[stackPos].begin;
	byteLength = offset+byteLength;

//need to check becuase of overflows
	if(offset < stack[stackPos].begin)
	{
		throw std::logic_error("bad sub file address");
	}

	if(stackPos+1 >= stackSize)
	{
		stackSize += 4;
		stack	   = (StackFrame*)realloc(stack, sizeof(StackFrame)*stackSize*3);
	}

	assert(ftell(file) == streamPos + stack[stackPos].begin);

	stackPos++;
	stack[stackPos].begin	= offset;
	stack[stackPos].end		= ~0;
	stack[stackPos].restore = streamPos + stack[stackPos-1].begin;
	stack[stackPos].byteLength = byteLength;

	fseek(file, offset, SEEK_SET);
	streamPos = offset;
}

void zCFile::PopSubFile()
{
	if(stackPos > 0)
	{
		fseek(file, stack[stackPos].restore, SEEK_SET);
		--stackPos;
		streamPos = streamPos - stack[stackPos].begin;

		assert(ftell(file) == streamPos + stack[stackPos].begin);
	}
}



}

#endif
