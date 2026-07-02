// Defect regression test for the writer's handle-member serialization width
// (src/z_zodiacwriter.cpp:179, also :207/:269).
//
// `m_file->Write(&id_no, sizeof(uint32_t))` binds to the templated
// Write<T>(const T*, int length) in zodiac.h, whose `length` is a COUNT, not a
// byte size — so it writes sizeof(uint32_t) * sizeof(uint32_t) = 16 bytes for a
// 4-byte object-handle id. The member's recorded zCProperty.byteLength is 16
// instead of 4, and 12 bytes of writer-stack garbage are appended to the object
// blob (an ASan stack-buffer-overflow READ under -DZODIAC_INSTRUMENT_LIB=ON:
// "READ of size 16" at z_zodiacwriter.cpp). This test reads the recorded width
// directly, so it is red on a stock build too.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"
#include "z_zodiacstate.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
// Node has a script-object handle member `next` (serialized as a 4-byte object id)
// and a primitive member `value` (a genuine 4-byte int, used as a sanity anchor).
const char * kScript =
	"class Node { int value; Node@ next; }\n"
	"Node@ head;\n"
	"void setup() {\n"
	"    Node@ a = Node(); Node@ b = Node();\n"
	"    a.value = 111; b.value = 222;\n"
	"    @a.next = b; @head = a;\n"
	"}\n";

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * engine)
{
	auto zodiac = zCreateZodiac(engine);
	RegisterZodiacAddons(zodiac.get());
	zodiac->SetProperty(zZP_SAVE_BYTECODE, true);
	return zodiac;
}

std::vector<char> BuildValidImage()
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_NE(mod, nullptr);
	EXPECT_GE(mod->AddScriptSection("m", kScript), 0);
	EXPECT_GE(mod->Build(), 0);

	asIScriptFunction * setup = mod->GetFunctionByDecl("void setup()");
	EXPECT_NE(setup, nullptr);
	asIScriptContext * ctx = engine.get()->CreateContext();
	EXPECT_GE(ctx->Prepare(setup), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

	auto zodiac = MakeZodiac(engine.get());
	zCMemoryFile file;
	EXPECT_EQ(zodiac->SaveToFile(&file), zE_Success);
	return file.bytes();
}

int PropertyByteLength(std::vector<char> & image, const char * propName)
{
	auto * h = reinterpret_cast<zCHeader *>(image.data() + image.size() - sizeof(zCHeader));
	auto * props   = reinterpret_cast<zCProperty *>(image.data() + h->propertiesOffset);
	const char * strings = image.data() + h->stringTableOffset;
	for(uint32_t i = 0; i < h->propertiesLength; ++i)
	{
		if(std::strcmp(strings + props[i].name, propName) == 0)
			return props[i].byteLength;
	}
	return -1;
}
}

TEST(WriterHandleWidth, HandleMemberSerializesFourBytes)
{
	std::vector<char> image = BuildValidImage();
	ASSERT_FALSE(image.empty());

	// Sanity anchor: the primitive int member is 4 bytes.
	EXPECT_EQ(PropertyByteLength(image, "value"), 4);

	// The defect: the handle member is written as 16 bytes (4 * sizeof(uint32_t)).
	EXPECT_EQ(PropertyByteLength(image, "next"), 4)
		<< "handle member 'next' should serialize as a single 4-byte object id";
}
