#ifndef Z_ZODIACCONTEXT_H
#define Z_ZODIACCONTEXT_H
#ifdef HAVE_ZODIAC
#include "zodiac.h"

namespace Zodiac
{
void ZodiacSave(zIZodiacWriter*, asIScriptContext*, int&);
void ZodiacLoad(zIZodiacReader*, asIScriptContext*, int&);
}


#endif
#endif // Z_ZODIACCONTEXT_H
