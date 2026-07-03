// Coverage for facade surface that no test exercised: the post-save / post-restore
// callbacks, the z-typeId <-> AS-typeId accessors, and repeated save. These are
// public API methods (include/zodiac.h) with zero prior test hits.
#include "test_engine.h"
#include "addons.h"
#include "memory_file.h"
#include "zodiac.h"

#include <angelscript.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace Zodiac;
using namespace zodiac_test;

namespace
{
struct Hooks { int preSave = 0, postSave = 0, preRestore = 0, postRestore = 0; };

std::unique_ptr<zIZodiac> MakeZodiac(asIScriptEngine * e)
{
	auto z = zCreateZodiac(e);
	RegisterZodiacAddons(z.get());
	z->SetProperty(zZP_SAVE_BYTECODE, true);
	return z;
}

asIScriptModule * BuildTrivial(asIScriptEngine * e)
{
	asIScriptModule * mod = e->GetModule("m", asGM_ALWAYS_CREATE);
	EXPECT_GE(mod->AddScriptSection("m", "int g = 7;\n"), 0);
	EXPECT_GE(mod->Build(), 0);
	return mod;
}
}

// PostSaving fires after a successful save; PostRestore after a successful load.
// Both receive the user-data pointer set via SetUserData (z_zodiac.cpp:68,:150).
TEST(FacadeCallbacks, PostSaveAndPostRestoreFireWithUserData)
{
	Hooks hooks;
	std::vector<char> image;
	{
		TestEngine e;
		BuildTrivial(e.get());
		auto z = MakeZodiac(e.get());
		z->SetUserData(&hooks);
		z->SetPreSavingCallback ([](void * ud){ ((Hooks*)ud)->preSave++;  });
		z->SetPostSavingCallback([](void * ud){ ((Hooks*)ud)->postSave++; });

		zCMemoryFile file;
		ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();
		image = file.bytes();
	}
	EXPECT_EQ(hooks.preSave, 1)  << "pre-saving callback must fire once";
	EXPECT_EQ(hooks.postSave, 1) << "post-saving callback must fire once after save";

	{
		TestEngine e;
		auto z = MakeZodiac(e.get());
		z->SetUserData(&hooks);
		z->SetPreRestoreCallback ([](void * ud){ ((Hooks*)ud)->preRestore++;  });
		z->SetPostRestoreCallback([](void * ud){ ((Hooks*)ud)->postRestore++; });

		zCMemoryFile file(image);
		ASSERT_EQ(z->LoadFromFile(&file), zE_Success) << z->GetErrorString();
	}
	EXPECT_EQ(hooks.preRestore, 1)  << "pre-restore callback must fire once";
	EXPECT_EQ(hooks.postRestore, 1) << "post-restore callback must fire once after load";
}

// The z-typeId <-> AS-typeId mapping accessors round-trip for a registered
// application type (here: the addon 'string' value type). Note the contract: the
// AS-typeId side of the map is only wired up once SortTypeList has run, which
// happens during a save/load — before the first save the accessor returns -1.
TEST(FacadeCallbacks, TypeIdAccessorsRoundTrip)
{
	TestEngine e;
	BuildTrivial(e.get());
	auto z = MakeZodiac(e.get());

	int asStringId = e->GetTypeIdByDecl("string");
	ASSERT_GE(asStringId, 0) << "string addon type should be registered";

	// Before any save the AS->z map is not yet populated.
	EXPECT_EQ(z->GetZTypeIdFromAsTypeId(asStringId), -1)
		<< "AS->z map is only built during save/load";

	// A save runs SortTypeList, which wires entry.asTypeId for every type.
	zCMemoryFile file;
	ASSERT_EQ(z->SaveToFile(&file), zE_Success) << z->GetErrorString();

	int zId = z->GetZTypeIdFromAsTypeId(asStringId);
	ASSERT_GE(zId, 0) << "after a save, a registered type must have a z-typeId";
	EXPECT_EQ(z->GetAsTypeIdFromZTypeId(zId), asStringId)
		<< "z-typeId must map back to the original AS typeId";
}

// Saving the same zodiac twice must be well-defined: either both succeed with an
// identical image, or the second is rejected with a Code — never a crash, leak,
// or divergent image. (There is a m_loaded guard for load but none for save.)
TEST(FacadeCallbacks, RepeatedSaveIsWellDefined)
{
	TestEngine e;
	BuildTrivial(e.get());
	auto z = MakeZodiac(e.get());

	zCMemoryFile file1, file2;
	Code rc1 = z->SaveToFile(&file1);
	ASSERT_EQ(rc1, zE_Success) << z->GetErrorString();

	Code rc2 = z->SaveToFile(&file2);
	if(rc2 == zE_Success)
		EXPECT_EQ(file1.bytes(), file2.bytes())
			<< "a second successful save must produce an identical image";
	else
		EXPECT_NE(rc2, zE_Success) << "otherwise the second save must report a Code";
}
