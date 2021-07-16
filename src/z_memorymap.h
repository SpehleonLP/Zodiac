#ifndef Z_MEMORYMAP_H
#define Z_MEMORYMAP_H
#ifdef HAVE_ZODIAC


namespace Zodiac
{

class zIFileDescriptor;

class zCMemoryMap
{
public:
	zCMemoryMap(zIFileDescriptor * descriptor);
	~zCMemoryMap();

	char	 const*		GetAddress() const { return (char*)m_contents; }
	unsigned long long  GetLength() const { return m_length; }

private:
	void			    * m_contents;
	unsigned long long	  m_length;
};

}

#endif
#endif // Z_MEMORYMAP_H
