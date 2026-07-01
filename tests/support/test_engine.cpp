#include "test_engine.h"
#include "addons.h"

#include <angelscript.h>
#include <gtest/gtest.h>

namespace zodiac_test
{

static void MessageCallback(const asSMessageInfo * msg, void *)
{
	if(msg->type == asMSGTYPE_ERROR)
	{
		ADD_FAILURE() << "AngelScript error: " << msg->section << " ("
					  << msg->row << ", " << msg->col << ") : " << msg->message;
	}
	else if(msg->type == asMSGTYPE_WARNING)
	{
		// warnings are informational; surface them but don't fail
		testing::Message() << "AngelScript warning: " << msg->message;
	}
}

TestEngine::TestEngine()
{
	m_engine = asCreateScriptEngine();
	if(m_engine == nullptr)
	{
		ADD_FAILURE() << "asCreateScriptEngine() returned null";
		return;
	}

	m_engine->SetMessageCallback(asFUNCTION(MessageCallback), 0, asCALL_CDECL);
	RegisterEngineAddons(m_engine);
}

TestEngine::~TestEngine()
{
	if(m_engine)
		m_engine->ShutDownAndRelease();
}

}
