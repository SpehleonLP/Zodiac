#ifndef ZODIAC_H
#define ZODIAC_H

#ifdef HAVE_ZODIAC

#ifndef ANGELSCRIPT_H
// Avoid having to inform include path if header is already include before
#include <angelscript.h>
#endif

#include <exception>
#include <cstdio>
#include <memory>


namespace Zodiac
{

class zIZodiac;
class zIFileDescriptor;
class zIZodiacReader;
class zIZodiacWriter;

enum zZodiacProp
{
	zZP_SAVE_BYTECODE,
	zZP_STRIP_DEBUGINFO,

};

enum Code
{
	zE_Success							= 0,

	zE_CastingException					= -1,
	zE_BufferOverrun					= -2,
	zE_BadObjectAddress					= -3,
	zE_BadFunctionInfo					= -4,

	zE_DuplicateObjectAddress			= -5,
	zE_ObjectRestoreTypeMismatch		= -6,
	zE_ObjectUnserializable				= -7,
	zE_BadTypeId						= -8,

	zE_InconsistentObjectType			= -9,
	zE_InconsistentObjectOwnership		= -10,
	zE_OwnerNotEncoded					= -11,
	zE_UnknownEncodingProtocol			= -12,

	zE_ModuleDoesNotExist				= -13,
	zE_DoubleLoad						= -14,
	zE_CantSaveContextWithoutBytecode	= -15,
	zE_ContextNotSuspended				= -16,

	zE_DuplicatePropertyAddress			= -17,
	zE_UnableToRestoreProperty			= -18,

	zE_BadSubFileAddress				= -19,
	zE_EndOfFile						= -20,
	zE_BadFileType						= -21,

	zE_AlreadyLoading					= -22,
	zE_AlreadySaving					= -23,
	zE_Total							= 24
};

enum zTYPEID
{
	zTYPEID_OBJHANDLE = asTYPEID_OBJHANDLE|asTYPEID_HANDLETOCONST,
	zTYPEID_OBJECT = asTYPEID_MASK_OBJECT|asTYPEID_MASK_SEQNBR,
};


enum Flags
{
	zFILE_BEGIN,
	zFILE_CUR,
	zFILE_END
};

typedef unsigned char  ubyte;
typedef unsigned short uword;
typedef unsigned int   uint;
typedef unsigned long long ulong;


typedef void (*zFUNCTION_t)(void*);
typedef void (*zREADER_FUNC_t)(zIZodiacReader *, void*);
typedef void (*zWRITER_FUNC_t)(zIZodiacWriter *, void*);

void ZodiacLoad(zIZodiacReader *, std::nullptr_t*, int &);
void ZodiacSave(zIZodiacWriter *, std::nullptr_t*, int &);

std::unique_ptr<zIZodiac> zCreateZodiac(asIScriptEngine * engine);

typedef void (*zSAVE_FUNC_t)(zIZodiacWriter *, void const*, int &);
typedef void (*zLOAD_FUNC_t)(zIZodiacReader *, void *, int &);


#define ZODIAC_GETSAVEFUNC(T) (static_cast<void (*)(Zodiac::zIZodiacWriter *, T const*, int &)>(ZodiacSave))
#define ZODIAC_GETLOADFUNC_R(T) (static_cast<void (*)(Zodiac::zIZodiacReader *, T**, int &)>(ZodiacLoad))
#define ZODIAC_GETLOADFUNC_V(T) (static_cast<void (*)(Zodiac::zIZodiacReader *, T*, int &)>(ZodiacLoad))


class zIZodiac
{
public:
	virtual ~zIZodiac() = default;

	virtual asIScriptEngine * GetEngine() const = 0;

//atomic
	virtual bool    IsInProgress() const = 0;
//atomic
	virtual float   Progress() const = 0;

	virtual void SetUserData(void *) = 0;
	virtual void * GetUserData() const = 0;

	virtual void    SetProperty(zZodiacProp property, bool value) = 0;
	virtual bool    GetProperty(zZodiacProp property) const = 0;

// progress / total steps for progress bar.
	virtual Code    SaveToFile(zIFileDescriptor *) = 0;
	virtual Code    LoadFromFile(zIFileDescriptor *) = 0;

	virtual void  SetPreRestoreCallback(zFUNCTION_t callback) = 0;
	virtual void  SetPreSavingCallback(zFUNCTION_t callback) = 0;

	virtual void  SetReadSaveDataCallback(zREADER_FUNC_t callback) = 0;
	virtual void  SetWriteSaveDataCallback(zWRITER_FUNC_t callback) = 0;

	virtual void  SetPostRestoreCallback(zFUNCTION_t callback) = 0;
	virtual void  SetPostSavingCallback(zFUNCTION_t callback) = 0;

	virtual int   GetAsTypeIdFromZTypeId(int asTypeId) const = 0;
	virtual int   GetZTypeIdFromAsTypeId(int asTypeId) const = 0;

	virtual const char * GetErrorString() = 0;
	virtual Code		 GetErrorCode() = 0;

	//does not copy string, assumes read only memory
	template<typename T>
	int   RegisterValueType(uint typeId, uint byteLength, const char * name, void (*save)(zIZodiacWriter *, T const*, int &), void (*load)(zIZodiacReader *, T*, int &), const char * nameSpace = nullptr)
	{
		return RegisterTypeCallback(typeId, byteLength, name, reinterpret_cast<zSAVE_FUNC_t>(save), reinterpret_cast<zLOAD_FUNC_t>(load), nameSpace, true);
	}

	template<typename T>
	int   RegisterRefType(uint typeId, uint byteLength, const char * name, void (*save)(zIZodiacWriter *, T const*, int &), void (*load)(zIZodiacReader *, T**, int &), const char * nameSpace = nullptr)
	{
		return RegisterTypeCallback(typeId, byteLength, name, reinterpret_cast<zSAVE_FUNC_t>(save), reinterpret_cast<zLOAD_FUNC_t>(load), nameSpace, false);
	}



/* why this not work but same idea does for nnholmon::json???
	template<typename T>
	inline int   RegisterRefType(const char * name, const char * nameSpace = nullptr)
	{ return RegisterTypeCallback(GetTypeId<T>(), sizeof(T), name, ZODIAC_GETSAVEFUNC(T), ZODIAC_GETLOADFUNC_R(T), nameSpace); }

	template<typename T>
	inline int   RegisterValueType(const char * name, const char * nameSpace = nullptr)
	{ return RegisterTypeCallback(GetTypeId<T>(), sizeof(T), name, ZODIAC_GETSAVEFUNC(T), ZODIAC_GETLOADFUNC_V(T), nameSpace); }
*/
//the typeid builtin doesn't always necessarily get the same reference depending on implementation,
//multiple can be created which isn't great for comparison.  Not fun!
//so make a unique int for every time this template exists
	template<typename T>
	static inline int GetTypeId()
	{
		static int _typeId{0};
		if(!_typeId) _typeId = ++typeIdCounter;
		return _typeId;
	}

private:
	virtual int   RegisterTypeCallback(uint typeId, uint byteLength, const char * name, zSAVE_FUNC_t, zLOAD_FUNC_t, const char * nameSpace, bool isValueType) = 0;

	static int typeIdCounter;
};

#define zRegisterRefType(zodiac, T, name, nameSpace)	zodiac->RegisterRefType(zIZodiac::GetTypeId<T>(), sizeof(T), name, ZODIAC_GETSAVEFUNC(T), ZODIAC_GETLOADFUNC_R(T), nameSpace)
#define zRegisterValueType(zodiac, T, name, nameSpace)	zodiac->RegisterValueType(zIZodiac::GetTypeId<T>(), sizeof(T), name, ZODIAC_GETSAVEFUNC(T), ZODIAC_GETLOADFUNC_V(T), nameSpace)



template<> inline int zIZodiac::GetTypeId<std::nullptr_t>() { return 0; }

extern std::unique_ptr<zIFileDescriptor> FromCFile(FILE *);
extern std::unique_ptr<zIFileDescriptor> FromCFile(FILE **);

class zIFileDescriptor : public asIBinaryStream
{
public:
struct ReadSubFile;
struct WriteSubFile;

	virtual ~zIFileDescriptor() = default;

	template<typename T> int Read(T * array, int length = 1)	 { return ((asIBinaryStream*)this)->Read((void*)array, sizeof(T)*length); }
	template<typename T> int Write(T const* array, int length = 1) { return ((asIBinaryStream*)this)->Write((void const*)array, sizeof(T) * length); }

	virtual void   seek(int, Flags) = 0;
	virtual uint tell() const = 0;

	inline void rewind() { seek(0, zFILE_BEGIN); }

	virtual uint SubFileOffset() const = 0;
	inline uint AbsoluteTell() const { return SubFileOffset() + tell(); }

protected:
	virtual void PushSubFile(uint, uint) = 0;
	virtual void PushSubFile(uint*) = 0;
	virtual void PopSubFile() = 0;
};

template<> inline int zIFileDescriptor::Read<void>(void * array, int length)  { return ((asIBinaryStream*)this)->Read(array, length); }
template<> inline int zIFileDescriptor::Write<void>(void const* array, int length) { return ((asIBinaryStream*)this)->Write(array, length); }

struct zIFileDescriptor::ReadSubFile
{
	ReadSubFile() = delete;
	ReadSubFile(zIFileDescriptor * parent, uint offset, uint length) : parent(parent) { parent->PushSubFile(offset, length); }
	ReadSubFile(ReadSubFile const &) = delete;
	ReadSubFile(ReadSubFile && it) : parent(it.parent) { it.parent = nullptr; }
	~ReadSubFile() { if(parent) parent->PopSubFile(); }

private:
	zIFileDescriptor * parent;
};

struct zIFileDescriptor::WriteSubFile
{
	WriteSubFile(zIFileDescriptor * parent, uint * length) : parent(parent) { parent->PushSubFile(length); }
	~WriteSubFile() { parent->PopSubFile(); }

private:
	zIFileDescriptor * parent;
};

class zIZodiacReader
{
public:
	virtual ~zIZodiacReader() = default;

	virtual zIFileDescriptor * GetFile() const = 0;
	virtual asIScriptEngine * GetEngine() const = 0;

	template<typename U, typename... Args>
	U * LoadRefObject(uint id, void (*)(zIZodiacReader *, U **, int &));

	template<typename U>
	U * LoadRefObject(uint id, void (*)(zIZodiacReader *, U **, int &));

	template<typename U, typename... Args>
	void LoadValuebject(uint id, U * ptr, void (*)(zIZodiacReader *, U *, int &));

	virtual void LoadScriptObject(void *, int address, int asTypeId, bool isWeak = false) = 0;
	inline  void LoadScriptObject(void * object, int address, asITypeInfo * typeInfo, bool isWeak = false) { LoadScriptObject(object, address, typeInfo? typeInfo->GetTypeId() : 0, isWeak); }

	virtual const char  * LoadString(int id) const = 0;
	virtual asITypeInfo * LoadTypeInfo(int id, bool RefCount) = 0;
	virtual int           LoadTypeId(int id) = 0;
//always refcounts
	virtual asIScriptFunction * LoadFunction(int id) = 0;
//always refcounts
	virtual asIScriptContext * LoadContext(int id) = 0;


private:
	template<typename U>
	U * CastObject(void * ptr, uint typeId);
	template<typename U, typename V>
	U * CastObject(void * ptr, uint typeId);
	template<typename U, typename V, typename... Args>
	U * CastObject(void * ptr, uint typeId);

	virtual void * LoadObject(int id, zLOAD_FUNC_t, int & actualType) = 0;
	virtual void LoadObject(int id, void *, zLOAD_FUNC_t, int & actualType) = 0;
};

class zIZodiacWriter
{
public:
	virtual ~zIZodiacWriter() = default;

	virtual zIFileDescriptor * GetFile() const = 0;
	virtual asIScriptEngine * GetEngine() const = 0;
	virtual bool SaveByteCode() const = 0;

	virtual int SaveString(const char *) = 0;
	inline  int SaveTypeInfo(asITypeInfo const* typeInfo) { return typeInfo? SaveTypeId(typeInfo->GetTypeId()) : 0; }
	virtual int SaveTypeId(int asTypeId) = 0;
	virtual int SaveFunction(asIScriptFunction const* id) = 0;
	virtual int SaveContext(asIScriptContext const* id) = 0;
	inline  int SaveScriptObject(asIScriptObject const* t, void const* ownr = nullptr)  { return SaveScriptObject( t, t? t->GetTypeId() : 0, ownr); }
	virtual int SaveScriptObject(void const* t, uint asTypeId, void const* ownr = nullptr) = 0;
	inline  int SaveScriptObject(void const* t, asITypeInfo const* asTypeId, void const* ownr = nullptr) {  return SaveScriptObject( t, asTypeId? asTypeId->GetTypeId() : 0, ownr);  }

// ownr indicates the object that owns (allocated memory for) the object being written.
// if the typeId isn't a handle type it is ignored.
	template<typename T>
	inline int SaveObject(T const* t, void (save)(zIZodiacWriter *, T const*, int &)) {	return SaveObject(t, zIZodiac::GetTypeId<T>(), reinterpret_cast<zSAVE_FUNC_t>(save)); }

protected:
	virtual int  SaveObject(void const* ptr, int zTypeId, zSAVE_FUNC_t) = 0;
};

template<typename U>
inline U * zIZodiacReader::CastObject(void * ptr, uint typeId)
{
	if(typeId != zIZodiac::GetTypeId<U>()) throw Code::zE_CastingException;
	return (U*)ptr;
}

template<typename U, typename V>
inline U * zIZodiacReader::CastObject(void * ptr, uint typeId)
{
	if(typeId == zIZodiac::GetTypeId<V>()) return (V*)ptr;
	return CastObject<U>(ptr, typeId);
}

template<typename U, typename V, typename... Args>
inline U * zIZodiacReader::CastObject(void * ptr, uint typeId)
{
	if(typeId == zIZodiac::GetTypeId<V>()) return (V*)ptr;
	return CastObject<U, Args...>(ptr, typeId);
}


template<typename T, typename... Args>
T *  zIZodiacReader::LoadRefObject(uint id, void (load_func)(zIZodiacReader *, T **, int &))
{
	int actualType{zIZodiac::GetTypeId<T>()};
	void * ptr = LoadObject(id, (zLOAD_FUNC_t)load_func, actualType);

	if(ptr != nullptr)
		return CastObject<T, Args...>(ptr, actualType);

	return nullptr;

}

template<typename T>
T *  zIZodiacReader::LoadRefObject(uint id, void (load_func)(zIZodiacReader *, T **, int &))
{
	int actualType{zIZodiac::GetTypeId<T>()};
	void * ptr = LoadObject(id, (zLOAD_FUNC_t)load_func, actualType);

	if(actualType != zIZodiac::GetTypeId<T>())
		throw Code::zE_CastingException;

	return (T*)ptr;
}

template<typename U, typename... Args>
void zIZodiacReader::LoadValuebject(uint id, U * ptr, void (save_func)(zIZodiacReader *, U *, int &))
{
	int actualType{zIZodiac::GetTypeId<U>()};
	LoadObject(id, ptr, (zLOAD_FUNC_t)save_func, actualType);

	if(actualType != zIZodiac::GetTypeId<U>())
		throw Code::zE_CastingException;
}


};

#endif

#endif // ZODIAC_H
