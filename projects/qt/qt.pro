TEMPLATE = app
CONFIG += c++14
CONFIG -= app_bundle qt

DEFINES += HAVE_ZODIAC AS_USE_STLNAMES=1

LIBS += -pthread

INCLUDEPATH += \
	../../../svn/angelscript-code/sdk/angelscript/include/ \
	../../../svn/angelscript-code/sdk/ \
	../../include/

SOURCES += \
	../../../svn/angelscript-code/sdk/add_on/contextmgr/contextmgr.cpp \
	../../../svn/angelscript-code/sdk/add_on/datetime/datetime.cpp \
	../../../svn/angelscript-code/sdk/add_on/debugger/debugger.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptany/scriptany.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptarray/scriptarray.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptbuilder/scriptbuilder.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptdictionary/scriptdictionary.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptfile/scriptfile.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptfile/scriptfilesystem.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptgrid/scriptgrid.cpp \
	../../../svn/angelscript-code/sdk/add_on/scripthandle/scripthandle.cpp \
	../../../svn/angelscript-code/sdk/add_on/scripthelper/scripthelper.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptmath/scriptmath.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptmath/scriptmathcomplex.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptstdstring/scriptstdstring.cpp \
	../../../svn/angelscript-code/sdk/add_on/scriptstdstring/scriptstdstring_utils.cpp \
	../../../svn/angelscript-code/sdk/add_on/serializer/serializer.cpp \
	../../../svn/angelscript-code/sdk/add_on/weakref/weakref.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_atomic.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_builder.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_bytecode.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm64.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_mips.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_ppc.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_ppc_64.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_sh4.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_x64_gcc.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_x64_mingw.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_x64_msvc.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_x86.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_xenon.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_compiler.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_configgroup.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_context.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_datatype.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_gc.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_generic.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_globalproperty.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_memory.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_module.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_objecttype.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_outputbuffer.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_parser.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_restore.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptcode.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptengine.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptfunction.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptnode.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptobject.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_string.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_string_util.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_thread.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_tokenizer.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_typeinfo.cpp \
	../../../svn/angelscript-code/sdk/angelscript/source/as_variablescope.cpp \
	../../example/example.cpp \
	../../example/print_func.cpp \
	../../src/z_cfile.cpp \
	../../src/z_memorymap.cpp \
	../../src/z_zodiac.cpp \
	../../src/z_zodiaccontext.cpp \
	../../src/z_zodiacreader.cpp \
	../../src/z_zodiacwriter.cpp

HEADERS += \
	../../../svn/angelscriINCpt-code/sdk/add_on/autowrapper/aswrappedcall.h \
	../../../svn/angelscript-code/sdk/add_on/contextmgr/contextmgr.h \
	../../../svn/angelscript-code/sdk/add_on/datetime/datetime.h \
	../../../svn/angelscript-code/sdk/add_on/debugger/debugger.h \
	../../../svn/angelscript-code/sdk/add_on/scriptany/scriptany.h \
	../../../svn/angelscript-code/sdk/add_on/scriptarray/scriptarray.h \
	../../../svn/angelscript-code/sdk/add_on/scriptbuilder/scriptbuilder.h \
	../../../svn/angelscript-code/sdk/add_on/scriptdictionary/scriptdictionary.h \
	../../../svn/angelscript-code/sdk/add_on/scriptfile/scriptfile.h \
	../../../svn/angelscript-code/sdk/add_on/scriptfile/scriptfilesystem.h \
	../../../svn/angelscript-code/sdk/add_on/scriptgrid/scriptgrid.h \
	../../../svn/angelscript-code/sdk/add_on/scripthandle/scripthandle.h \
	../../../svn/angelscript-code/sdk/add_on/scripthelper/scripthelper.h \
	../../../svn/angelscript-code/sdk/add_on/scriptmath/scriptmath.h \
	../../../svn/angelscript-code/sdk/add_on/scriptmath/scriptmathcomplex.h \
	../../../svn/angelscript-code/sdk/add_on/scriptstdstring/scriptstdstring.h \
	../../../svn/angelscript-code/sdk/add_on/serializer/serializer.h \
	../../../svn/angelscript-code/sdk/add_on/weakref/weakref.h \
	../../../svn/angelscript-code/sdk/angelscript/include/angelscript.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_array.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_atomic.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_builder.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_bytecode.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm_gcc.S \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm_vita.S \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm_xcode.S \
	../../../svn/angelscript-code/sdk/angelscript/source/as_compiler.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_config.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_configgroup.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_context.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_criticalsection.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_datatype.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_debug.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_gc.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_generic.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_map.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_memory.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_module.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_namespace.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_objecttype.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_outputbuffer.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_parser.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_property.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_restore.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptcode.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptengine.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptfunction.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptnode.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_scriptobject.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_string.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_string_util.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_symboltable.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_texts.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_thread.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_tokendef.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_tokenizer.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_typeinfo.h \
	../../../svn/angelscript-code/sdk/angelscript/source/as_variablescope.h \
	../../example/print_func.h \
	../../include/zodiac.h \
	../../include/zodiac_addon.hpp \
	../../src/z_cfile.h \
	../../src/z_memorymap.h \
	../../src/z_zodiac.h \
	../../src/z_zodiaccontext.h \
	../../src/z_zodiacreader.h \
	../../src/z_zodiacstate.h \
	../../src/z_zodiacwriter.h

DISTFILES += \
	../../../svn/angelscript-code/sdk/add_on/autowrapper/generator/generator.sln \
	../../../svn/angelscript-code/sdk/add_on/autowrapper/generator/generator.vcproj \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm64_gcc.S \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm64_msvc.asm \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_arm_msvc.asm \
	../../../svn/angelscript-code/sdk/angelscript/source/as_callfunc_x64_msvc_asm.asm
