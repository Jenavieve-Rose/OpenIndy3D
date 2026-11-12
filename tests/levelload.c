/*
 * Level-load smoke test (PORT-PLAN.md Phase 1 and 7).
 *
 * Starts the engine the way JonesMain_Startup does, minus the display, input, sound output and dialogs (harness.c);
 * loads the static world; then loads each level with sithWorld_Load and frees it. Every binary section loader of the
 * .cnd format runs, which is where a wrong 64-bit layout shows up (docs/PORTING.md, "64-bit audit").
 *
 * For each level it prints the section counts, so a misread that happens to "succeed" is still visible as nonsense.
 *
 * Usage: levelload <data root> [level.cnd ...]
 *   <data root>  the folder holding RESOURCE/ (as --data for the game)
 *   level.cnd    names under ndy\ in the GOBs; default: all 17 levels
 * Exit status: number of levels (and the static world) that failed to load.
 */
#include <stdio.h>

#include <j3dcore/j3d.h>

#include <sith/Main/sithMain.h>
#include <sith/World/sithWorld.h>

#include "harness.h"

static const char* aDefaultLevels[] = {
    "00_cyn.cnd", "01_bab.cnd", "02_riv.cnd", "03_shs.cnd", "05_lag.cnd", "06_vol.cnd", "07_tem.cnd", "08_teo.cnd",
    "09_olv.cnd", "10_sea.cnd", "11_pyr.cnd", "12_sol.cnd", "13_nub.cnd", "14_inf.cnd", "15_aet.cnd", "16_jep.cnd",
    "17_pru.cnd",
    // 04 does not exist in the retail data; the bonus level is 17_pru.
};

static void levelload_PrintWorld(const char* pName, const SithWorld* pWorld)
{
    printf("  %-22s sectors %5zu  surfaces %6zu  verts %6zu  materials %4zu  models %4zu  keyframes %4zu  "
           "templates %4zu  things %5zu  cogs %4zu  sounds %4zu  pvs %zu\n",
           pName, (size_t)pWorld->numSectors, (size_t)pWorld->numSurfaces, (size_t)pWorld->numVertices,
           (size_t)pWorld->numMaterials, (size_t)pWorld->numModels, (size_t)pWorld->numKeyframes,
           (size_t)pWorld->numThingTemplates, (size_t)pWorld->numThings, (size_t)pWorld->numCogs,
           (size_t)pWorld->numSounds, (size_t)pWorld->sizePVS);
}

int main(int argc, char** argv)
{
    if ( argc < 2 )
    {
        fprintf(stderr, "usage: %s <data root> [level.cnd ...]\n", argv[0]);
        return 2;
    }

    harness_InstallHandlers();

    if ( harness_Startup(argv[1]) )
    {
        return 1;
    }

    int numFailed = 0;
    printf("static world:\n");
    if ( sithOpenStatic("jones3dstatic.cnd") )
    {
        printf("  FAIL jones3dstatic.cnd\n");
        return 1;
    }
    levelload_PrintWorld("jones3dstatic.cnd", sithWorld_pStaticWorld);

    const char** apLevels = argc > 2 ? (const char**)&argv[2] : aDefaultLevels;
    size_t numLevels = argc > 2 ? (size_t)(argc - 2) : sizeof(aDefaultLevels) / sizeof(aDefaultLevels[0]);

    printf("levels:\n");
    for ( size_t i = 0; i < numLevels; ++i )
    {
        char aPath[128];
        snprintf(aPath, sizeof(aPath), "ndy\\%s", apLevels[i]);

        SithWorld* pWorld = sithWorld_NewEntry();
        if ( !pWorld )
        {
            fprintf(stderr, "levelload: out of memory\n");
            return 1;
        }

        sithWorld_pCurrentWorld = pWorld;
        if ( sithWorld_Load(pWorld, aPath) )
        {
            printf("  FAIL %s\n", apLevels[i]);
            ++numFailed;
        }
        else
        {
            levelload_PrintWorld(apLevels[i], pWorld);
        }
        sithWorld_Free(pWorld);
        sithWorld_pCurrentWorld = NULL;
    }

    sithCloseStatic();
    printf("%d of %zu level(s) failed\n", numFailed, numLevels);
    return numFailed;
}
