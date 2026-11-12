/*
 * COG compile test (PORT-PLAN.md Phase 1).
 *
 * Lists every .cog in JONES3D.GOB, CD1.GOB and CD2.GOB, and compiles each once through the engine's normal lookup
 * (sithCogParse_Load on "cog\<name>"), so where a loose file in RESOURCE/Cog overrides a GOB entry, the loose file is
 * the one compiled, as in the game. The level-load test only compiles the scripts levels reference; this covers all.
 *
 * Usage: cogcheck <data root>
 * Exit status: number of scripts that failed to compile (capped at 125).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <j3dcore/j3d.h>

#include <sith/Cog/sithCog.h>
#include <sith/Cog/sithCogParse.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithWorld.h>

#include "harness.h"

typedef struct CogName
{
    char aName[128];
} CogName;

static CogName* cogcheck_aNames   = NULL;
static size_t   cogcheck_numNames = 0;
static size_t   cogcheck_sizeNames = 0;

static int cogcheck_HasName(const char* pName)
{
    for ( size_t i = 0; i < cogcheck_numNames; ++i )
    {
        if ( strcasecmp(cogcheck_aNames[i].aName, pName) == 0 )
        {
            return 1;
        }
    }
    return 0;
}

// Reads a GOB directory (header "GOB ", version 20, directory offset; then count and {offset, size, name[128]}
// entries, all little-endian) and collects the names under cog\ ending in .cog.
static int cogcheck_ListGob(const char* pPath)
{
    FILE* pFile = fopen(pPath, "rb");
    if ( !pFile )
    {
        fprintf(stderr, "cogcheck: cannot open %s\n", pPath);
        return 1;
    }

    uint8_t aHeader[12];
    if ( fread(aHeader, 1, sizeof(aHeader), pFile) != sizeof(aHeader) || memcmp(aHeader, "GOB ", 4) != 0 )
    {
        fprintf(stderr, "cogcheck: %s is not a GOB\n", pPath);
        fclose(pFile);
        return 1;
    }

    int32_t dirOffset, numEntries;
    memcpy(&dirOffset, aHeader + 8, 4);
    if ( fseek(pFile, dirOffset, SEEK_SET) != 0 || fread(&numEntries, 4, 1, pFile) != 1 )
    {
        fclose(pFile);
        return 1;
    }

    for ( int32_t i = 0; i < numEntries; ++i )
    {
        uint8_t aEntry[136];
        if ( fread(aEntry, 1, sizeof(aEntry), pFile) != sizeof(aEntry) )
        {
            fclose(pFile);
            return 1;
        }

        char aName[129];
        memcpy(aName, aEntry + 8, 128);
        aName[128] = '\0';

        size_t len = strlen(aName);
        if ( strncasecmp(aName, "cog\\", 4) != 0 || len < 8 || strcasecmp(aName + len - 4, ".cog") != 0 )
        {
            continue;
        }

        const char* pBase = aName + 4;
        if ( cogcheck_HasName(pBase) )
        {
            continue;
        }

        if ( cogcheck_numNames == cogcheck_sizeNames )
        {
            cogcheck_sizeNames = cogcheck_sizeNames ? cogcheck_sizeNames * 2 : 1024;
            cogcheck_aNames = realloc(cogcheck_aNames, cogcheck_sizeNames * sizeof(CogName));
            if ( !cogcheck_aNames )
            {
                fclose(pFile);
                return 1;
            }
        }
        snprintf(cogcheck_aNames[cogcheck_numNames++].aName, sizeof(cogcheck_aNames[0].aName), "%s", pBase);
    }

    fclose(pFile);
    return 0;
}

int main(int argc, char** argv)
{
    if ( argc < 2 )
    {
        fprintf(stderr, "usage: %s <data root>\n", argv[0]);
        return 2;
    }

    harness_InstallHandlers();

    static const char* aGobs[] = { "JONES3D.GOB", "CD1.GOB", "CD2.GOB" };
    for ( size_t i = 0; i < sizeof(aGobs) / sizeof(aGobs[0]); ++i )
    {
        char aPath[1024];
        snprintf(aPath, sizeof(aPath), "%s/RESOURCE/%s", argv[1], aGobs[i]);
        if ( cogcheck_ListGob(aPath) )
        {
            return 1;
        }
    }

    if ( harness_Startup(argv[1]) )
    {
        return 1;
    }

    // Scripts resolve some symbols against the current world; the static world is the one that is always there.
    if ( sithOpenStatic("jones3dstatic.cnd") )
    {
        fprintf(stderr, "cogcheck: the static world failed to load\n");
        return 1;
    }
    sithWorld_pCurrentWorld = sithWorld_pStaticWorld;

    int numFailed = 0;
    for ( size_t i = 0; i < cogcheck_numNames; ++i )
    {
        char aPath[160];
        snprintf(aPath, sizeof(aPath), "cog\\%s", cogcheck_aNames[i].aName);

        SithCogScript script;
        memset(&script, 0, sizeof(script));
        if ( !sithCogParse_Load(aPath, &script, 0) )
        {
            printf("  FAIL %s\n", cogcheck_aNames[i].aName);
            ++numFailed;
        }
        sithCog_FreeScriptEntry(&script);
    }

    sithWorld_pCurrentWorld = NULL;
    sithCloseStatic();
    printf("%zu script(s) compiled, %d failed\n", cogcheck_numNames, numFailed);
    return numFailed > 125 ? 125 : numFailed;
}
