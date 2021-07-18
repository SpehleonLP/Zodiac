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
#include "add_on/scriptstdstring/scriptstdstring.h"
#include "add_on/scriptarray/scriptarray.h"
#include "add_on/scriptdictionary/scriptdictionary.h"
#include "add_on/datetime/datetime.h"
#include "print_func.h"
#include "zodiac.h"
#include <memory>
#include <vector>

//must be included after add_on-s
#include "zodiac_addon.hpp"

#define UINT unsigned int
typedef unsigned int DWORD;

// Function prototypes
void ConfigureEngine(asIScriptEngine *engine);
int  CompileScript(asIScriptEngine *engine);

void Suspend()
{
	auto ctx = asGetActiveContext();
	ctx->Suspend();
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

asIScriptContext *ctx;

struct SaveData
{
	uint32_t Context;
};

void WriteSaveDataFunc(Zodiac::zIZodiacWriter * writer, void* ptr)
{
	((SaveData*)ptr)->Context = writer->SaveContext(ctx);

	auto file = writer->GetFile();
	file->Write((SaveData*)ptr);
}


void ReadSaveDataFunc(Zodiac::zIZodiacReader * reader, void* ptr)
{
	auto file = reader->GetFile();
	file->Read((SaveData*)ptr);

	ctx = reader->LoadContext(((SaveData*)ptr)->Context);
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
	auto engine = asCreateScriptEngine();
	if( engine == 0 )
	{
		std::cout << "Failed to create script engine." << std::endl;
		return -1;
	}

	// The script compiler will send any compiler messages to the callback function
	engine->SetMessageCallback(asFUNCTION(MessageCallback), 0, asCALL_CDECL);

	RegisterStdString(engine);
	RegisterScriptArray(engine, false);
	RegisterScriptDictionary(engine);

	Print::asRegister(engine);
	r = engine->RegisterGlobalFunction("void Suspend()", asFUNCTION(Suspend), asCALL_CDECL); assert( r >= 0 );

//-------------------
// create zodiac
//--------------------

	std::unique_ptr<Zodiac::zIZodiac> zodiac(Zodiac::zCreateZodiac(engine));
	SaveData data;

	zodiac->SetUserData(&data);
	zodiac->SetReadSaveDataCallback(ReadSaveDataFunc);
	zodiac->SetWriteSaveDataCallback(WriteSaveDataFunc);

	zodiac->SetProperty(Zodiac::zZP_SAVE_BYTECODE, true);
//register add_on directory stuffs
	Zodiac::ZodiacRegisterAddons(zodiac.get());


// try to restore
	FILE * fp = nullptr; //fopen("save_file.z", "rb");

	if(fp != nullptr)
	{
	//use pointer to fp to signify taking ownership and closing when the class is deleted
		std::unique_ptr<Zodiac::zIFileDescriptor> file(Zodiac::FromCFile(&fp));
		zodiac->LoadFromFile(file.get());
	}
	else
	{
// restore failed create context
		auto script = ReadFile("main.as");

		asIScriptModule *mod = engine->GetModule("script", asGM_ALWAYS_CREATE);
		r = mod->AddScriptSection("script", script.data(), script.size());
		r = mod->Build();

		asIScriptFunction *func = mod->GetFunctionByDecl("void main()");

		ctx = mod->GetEngine()->RequestContext();
		ctx->Prepare(func);
	}

	if(ctx) ctx->Execute();

// finished delete save
	if(!ctx || ctx->GetState() != asEXECUTION_SUSPENDED)
	{
		remove("save_file.z");
	}
// suspended create save
	else
	{
		FILE * fp = fopen("save_file.z", "wb");

//use pointer to fp to signify taking ownership and closing when the class is deleted
		std::unique_ptr<Zodiac::zIFileDescriptor> file(Zodiac::FromCFile(&fp));
		zodiac->SaveToFile(file.get());
	}

	engine->ReturnContext(ctx);
	// Shut down the engine
	engine->ShutDownAndRelease();

	return 0;
}

int main(int , char **)
{
	try
	{
		Run();
	}
	catch(std::exception & e)
	{
		std::cerr << e.what() << std::endl;
	}
}
