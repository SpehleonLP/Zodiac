#ifndef Z_MEMORYMAP_H
#define Z_MEMORYMAP_H
#ifdef HAVE_ZODIAC


namespace Zodiac
{

class zIFileDescriptor;

// Read-only view of an entire file. If the descriptor exposes a real OS file
// descriptor (GetFileDescriptor() >= 0) the whole file is mapped read-only via
// mmap (demand-paged, zero up-front copy); otherwise it falls back to a
// malloc'd buffer slurped from the stream. The dtor tears down whichever path
// was taken. Despite the name it is NOT always an mmap (kept for churn reasons).
class zCMemoryMap
{
public:
	zCMemoryMap(zIFileDescriptor * descriptor);
	~zCMemoryMap();

	char	 const*		GetAddress() const { return (char*)m_contents; }
	unsigned long long  GetLength() const { return m_length; }

private:
	void			    * m_contents{};
	unsigned long long	  m_length{};
	bool				  m_isMapped{}; // true => munmap in dtor; false => free
};

}

#endif
#endif // Z_MEMORYMAP_H
