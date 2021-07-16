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
		size  = stack[stackPos].end - ftell(file);
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

		if((uint32_t)offset < stack[stackPos].begin) offset = stack[stackPos].begin;
		if((uint32_t)offset > stack[stackPos].end  ) offset = stack[stackPos].end;

		fseek(file, offset, SEEK_SET);
		break;
	case Flags::zFILE_CUR:
		offset += ftell(file);

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
}

uint zCFile::tell() const
{
	return ftell(file) - stack[stackPos].begin;
}

uint zCFile::SubFileOffset() const { return stack[stackPos].begin; }

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

	stackPos++;
	stack[stackPos].begin	= offset;
	stack[stackPos].end		= byteLength;
	stack[stackPos].restore = ftell(file);
	stack[stackPos].byteLength = nullptr;

	fseek(file, offset, SEEK_SET);
}

void zCFile::PushSubFile(uint * byteLength)
{
	if(stackPos+1 >= stackSize)
	{
		stackSize += 4;
		stack	   = (StackFrame*)realloc(stack, sizeof(StackFrame)*stackSize*3);
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
		fseek(file, stack[stackPos].restore, SEEK_SET);
		--stackPos;

	}
}



}

#endif
