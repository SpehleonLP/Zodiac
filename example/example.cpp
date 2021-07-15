// This sample shows how to use co-routines with AngelScript. Co-routines
// are threads that work together. When one yields the next one takes over.
// This way they are always synchronized, which makes them much easier to
// use than threads that run in parallel.

#if 0

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

#include "zodiac.h"
#include <memory>

#define UINT unsigned int
typedef unsigned int DWORD;

// Function prototypes
void ConfigureEngine(asIScriptEngine *engine);
int  CompileScript(asIScriptEngine *engine);

void PrintString(std::string &str)
{
	std::cout << str << std::endl;
}

void Suspend()
{
	auto ctx = asGetActiveContext();
	ctx->Suspend();
}


#define TEXT(x) #x

void MessageCallback(const asSMessageInfo *msg, void *)
{
	const char *type = "ERR ";
	if( msg->type == asMSGTYPE_WARNING )
		type = "WARN";
	else if( msg->type == asMSGTYPE_INFORMATION )
		type = "INFO";

	printf("%s (%d, %d) : %s : %s\n", msg->section, msg->row, msg->col, type, msg->message);
}

const char Script[] = TEXT(

	void main()
	{
		string text = "some text";
		Supsend();
		Print(text);
	}
);

asIScriptContext *ctx;

struct SaveData
{
	uint32_t Context;
};

void WriteSaveDataFunc(Zodiac::zIZodiacWriter * writer, void* ptr)
{
	((SaveData*)ptr)->Context = writer->SaveObject<asIScriptContext>(ctx);

	auto file = writer->GetFile();
	file->Write((SaveData*)ptr);
}


void ReadSaveDataFunc(Zodiac::zIZodiacReader * reader, void* ptr)
{
	auto file = reader->GetFile();
	file->Read((SaveData*)ptr);

	ctx = reader->LoadObject<asIScriptContext>(((SaveData*)ptr)->Context );
}

int main(int argc, char **argv)
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

	r = engine->RegisterGlobalFunction("void Print(string &in)", asFUNCTION(PrintString), asCALL_CDECL); assert( r >= 0 );
	r = engine->RegisterGlobalFunction("void Suspend()", asFUNCTION(Suspend), asCALL_CDECL); assert( r >= 0 );

	FILE * fp = fopen("save_file.z", "r");

	if(fp == nullptr)
	{
		asIScriptModule *mod = engine->GetModule("script", asGM_ALWAYS_CREATE);
		r = mod->AddScriptSection("script", Script, strlen(Script));
		r = mod->Build();

		asIScriptFunction *func = mod->GetFunctionByDecl("void main()");

		asIScriptContext *ctx = mod->GetEngine()->RequestContext();
		ctx->Prepare(func);
	}
	else
	{
		std::unique_ptr<Zodiac::zIZodiac> zodiac(Zodiac::zCreateZodiac(engine));
		SaveData data;

		zodiac->SetUserData(&data);
		zodiac->SetReadSaveDataCallback(ReadSaveDataFunc);

		std::unique_ptr<Zodiac::zIFileDescriptor> file(Zodiac::FromCFile(&fp));
		zodiac->LoadFromFile(file.get());
	}

	ctx->Execute();

	if(ctx->GetState() == asEXECUTION_SUSPENDED)
	{
		std::unique_ptr<Zodiac::zIZodiac> zodiac(Zodiac::zCreateZodiac(engine));
		SaveData data;

		zodiac->SetUserData(&data);
		zodiac->SetWriteSaveDataCallback(WriteSaveDataFunc);

		zodiac->SetProperty(Zodiac::zZP_SAVE_BYTECODE, true);

		FILE * fp = fopen("save_file.z", "w");

		std::unique_ptr<Zodiac::zIFileDescriptor> file(Zodiac::FromCFile(&fp));
		zodiac->SaveToFile(file.get());
	}
	else
	{
		remove("save_file.z");
	}

	// Shut down the engine
	engine->ShutDownAndRelease();

	return 0;
}

#endif
