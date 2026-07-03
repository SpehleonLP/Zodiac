// Group S seek/tell-fault hardening (Task 5).
//
// zCFile::seek/tell now check every fseek/ftell result and THROW zE_IOError on
// a hard failure (e.g. a non-seekable pipe/FIFO/socket). This test drives the
// fault-injecting zCFaultyFile double, whose seek()/tell() throw the same Code,
// and asserts that a seek/tell failure becomes a RETURNED Zodiac::Code at the
// SaveToFile boundary, NOT an escaping throw and NOT a spurious zE_Success.
//
// RED note: the double's seek/tell fault is independent of the production fix,
// so this test primarily validates that the FACADE maps a seek/tell exception
// to zE_IOError and exercises the harness knob. A pure production-path S1 test
// would require a real non-seekable pipe; the spec explicitly allows this
// double-based substitution. The production-side checks (Part A) are verified
// by review plus the whole suite staying green (no seek/tell regression).
#include "test_engine.h"
#include "addons.h"
#include "faulty_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}
}

TEST(IOSeekFault, SeekTellFailureAbortsSaveWithCode)
{
	TestEngine engine;
	asIScriptModule * mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("m", "int g = 1;\n"), 0);
	ASSERT_GE(mod->Build(), 0);

	auto zodiac = MakeZodiac(engine.get());
	zCFaultyFile file;
	file.failSeek();                     // every seek/tell now throws zE_IOError

	Code rc = zE_Success;
	EXPECT_NO_THROW({ rc = zodiac->SaveToFile(&file); })
		<< "a seek/tell failure must become a returned Code, not a throw/crash";
	EXPECT_EQ(rc, zE_IOError)
		<< "a non-seekable/broken descriptor must abort the save with zE_IOError";
}
