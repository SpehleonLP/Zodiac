#ifndef PRINT_FUNC_H
#define PRINT_FUNC_H

#if AS_USE_NAMESPACE
namespace AngelScript { class asIScriptEngine; }
#else
class asIScriptEngine;
#endif


namespace Print
{
#if AS_USE_NAMESPACE
typedef AngelScript::asIScriptEngine asIScriptEngine;
#else
typedef ::asIScriptEngine asIScriptEngine;
#endif

void asRegister( asIScriptEngine * engine);
};



#endif // PRINT_FUNC_H
