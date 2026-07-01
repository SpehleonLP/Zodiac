cmake_minimum_required(VERSION 3.20)
project(zodiac CXX)

# ---------------------------------------------------------------------------
# Standalone build for the Zodiac AngelScript serialization library.
# The engine normally compiles Zodiac's .cpp in-tree; this file lets it build
# and be gtest-ed standalone. Build dir MUST live on an exec-capable ext4 path
# (e.g. ~/Developer/Build/Zodiac-Debug) — /mnt/Passport is a no-exec FUSE mount.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ZODIAC_BUILD_TESTS "Build the Zodiac gtest suite" ON)
option(ZODIAC_BUILD_EXAMPLE "Build the legacy example/ target" OFF)
option(ZODIAC_NO_SANITIZERS "Disable ASan/UBSan on the test target" OFF)

set(ANGELSCRIPT_SDK "/mnt/Passport/Libraries/svn/angelscript-code/sdk"
    CACHE PATH "Root of the patched AngelScript SDK")

# --- AngelScript VM (patched SDK, no prebuilt lib) -------------------------
# NOTE: NOT instrumented with sanitizers — the AS bytecode interpreter is known
# to trip UBSan (type-punning / unaligned access). ASan works cross-module even
# when only the test target + zodiac are instrumented.
file(GLOB ANGELSCRIPT_SOURCES CONFIGURE_DEPENDS
     "${ANGELSCRIPT_SDK}/angelscript/source/*.cpp")
add_library(angelscript STATIC ${ANGELSCRIPT_SOURCES})
target_include_directories(angelscript PUBLIC
     "${ANGELSCRIPT_SDK}/angelscript/include")

# --- AngelScript add-ons ----------------------------------------------------
# NOTE on scriptarray: the patched SDK's scriptarray.cpp is engine-coupled — it
# #includes "AngelScript/Native/as_arraysupport.h" and the custom CreateCScriptArray
# factory that live only in the engine tree, so it cannot compile standalone. Per
# the spec ("the standalone harness should use the stock scriptarray add-on"), we
# vendor the stock 2.38.0 scriptarray (.h + .cpp, extracted from the SDK's own
# angelscript_2.38.0 zip) under third_party/as_stock and shadow the patched header
# by putting that dir first on the include path.
set(AS_STOCK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/as_stock")
set(AS_ADDON_SOURCES
    "${ANGELSCRIPT_SDK}/add_on/scriptstdstring/scriptstdstring.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scriptstdstring/scriptstdstring_utils.cpp"
    "${AS_STOCK_DIR}/add_on/scriptarray/scriptarray.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scriptdictionary/scriptdictionary.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scriptany/scriptany.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scriptgrid/scriptgrid.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scripthandle/scripthandle.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scriptfile/scriptfile.cpp"
    "${ANGELSCRIPT_SDK}/add_on/scriptmath/scriptmath.cpp"
    "${ANGELSCRIPT_SDK}/add_on/contextmgr/contextmgr.cpp"
    "${ANGELSCRIPT_SDK}/add_on/datetime/datetime.cpp"
    "${ANGELSCRIPT_SDK}/add_on/weakref/weakref.cpp")
add_library(as_addons STATIC ${AS_ADDON_SOURCES})
# Vendored-stock dir first so `add_on/scriptarray/scriptarray.h` resolves to the
# stock header; SDK dir provides every other `add_on/<name>/<name>.h`.
target_include_directories(as_addons PUBLIC "${AS_STOCK_DIR}" "${ANGELSCRIPT_SDK}")
target_link_libraries(as_addons PUBLIC angelscript)

# --- Zodiac -----------------------------------------------------------------
file(GLOB ZODIAC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
# z_zodiaccontext.cpp (suspended-context save/restore) is now adapted to trunk's
# AngelScript context API (2-arg PushFunction + GetAddressOfVar in place of the
# owner's original, never-upstreamed SetVarContents) and compiles in-tree again.
add_library(zodiac STATIC ${ZODIAC_SOURCES})
target_include_directories(zodiac PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(zodiac PUBLIC HAVE_ZODIAC=1)
target_link_libraries(zodiac PUBLIC angelscript as_addons)

enable_testing()
if(ZODIAC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# example/ intentionally left unwired (out of scope for the test harness task).
