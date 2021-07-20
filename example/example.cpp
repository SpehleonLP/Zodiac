// This sample shows how to use the zodiac

#include <iostream>  // cout
#include <assert.h>  // assert()
#include <string.h>  // strstr()
#ifdef __GNUC__
	#include <sys/time.h>
	#include <stdio.h>
	#include <termios.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <string.h>
#else
	#include <conio.h>   // kbhit(), getch()
	#include <windows.h> // timeGetTime()
	#include <crtdbg.h>  // debugging routines
#endif
#include <list>
#include <angelscript.h>
#include "add_on/scriptmath/scriptmath.h"
#include "add_on/scriptstdstring/scriptstdstring.h"
#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptdictionary/scriptdictionary.h"
#include "add_on/contextmgr/contextmgr.h"
#include "add_on/datetime/datetime.h"
#include "add_on/weakref/weakref.h"
#include "print_func.h"
#include "zodiac.h"
#include <memory>
#include <vector>
#include <ctime>

#include <chrono>
#include <thread>

//must be included after add_on-s
#include "zodiac_addon.hpp"

#define UINT unsigned int
typedef unsigned int DWORD;

// Function prototypes
void ConfigureEngine(asIScriptEngine *engine);
int  CompileScript(asIScriptEngine *engine);

DWORD timeGetTime()
{
	timeval time;
	gettimeofday(&time, NULL);
	return time.tv_sec*1000 + time.tv_usec/1000;
}

bool ShouldSave = false;

void SaveAndQuit()
{
	auto ctx = asGetActiveContext();
	ctx->Suspend();

	ShouldSave = true;
}

void MessageCallback(const asSMessageInfo *msg, void *)
{
	const char *type = "ERR ";
	if( msg->type == asMSGTYPE_WARNING )
		type = "WARN";
	else if( msg->type == asMSGTYPE_INFORMATION )
		type = "INFO";

	printf("%s (%d, %d) : %s : %s\n", msg->section, msg->row, msg->col, type, msg->message);
}

struct SaveData
{
	CContextMgr * mgr{};
	uint32_t mgrId{};
};

void WriteSaveDataFunc(Zodiac::zIZodiacWriter * writer, void* ptr)
{
	auto save = (SaveData*)ptr;
	save->mgrId = writer->SaveObject<CContextMgr>(save->mgr, Zodiac::ZodiacSaveContextManager);
	writer->GetFile()->Write(&save->mgrId);
}


void ReadSaveDataFunc(Zodiac::zIZodiacReader * reader, void* ptr)
{
	auto save = (SaveData*)ptr;
	auto file = reader->GetFile();
	file->Read(&save->mgrId);

	reader->LoadValuebject(save->mgrId, save->mgr,  Zodiac::ZodiacLoadContextManager);
}

std::string ReadFile(const char * path)
{
	FILE * fp = fopen(path, "r");

	fseek(fp, 0, SEEK_END);
	std::string file;
	file.resize(ftell(fp));
	fseek(fp, 0, SEEK_SET);

	fread(&file[0], file.size(), 1, fp);
	fclose(fp);

	return file;
}

void ScriptSleep(asUINT milliSeconds)
{
	// Get a pointer to the context that is currently being executed
	asIScriptContext *ctx = asGetActiveContext();
	if( ctx )
	{
		// Get the context manager from the user data
		CContextMgr *ctxMgr = reinterpret_cast<CContextMgr*>(ctx->GetUserData(1002));
		if( ctxMgr )
		{
			// Let the context manager know that it should run the next co-routine
			ctxMgr->NextCoRoutine();

			// Suspend its execution. The VM will continue until the current
			// statement is finished and then return from the Execute() method
			ctx->Suspend();

			// Tell the context manager when the context is to continue execution
			ctxMgr->SetSleeping(ctx, milliSeconds);
		}
	}
}

auto CreateEngine()
{
	struct Internal
	{
		static void DestroyScriptEngine(asIScriptEngine * engine)
		{
			engine->ShutDownAndRelease();
		}
	};

	std::unique_ptr<asIScriptEngine, void (*)(asIScriptEngine*)> ptr(asCreateScriptEngine(), &Internal::DestroyScriptEngine);
	if(!ptr) return ptr;

	auto engine = ptr.get();

	// The script compiler will send any compiler messages to the callback function
	engine->SetMessageCallback(asFUNCTION(MessageCallback), 0, asCALL_CDECL);

	RegisterStdString(engine);
	RegisterScriptArray(engine, true);
	RegisterScriptDictionary(engine);
	RegisterScriptDateTime(engine);
	RegisterScriptWeakRef(engine);

	int r = engine->RegisterGlobalFunction("int rand()", asFUNCTION(std::rand), asCALL_CDECL); assert(r >= 0);
	r = engine->RegisterGlobalFunction("void SaveAndQuit()", asFUNCTION(SaveAndQuit), asCALL_CDECL); assert(r >= 0);

	r = engine->RegisterGlobalFunction("void sleep(uint)", asFUNCTION(ScriptSleep), asCALL_CDECL); assert( r >= 0 );

	Print::asRegister(engine);

	return ptr;
}

std::unique_ptr<Zodiac::zIZodiac> CreateZodiac(asIScriptEngine * engine, SaveData * save_data)
{
	auto zodiac = Zodiac::zCreateZodiac(engine);

	zodiac->SetUserData(save_data);
	zodiac->SetReadSaveDataCallback(ReadSaveDataFunc);
	zodiac->SetWriteSaveDataCallback(WriteSaveDataFunc);

	zodiac->SetProperty(Zodiac::zZP_SAVE_BYTECODE, true);
//register add_on directory stuffs
	Zodiac::ZodiacRegisterAddons(zodiac.get());

	return zodiac;
}


int LogResult(asIScriptContext * ctx, bool can_suspend = true);

bool LoadFromFile(Zodiac::zIZodiac * zodiac, const char * filename)
{
	FILE * fp = fopen(filename, "rb");

	if(fp == nullptr)
		return false;

	auto file = Zodiac::FromCFile(&fp);
	return zodiac->LoadFromFile(file.get());
}

int Run()
{
	// Perform memory leak validation in debug mode
	#if defined(_MSC_VER)
	_CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF|_CRTDBG_ALLOC_MEM_DF);
	_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
	#endif

	int r;

	// Create the script engine
	auto engine = CreateEngine();
	if( engine == nullptr )
	{
		std::cout << "Failed to create script engine." << std::endl;
		return -1;
	}
	CContextMgr ctxManager;

	ctxManager.SetGetTimeCallback(timeGetTime);
//	ctxManager.RegisterThreadSupport(engine.get());
	ctxManager.RegisterCoRoutineSupport(engine.get());

	SaveData saveData;
	saveData.mgr = &ctxManager;

	auto zodiac = CreateZodiac(engine.get(), &saveData);


// try to restore
	if(!LoadFromFile(zodiac.get(), "save_file.zdc"))
	{
// restore failed create context
		auto script = ReadFile("main.as");

		asIScriptModule *mod = engine->GetModule("script", asGM_ALWAYS_CREATE);
		r = mod->AddScriptSection("script", script.data(), script.size());
		r = mod->Build();

		asIScriptFunction *func = mod->GetFunctionByDecl("void SetUp()");

		auto ctx = mod->GetEngine()->RequestContext();
		ctx->Prepare(func);

		ctx->Execute();
		LogResult(ctx, false);

		if(ctx->GetState() != asEXECUTION_FINISHED)
			return -1;

		engine->ReturnContext(ctx);

		func = mod->GetFunctionByDecl("void RunSchedule()");
		ctxManager.AddContext(engine.get(), func);
	}

	auto start_time = std::chrono::system_clock::now();
	while(!ShouldSave && ctxManager.ExecuteScripts() > 0)
	{
		fflush(stdout);
		std::this_thread::sleep_for(std::chrono::seconds(1));

		auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start_time).count();

		if(time_diff > 5)
		{
			ShouldSave = true;
		}
	};

// finished delete save
	if(!ShouldSave)
	{
		remove("save_file.zdc");
	}
// suspended create save
	else
	{
		FILE * fp = fopen("save_file.zdc", "wb");

//use pointer to fp to signify taking ownership and closing when the class is deleted
		auto file = Zodiac::FromCFile(&fp);
		zodiac->SaveToFile(file.get());
	}

	return 0;
}

int main(int , char **)
{
#if 1
	Run();
#else
	try
	{
		Run();
	}
	catch(std::exception & e)
	{
		std::cerr << e.what() << std::endl;
	}
#endif
}

void LogMessage(asIScriptContext * ctx, int level, std::string const& msg);
void asLogException(asIScriptContext * ctx);
void asLog(int type,int row, int col, const char * section, const char * module, const char * function, const char * message);

int LogResult(asIScriptContext * ctx, bool can_suspend)
{
	auto state = ctx->GetState();

	switch(state)
	{
	case asEXECUTION_FINISHED:
		return 0;
	case asEXECUTION_SUSPENDED:
	{
		if(can_suspend == false)
		{
			LogMessage(ctx, -1, "Script Function suspended");
			return false;
		}

		return 1;
	}
	case asEXECUTION_ABORTED:
		LogMessage(ctx, 0, "Script function aborted");
		return 0;
	case asEXECUTION_EXCEPTION:
	{
		asLogException(ctx);
		return -1;
	}
	case asEXECUTION_PREPARED:
	case asEXECUTION_UNINITIALIZED:
	case asEXECUTION_ACTIVE:
		return 1;
	case asEXECUTION_ERROR:
		LogMessage(ctx, -2, "Execution Error");
		return -1;
	default:
		break;
	}

	return 0;

};

void LogMessage(asIScriptContext * ctx, int level, std::string const& msg)
{
	level = std::max(-1, std::min(level, 9));

	int row{};
	int col{};
	const char * section{};
	const char * module{};
	const char * function{};

	ctx->GetLineNumber(0, &col, &section);
	auto func = ctx->GetFunction(0);

	if(func)
	{
		function = func->GetDeclaration();
		module   = func->GetModule()->GetName();
	}

	asLog(	level,
			row,
			col,
			section,
			module,
			function,
			msg.c_str());

}

void asLogException(asIScriptContext * ctx)
{
	int row{};
	int col{};
	const char * section{};
	const char * module{};
	const char * function{};

	ctx->GetExceptionLineNumber(&col, &section);
	auto func = ctx->GetExceptionFunction();

	if(func)
	{
		function = func->GetDeclaration();
		module   = func->GetModule()->GetName();
	}

	asLog(	-1,
			row,
			col,
			section,
			module,
			function,
			ctx->GetExceptionString());
}


void asLog(
		int,
		int row,
		int col,
		const char * section,
		const char * module,
		const char * function,
		const char * message)
{
	(void)col;

	std::string path;

	if(section != nullptr)
		path = section;

	if(module != nullptr)
	{
		if(path.size())
			path += "/";

		path += module;
	}

	if(function != nullptr)
	{
		if(path.size())
			path += "/";

		path += function;
	}

	fprintf(stderr, "%s (%d):%s\n", path.c_str(), row, message);
}
