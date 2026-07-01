// PHASE-0 LINK SCAFFOLD --- NOT a real implementation.
//
// src/z_zodiaccontext.cpp (suspended-context save/restore) is intentionally
// EXCLUDED from the standalone build (it needs an AS context API that trunk
// changed; adapted in a later slice). BUT the exclusion is NOT link-clean:
// z_zodiacwriter.cpp:873 (SaveContext) and z_zodiacreader.cpp:1092 (LoadContext)
// reference these two symbols UNCONDITIONALLY, so libzodiac cannot link into any
// executable without them.
//
// Per the Phase-0 mandate we do NOT re-add the excluded TU. Instead these
// throwing stubs satisfy the linker so the tests that never serialize a
// suspended context (canary, cfile, empty/memoryfile round-trips) can build and
// run. If a test ever actually drives context serialization it throws loudly
// rather than corrupting silently.
//
// This file must be DELETED once z_zodiaccontext.cpp is re-adapted and
// re-included (the later slice).
#include "z_zodiaccontext.h"
#ifdef HAVE_ZODIAC
#include "zodiac.h"

namespace Zodiac
{

void ZodiacSave(zIZodiacWriter *, asIScriptContext const*, int&)
{
	throw zE_CantSaveContextWithoutBytecode;
}

void ZodiacLoad(zIZodiacReader *, asIScriptContext **, int&)
{
	throw zE_ContextNotSuspended;
}

}

#endif
