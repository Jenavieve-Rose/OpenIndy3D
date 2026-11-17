/*
 * outfit: Indy's outfit, the "Indy Outfit" setting (docs/PORTING.md, "Indy's outfit").
 *
 * Runs the game headless, as saveload does, starting the level with the outfit in J3D_REG_Indy_Outfit (0 as the level
 * chooses, 1 jacket, 2 no jacket). It checks that the local player and Indy's templates (the player and cutscene actor
 * templates of both outfits) are drawn with the model the setting asks for: the level's own choice with 0, else the
 * chosen outfit's model when the level holds it. The level's choice is read from the templates' names, which the
 * setting does not change (shirtplayer and indy_sh_actor are jacketless, walkplayer and indy_actor wear the jacket).
 *
 * Then it saves, and restores that save with each outfit in turn (1, 2, 0), checking the same after each restore and
 * running frames so the restored Indy plays: a save follows the outfit chosen when it is loaded.
 *
 *   outfit <data root> <level number> [frames]
 *
 * Saves go under "Install Path" (J3D_REG_Install_Path). Exit 0 on success.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Main/JonesMain.h>
#include <platform/common/host_dirs.h>
#include <sith/Dss/sithGamesave.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWorld.h>
#include <std/Win95/stdWin95.h>

#include "harness.h"

int J3DAPI sithGamesave_SaveFile(const char* pFilename);                 // sithGamesave.c, not in its header
int J3DAPI sithGamesave_RestoreFile(const char* pFilename, int bNotify);

// Indy's templates and the model each has in the level data.
static const struct
{
    const char* pTemplate;
    const char* pModel;
} outfit_aTemplates[] = {
    { "walkplayer",      "gen_in.3do"       },
    { "indy_actor",      "gen_in.3do"       },
    { "shirtplayer",     "gen_in_shirt.3do" },
    { "indy_sh_actor",   "gen_in_shirt.3do" },
    { "raftplayer",      "gen_ir.3do"       },
    { "shirtraftplayer", "gen_ir_shirt.3do" },
};

// { with the jacket, without }
static const char* const outfit_aaPairs[][2] = {
    { "gen_in.3do", "gen_in_shirt.3do" },
    { "gen_ir.3do", "gen_ir_shirt.3do" },
};

static int outfit_RunFrames(unsigned long numFrames)
{
    for ( unsigned long i = 0; i < numFrames; ++i )
    {
        if ( JonesMain_Process() )
        {
            fprintf(stderr, "outfit: the game loop ended after %lu of %lu frames\n", i, numFrames);
            return 1;
        }
    }
    return 0;
}

// The model the setting gives a thing whose level data names pRetail.
static const char* outfit_Expected(const char* pRetail, int outfit)
{
    if ( outfit != SITHMODEL_OUTFIT_JACKET && outfit != SITHMODEL_OUTFIT_NOJACKET )
    {
        return pRetail;
    }
    for ( size_t i = 0; i < sizeof(outfit_aaPairs) / sizeof(outfit_aaPairs[0]); ++i )
    {
        if ( !strcasecmp(pRetail, outfit_aaPairs[i][0]) || !strcasecmp(pRetail, outfit_aaPairs[i][1]) )
        {
            const char* pWant = outfit_aaPairs[i][outfit == SITHMODEL_OUTFIT_JACKET ? 0 : 1];
            return sithModel_GetModel(pWant) ? pWant : pRetail;
        }
    }
    return pRetail;
}

static const char* outfit_ModelName(const SithThing* pThing)
{
    if ( pThing->renderData.type != RD_THING_MODEL3 || !pThing->renderData.data.pModel3 )
    {
        return "(none)";
    }
    return pThing->renderData.data.pModel3->aName;
}

// Returns the number of things not drawn as expected.
static int outfit_Check(const char* pWhen, int outfit)
{
    int numWrong = 0;
    const SithWorld* pWorld = sithWorld_pCurrentWorld;
    printf("%s, outfit %d (latched %d):\n", pWhen, outfit, sithModel_GetOutfit());
    if ( sithModel_GetOutfit() != outfit )
    {
        printf("  WRONG latched outfit\n");
        ++numWrong;
    }

    for ( size_t i = 0; i < sizeof(outfit_aTemplates) / sizeof(outfit_aTemplates[0]); ++i )
    {
        for ( size_t t = 0; t < pWorld->numThingTemplates; ++t )
        {
            const SithThing* pTemplate = &pWorld->aThingTemplates[t];
            if ( strcasecmp(pTemplate->aName, outfit_aTemplates[i].pTemplate) )
            {
                continue;
            }
            const char* pWant = outfit_Expected(outfit_aTemplates[i].pModel, outfit);
            const char* pHave = outfit_ModelName(pTemplate);
            int bOk = !strcasecmp(pWant, pHave);
            printf("  template %-16s %-18s %s\n", pTemplate->aName, pHave, bOk ? "ok" : "WRONG");
            numWrong += !bOk;
        }
    }

    const SithThing* pPlayer = sithPlayer_pLocalPlayerThing;
    if ( !pPlayer || !pPlayer->pTemplate )
    {
        printf("  WRONG: no local player\n");
        return numWrong + 1;
    }
    for ( size_t i = 0; i < sizeof(outfit_aTemplates) / sizeof(outfit_aTemplates[0]); ++i )
    {
        if ( !strcasecmp(pPlayer->pTemplate->aName, outfit_aTemplates[i].pTemplate) )
        {
            const char* pWant = outfit_Expected(outfit_aTemplates[i].pModel, outfit);
            const char* pHave = outfit_ModelName(pPlayer);
            int bOk = !strcasecmp(pWant, pHave);
            printf("  player   %-16s %-18s %s\n", pPlayer->pTemplate->aName, pHave, bOk ? "ok" : "WRONG");
            return numWrong + !bOk;
        }
    }
    printf("  player   %-16s %-18s (not one of Indy's walking templates)\n", pPlayer->pTemplate->aName, outfit_ModelName(pPlayer));
    return numWrong;
}

static void outfit_Set(int outfit)
{
    char aValue[8];
    snprintf(aValue, sizeof(aValue), "%d", outfit);
    setenv("J3D_REG_Indy_Outfit", aValue, 1);
}

int main(int argc, char** argv)
{
    static const GUID outfit_guid = { 0x82CE4DA0, 0x9CBF, 0x11D1, { 0x90, 0x85, 0x00, 0x60, 0x97, 0x76, 0xEA, 0x02 } };

    harness_InstallHandlers();
    if ( argc < 3 )
    {
        fprintf(stderr, "usage: %s <data root> <level number> [frames]\n", argv[0]);
        return 2;
    }
    unsigned long frames = argc > 3 ? strtoul(argv[3], NULL, 10) : 300;
    const char* pStart = getenv("J3D_REG_Indy_Outfit");
    int startOutfit = pStart ? atoi(pStart) : 0;

    setenv("J3D_DATA", argv[1], 1);
    setenv("J3D_HEADLESS", "1", 1);
    if ( !getenv("J3D_SETTINGS") )
    {
        setenv("J3D_SETTINGS", "/dev/null", 1);
    }
    const char* pInstallPath = getenv("J3D_REG_Install_Path");
    if ( !pInstallPath || J3DHost_MakeDirs(pInstallPath) )
    {
        fprintf(stderr, "outfit: set J3D_REG_Install_Path to a folder the test may write to\n");
        return 2;
    }

    stdWin95_SetWindow(NULL);
    stdWin95_SetInstance(NULL);
    stdWin95_SetGuid(&outfit_guid);
    if ( JonesMain_Startup(argv[2]) )
    {
        fprintf(stderr, "outfit: JonesMain_Startup failed\n");
        return 1;
    }
    if ( outfit_RunFrames(frames) )
    {
        return 1;
    }
    if ( !sithWorld_pCurrentWorld || !sithPlayer_pLocalPlayerThing )
    {
        fprintf(stderr, "outfit: no level is running after %lu frames\n", frames);
        return 1;
    }

    int numWrong = outfit_Check(sithWorld_pCurrentWorld->aName, startOutfit);

    char aPath[J3D_PATHSIZE];
    snprintf(aPath, sizeof(aPath), "%s/outfit.nds", sithGetSaveGamesDir());
    sithMakeDirs();
    if ( sithGamesave_SaveFile(aPath) )
    {
        fprintf(stderr, "outfit: saving %s failed\n", aPath);
        return 1;
    }

    static const int aRestoreOutfits[] = { SITHMODEL_OUTFIT_JACKET, SITHMODEL_OUTFIT_NOJACKET, SITHMODEL_OUTFIT_GAME };
    for ( size_t i = 0; i < sizeof(aRestoreOutfits) / sizeof(aRestoreOutfits[0]); ++i )
    {
        outfit_Set(aRestoreOutfits[i]);
        if ( sithGamesave_RestoreFile(aPath, /*bNotify=*/0) )
        {
            fprintf(stderr, "outfit: restoring %s failed\n", aPath);
            return 1;
        }
        numWrong += outfit_Check("restored", aRestoreOutfits[i]);
        if ( outfit_RunFrames(frames) )
        {
            return 1;
        }
    }

    JonesMain_Shutdown();
    printf("%s\n", numWrong ? "FAILED" : "passed");
    return numWrong != 0;
}
