find_package(GTest REQUIRED)
include(GoogleTest)

file(GLOB ZODIAC_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")
add_executable(zodiac_tests ${ZODIAC_TEST_SOURCES})
target_include_directories(zodiac_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(zodiac_tests PRIVATE zodiac GTest::gtest GTest::gtest_main)

if(NOT ZODIAC_NO_SANITIZERS)
    # ASan + UBSan on the test target only. The angelscript / as_addons libs are
    # deliberately NOT instrumented (AS VM trips UBSan); ASan still works
    # cross-module against them.
    target_compile_options(zodiac_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(zodiac_tests PRIVATE
        -fsanitize=address,undefined)
endif()

gtest_discover_tests(zodiac_tests)
