#ifndef ZODIAC_TESTS_SUPPORT_TEST_ENGINE_H
#define ZODIAC_TESTS_SUPPORT_TEST_ENGINE_H

class asIScriptEngine;

namespace zodiac_test
{

// RAII wrapper around a stock asIScriptEngine with the add-ons registered and
// a message callback that FAILS the current gtest on any asMSGTYPE_ERROR.
class TestEngine
{
public:
	TestEngine();
	~TestEngine();

	TestEngine(TestEngine const&) = delete;
	TestEngine & operator=(TestEngine const&) = delete;

	asIScriptEngine * get() const { return m_engine; }
	asIScriptEngine * operator->() const { return m_engine; }
	operator asIScriptEngine *() const { return m_engine; }

private:
	asIScriptEngine * m_engine{};
};

}

#endif // ZODIAC_TESTS_SUPPORT_TEST_ENGINE_H
