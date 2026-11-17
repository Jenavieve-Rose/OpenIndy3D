/*
 * saveload: the savegame round trip (PORT-PLAN.md P1.20).
 *
 * Runs the game itself headless, as `jones3d --headless` does, so the level is set up exactly as a player gets it:
 * JonesMain_Startup loads the first level and the game loop runs it. After some frames it saves with
 * sithGamesave_SaveFile, records the world, restores the save with sithGamesave_RestoreFile (which closes the world and
 * loads the level again from the file), records the world again and compares the two. It then saves the restored game
 * and requires the second file to be byte-for-byte the first, and runs more frames to show the restored game goes on.
 *
 *   saveload <data root> [frames before the save] [frames after the restore] [level number]
 *
 * The level number is the game's own, 1 to 17 in story order (JonesMain_Startup reads it from the command line, as
 * `jones3d <n>` does); the default is the first level. ctest runs every level (PORT-PLAN.md P7.2).
 *
 * Saves go under "Install Path" (J3D_REG_Install_Path; ctest points it into the build tree). Exit 0 on success.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Main/JonesMain.h>
#include <platform/common/host_dirs.h>
#include <sith/Cog/sithCog.h>
#include <sith/Cog/sithCogParse.h>
#include <sith/Dss/sithGamesave.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithSector.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWorld.h>
#include <std/Win95/stdWin95.h>

#include "harness.h"

int J3DAPI sithGamesave_SaveFile(const char* pFilename);                 // sithGamesave.c, not in its header
int J3DAPI sithGamesave_RestoreFile(const char* pFilename, int bNotify);

#define SAVELOAD_MAXTHINGS  8192 // more than any level holds; a level that has more fails the test (saveload_Capture)
#define SAVELOAD_NUMGLOBALS 16   // NdsHeader.aCogGlobalValues

typedef struct SaveloadThing
{
    int idx;
    int type;
    int guid;
    uint32_t signature;
    uint32_t flags;
    rdVector3 pos;
    int sectorIdx;
    float health;   // actors and players only
} SaveloadThing;

typedef struct SaveloadState
{
    char aWorldName[64];
    uint32_t msecGameTime;
    size_t numThings;   // things with a type
    int bTruncated;     // the level holds more things than aThings
    SaveloadThing aThings[SAVELOAD_MAXTHINGS];
    int playerIdx;
    SithCogSymbolValue aGlobals[SAVELOAD_NUMGLOBALS];
    int aGlobalFound[SAVELOAD_NUMGLOBALS];
} SaveloadState;

static SaveloadState saveload_before;
static SaveloadState saveload_after;

static void saveload_Capture(SaveloadState* pState)
{
    const SithWorld* pWorld = sithWorld_pCurrentWorld;
    memset(pState, 0, sizeof(*pState));
    snprintf(pState->aWorldName, sizeof(pState->aWorldName), "%s", pWorld->aName);
    pState->msecGameTime = sithTime_msecGameTime;
    pState->playerIdx    = sithPlayer_pLocalPlayerThing ? sithPlayer_pLocalPlayerThing->idx : -1;

    for ( int i = 0; i <= pWorld->lastThingIdx; ++i )
    {
        const SithThing* pThing = &pWorld->aThings[i];
        // Only what a save holds: no free slots, and no timed debris or particles, which sithThing_CanSync leaves out
        // (a level whose scripts have one alive at the save is not a failed round trip)
        if ( !sithThing_CanSync(pThing) )
        {
            continue;
        }
        if ( pState->numThings >= SAVELOAD_MAXTHINGS )
        {
            pState->bTruncated = 1;
            break;
        }

        SaveloadThing* pOut = &pState->aThings[pState->numThings++];
        pOut->idx       = i;
        pOut->type      = pThing->type;
        pOut->guid      = pThing->guid;
        pOut->signature = pThing->signature;
        pOut->flags     = pThing->flags;
        pOut->pos       = pThing->pos;
        pOut->sectorIdx = pThing->pInSector ? sithSector_GetSectorIndex(pThing->pInSector) : -1;
        if ( pThing->type == SITH_THING_ACTOR || pThing->type == SITH_THING_PLAYER )
        {
            pOut->health = pThing->thingInfo.actorInfo.health;
        }
    }

    for ( int n = 0; n < SAVELOAD_NUMGLOBALS; ++n )
    {
        char aName[32];
        snprintf(aName, sizeof(aName), "global%d", n);
        const SithCogSymbol* pSymbol = sithCogParse_GetSymbol(sithCog_pSymbolTable, aName);
        if ( pSymbol )
        {
            pState->aGlobalFound[n] = 1;
            pState->aGlobals[n].type = pSymbol->value.type;
            if ( pSymbol->value.type == SITHCOG_VALUE_INT || pSymbol->value.type == SITHCOG_VALUE_FLOAT )
            {
                pState->aGlobals[n].val.intValue = pSymbol->value.val.intValue;
            }
            else if ( pSymbol->value.type == SITHCOG_VALUE_VECTOR )
            {
                pState->aGlobals[n].val.vecValue = pSymbol->value.val.vecValue;
            }
        }
    }
}

static int saveload_VecEqual(const rdVector3* pA, const rdVector3* pB)
{
    return memcmp(pA, pB, sizeof(*pA)) == 0; // bit-exact: positions are stored as raw floats
}

// Prints every difference. Returns the number found.
static int saveload_Compare(const SaveloadState* pA, const SaveloadState* pB)
{
    int numDiffs = 0;
#define SAVELOAD_DIFF(...) do { printf("  DIFF: " __VA_ARGS__); ++numDiffs; } while ( 0 )

    if ( strcmp(pA->aWorldName, pB->aWorldName) )
    {
        SAVELOAD_DIFF("world %s / %s\n", pA->aWorldName, pB->aWorldName);
    }
    if ( pA->msecGameTime != pB->msecGameTime )
    {
        SAVELOAD_DIFF("game time %u / %u ms\n", pA->msecGameTime, pB->msecGameTime);
    }
    if ( pA->playerIdx != pB->playerIdx )
    {
        SAVELOAD_DIFF("player thing %d / %d\n", pA->playerIdx, pB->playerIdx);
    }
    if ( pA->bTruncated || pB->bTruncated )
    {
        SAVELOAD_DIFF("more than %d things: raise SAVELOAD_MAXTHINGS\n", SAVELOAD_MAXTHINGS);
    }
    if ( pA->numThings != pB->numThings )
    {
        SAVELOAD_DIFF("thing count %zu / %zu\n", pA->numThings, pB->numThings);
    }

    for ( size_t i = 0; i < pA->numThings && i < pB->numThings; ++i )
    {
        const SaveloadThing* pTA = &pA->aThings[i];
        const SaveloadThing* pTB = &pB->aThings[i];
        if ( pTA->idx != pTB->idx || pTA->type != pTB->type || pTA->guid != pTB->guid )
        {
            SAVELOAD_DIFF("thing slot %zu: idx %d type %d guid %d / idx %d type %d guid %d\n", i, pTA->idx, pTA->type,
                pTA->guid, pTB->idx, pTB->type, pTB->guid);
            continue;
        }
        if ( pTA->signature != pTB->signature )
        {
            SAVELOAD_DIFF("thing %d: signature %u / %u\n", pTA->idx, pTA->signature, pTB->signature);
        }
        if ( pTA->flags != pTB->flags )
        {
            SAVELOAD_DIFF("thing %d: flags 0x%x / 0x%x\n", pTA->idx, pTA->flags, pTB->flags);
        }
        if ( !saveload_VecEqual(&pTA->pos, &pTB->pos) )
        {
            SAVELOAD_DIFF("thing %d: pos (%g %g %g) / (%g %g %g)\n", pTA->idx, pTA->pos.x, pTA->pos.y, pTA->pos.z,
                pTB->pos.x, pTB->pos.y, pTB->pos.z);
        }
        if ( pTA->sectorIdx != pTB->sectorIdx )
        {
            SAVELOAD_DIFF("thing %d: sector %d / %d\n", pTA->idx, pTA->sectorIdx, pTB->sectorIdx);
        }
        if ( memcmp(&pTA->health, &pTB->health, sizeof(float)) )
        {
            const SithThing* pThing = &sithWorld_pCurrentWorld->aThings[pTA->idx];
            SAVELOAD_DIFF("thing %d (%s, type %d): health %g / %g\n", pTA->idx, pThing->aName, pTA->type, pTA->health, pTB->health);
        }
    }

    for ( int n = 0; n < SAVELOAD_NUMGLOBALS; ++n )
    {
        if ( pA->aGlobalFound[n] != pB->aGlobalFound[n]
            || memcmp(&pA->aGlobals[n], &pB->aGlobals[n], sizeof(pA->aGlobals[n])) )
        {
            SAVELOAD_DIFF("cog global%d: type %d value %d / type %d value %d\n", n, pA->aGlobals[n].type,
                pA->aGlobals[n].val.intValue, pB->aGlobals[n].type, pB->aGlobals[n].val.intValue);
        }
    }
#undef SAVELOAD_DIFF
    return numDiffs;
}

static int saveload_RunFrames(unsigned long numFrames)
{
    for ( unsigned long i = 0; i < numFrames; ++i )
    {
        if ( JonesMain_Process() )
        {
            fprintf(stderr, "saveload: the game loop ended after %lu of %lu frames\n", i, numFrames);
            return 1;
        }
    }
    return 0;
}

// Returns the file's contents (caller frees) and its size, or NULL.
static unsigned char* saveload_ReadFile(const char* pPath, size_t* pSize)
{
    FILE* fp = fopen(pPath, "rb");
    if ( !fp )
    {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* pData = size > 0 ? malloc((size_t)size) : NULL;
    if ( pData && fread(pData, 1, (size_t)size, fp) != (size_t)size )
    {
        free(pData);
        pData = NULL;
    }
    fclose(fp);
    *pSize = pData ? (size_t)size : 0;
    return pData;
}

// A .nds file is NdsHeader, the thumbnail (absent here: sithGamesave_SaveFile is called directly, so no thumbnail is
// pending), the world as DSS messages ({uint16 type, uint32 length, data}, ended by SITHDSS_ENDSECTION), then the sound
// mixer and weapon sections. sithDSS.h numbers the message types.
#define SAVELOAD_DSS_AISTATUS   15
#define SAVELOAD_DSS_ANIMSTATUS 17
#define SAVELOAD_DSS_ENDSECTION 44
#define SAVELOAD_MAXBLOCKS      4096

typedef struct SaveloadBlock
{
    uint16_t type;
    uint32_t length;
    const unsigned char* pData;
} SaveloadBlock;

// Splits the message section into blocks. Returns the block count and sets *pTail to the offset after ENDSECTION, or
// returns -1 if the file does not parse.
static int saveload_SplitBlocks(const unsigned char* pFile, size_t size, SaveloadBlock* aBlocks, size_t* pTail)
{
    size_t offset = sizeof(NdsHeader);
    int num = 0;
    while ( offset + 6 <= size && num < SAVELOAD_MAXBLOCKS )
    {
        SaveloadBlock* pBlock = &aBlocks[num++];
        memcpy(&pBlock->type, pFile + offset, sizeof(pBlock->type));
        memcpy(&pBlock->length, pFile + offset + 2, sizeof(pBlock->length));
        pBlock->pData = pFile + offset + 6;
        offset += 6 + (size_t)pBlock->length;
        if ( offset > size )
        {
            return -1;
        }
        if ( pBlock->type == SAVELOAD_DSS_ENDSECTION )
        {
            *pTail = offset;
            return num;
        }
    }
    return -1;
}

// Prints a differing message's bytes from both saves (the first 64), so a failure can be read from the test's output.
static void saveload_DumpBlocks(const SaveloadBlock* pA, const SaveloadBlock* pB)
{
    const SaveloadBlock* apBlocks[2] = { pA, pB };
    for ( int n = 0; n < 2; ++n )
    {
        printf("    %s:", n ? "second" : "first ");
        for ( uint32_t i = 0; i < apBlocks[n]->length && i < 64; ++i )
        {
            printf("%s%02x", (i % 4) ? "" : " ", apBlocks[n]->pData[i]);
        }
        printf("\n");
    }
}

static int saveload_BlockEqual(const SaveloadBlock* pA, const SaveloadBlock* pB, size_t skip)
{
    return pA->type == pB->type && pA->length == pB->length && pA->length >= skip
        && memcmp(pA->pData + skip, pB->pData + skip, pA->length - skip) == 0;
}

// Compares the save of the restored game (B) with the original save (A). Everything must be identical except
// differences that are retail behaviour, not port bugs (docs/PORTING.md, "Savegame round trip"):
//   - AI status messages come out in a different order (the AI blocks are reallocated on restore): compared as a set;
//   - an animation's ID is its slot number, and restore reallocates slots without holes: the ID is not compared;
//   - the sound section's channels (saveload_CompareSound).
// (Sound_Restore's pause time, the fifth word, compares since the port fixed its sign, PORT-PLAN.md P4.9.)

// One saved sound channel (Sound_Save): its name, then 17 words, the fourth the channel handle, the thirteenth the flags
// and the fourteenth the guid that names the channel to the game.
#define SAVELOAD_MAXCHANNELS 256
#define SAVELOAD_CHANNEL_HANDLE 3
#define SAVELOAD_CHANNEL_FLAGS 12
#define SAVELOAD_CHANNEL_GUID 13
#define SAVELOAD_SOUND_LOOP 0x2u
#define SAVELOAD_SOUND_FAR 0x200000u

typedef struct SaveloadChannel
{
    const unsigned char* pName;
    uint32_t nameLen;
    uint32_t aWords[17];
} SaveloadChannel;

// Splits the channel list that starts at pSec + 20 (after the ambient handle, magic, pause count, elapsed and pause
// times). Returns the channel count and sets *pEnd to the offset of the end-of-list word, or returns -1.
static int saveload_SplitChannels(const unsigned char* pSec, size_t size, SaveloadChannel* aChannels, size_t* pEnd)
{
    size_t off = 5 * sizeof(uint32_t);
    int num = 0;
    uint32_t nameLen;
    while ( off + sizeof(uint32_t) <= size && (memcpy(&nameLen, pSec + off, sizeof(nameLen)), nameLen != 0) )
    {
        if ( num == SAVELOAD_MAXCHANNELS || off + sizeof(uint32_t) + nameLen + sizeof(aChannels->aWords) > size )
        {
            return -1;
        }
        SaveloadChannel* pChannel = &aChannels[num++];
        pChannel->nameLen = nameLen;
        pChannel->pName   = pSec + off + sizeof(uint32_t);
        memcpy(pChannel->aWords, pChannel->pName + nameLen, sizeof(pChannel->aWords));
        off += sizeof(uint32_t) + nameLen + sizeof(pChannel->aWords);
    }
    if ( off + sizeof(uint32_t) > size )
    {
        return -1;
    }
    *pEnd = off;
    return num;
}

// The sound section (sithSoundMixer_GameSave, Sound_Save) and the weapon section after it. Retail's restore does not
// give the channels back exactly as they were saved (phase 8 fix series):
//   - the ambient channel's handle (word 0): sithSoundMixer_GameRestore stops the ambient channel, and the mixer starts
//     it again, under a new handle, on the next frame: not compared;
//   - the order: Sound_Save walks the channels from the last, and Sound_Restore replays them in the saved order into
//     new channels, so the second save lists them in another order: channels are matched by guid;
//   - each channel's handle: Sound_Restore reads it into the handle entropy and Sound_GenerateChannelHandle adds
//     SOUND_HANDLE_MIN (1234) to it, so a restored channel's handle is 1234 higher;
//   - each channel's flags gain SOUND_CHANNEL_FAR: Sound_Restore moves the listener 100,000 units away before replaying
//     the channels, so each starts far and the next Sound_Update brings it back;
//   - a channel saved only because it was far, and not looping (a one-shot sound out of earshot), is not replayed.
// Everything else (the other channel words, the names, the entropy and fades after the list, the weapon section) must
// be identical.
static int saveload_CompareSound(const unsigned char* pSecA, size_t sizeA, const unsigned char* pSecB, size_t sizeB)
{
    static SaveloadChannel aChannelsA[SAVELOAD_MAXCHANNELS], aChannelsB[SAVELOAD_MAXCHANNELS];
    int numDiffs = 0;
    size_t endA, endB;
#define SAVELOAD_SOUNDDIFF(...) do { printf("  FILE DIFF: " __VA_ARGS__); ++numDiffs; } while ( 0 )

    int numA = saveload_SplitChannels(pSecA, sizeA, aChannelsA, &endA);
    int numB = saveload_SplitChannels(pSecB, sizeB, aChannelsB, &endB);
    if ( numA < 0 || numB < 0 )
    {
        SAVELOAD_SOUNDDIFF("sound section: the channel list does not parse\n");
        return numDiffs;
    }

    if ( memcmp(pSecA + sizeof(uint32_t), pSecB + sizeof(uint32_t), 4 * sizeof(uint32_t)) )
    {
        SAVELOAD_SOUNDDIFF("sound section: the words after the ambient handle differ\n");
    }

    int aMatched[SAVELOAD_MAXCHANNELS] = { 0 };
    for ( int i = 0; i < numA; ++i )
    {
        const SaveloadChannel* pA = &aChannelsA[i];
        const SaveloadChannel* pB = NULL;
        for ( int k = 0; k < numB && !pB; ++k )
        {
            if ( !aMatched[k] && aChannelsB[k].aWords[SAVELOAD_CHANNEL_GUID] == pA->aWords[SAVELOAD_CHANNEL_GUID] )
            {
                aMatched[k] = 1;
                pB = &aChannelsB[k];
            }
        }

        const uint32_t flagsA = pA->aWords[SAVELOAD_CHANNEL_FLAGS];
        if ( !pB )
        {
            if ( (flagsA & SAVELOAD_SOUND_LOOP) || !(flagsA & SAVELOAD_SOUND_FAR) )
            {
                SAVELOAD_SOUNDDIFF("sound channel %.*s (guid %u) not restored\n", (int)pA->nameLen, pA->pName,
                    pA->aWords[SAVELOAD_CHANNEL_GUID]);
            }
            continue;
        }

        if ( pA->nameLen != pB->nameLen || memcmp(pA->pName, pB->pName, pA->nameLen) )
        {
            SAVELOAD_SOUNDDIFF("sound channel guid %u: another sound\n", pA->aWords[SAVELOAD_CHANNEL_GUID]);
            continue;
        }
        if ( pB->aWords[SAVELOAD_CHANNEL_HANDLE] != pA->aWords[SAVELOAD_CHANNEL_HANDLE] + 1234 )
        {
            SAVELOAD_SOUNDDIFF("sound channel handle %u restored as %u, not %u\n", pA->aWords[SAVELOAD_CHANNEL_HANDLE],
                pB->aWords[SAVELOAD_CHANNEL_HANDLE], pA->aWords[SAVELOAD_CHANNEL_HANDLE] + 1234);
        }
        if ( pB->aWords[SAVELOAD_CHANNEL_FLAGS] != (flagsA | SAVELOAD_SOUND_FAR) )
        {
            SAVELOAD_SOUNDDIFF("sound channel flags 0x%x restored as 0x%x\n", flagsA, pB->aWords[SAVELOAD_CHANNEL_FLAGS]);
        }
        for ( int w = 0; w < 17; ++w )
        {
            if ( w != SAVELOAD_CHANNEL_HANDLE && w != SAVELOAD_CHANNEL_FLAGS && pA->aWords[w] != pB->aWords[w] )
            {
                SAVELOAD_SOUNDDIFF("sound channel %.*s (guid %u): word %d 0x%x restored as 0x%x\n", (int)pA->nameLen,
                    pA->pName, pA->aWords[SAVELOAD_CHANNEL_GUID], w, pA->aWords[w], pB->aWords[w]);
            }
        }
    }
    for ( int k = 0; k < numB; ++k )
    {
        if ( !aMatched[k] )
        {
            SAVELOAD_SOUNDDIFF("sound channel %.*s (guid %u) is new in the second save\n", (int)aChannelsB[k].nameLen,
                aChannelsB[k].pName, aChannelsB[k].aWords[SAVELOAD_CHANNEL_GUID]);
        }
    }

    // After the list: the entropy, the fades and the weapon section.
    if ( sizeA - endA != sizeB - endB || memcmp(pSecA + endA, pSecB + endB, sizeA - endA) )
    {
        SAVELOAD_SOUNDDIFF("the handle entropy, fades or weapon section differ\n");
    }
#undef SAVELOAD_SOUNDDIFF
    return numDiffs;
}

static int saveload_CompareFiles(const char* pPathA, const char* pPathB)
{
    static SaveloadBlock aBlocksA[SAVELOAD_MAXBLOCKS], aBlocksB[SAVELOAD_MAXBLOCKS];
    size_t sizeA, sizeB, tailA = 0, tailB = 0;
    unsigned char* pA = saveload_ReadFile(pPathA, &sizeA);
    unsigned char* pB = saveload_ReadFile(pPathB, &sizeB);
    int numDiffs = 0;
#define SAVELOAD_FILEDIFF(...) do { printf("  FILE DIFF: " __VA_ARGS__); ++numDiffs; } while ( 0 )

    int numA = pA ? saveload_SplitBlocks(pA, sizeA, aBlocksA, &tailA) : -1;
    int numB = pB ? saveload_SplitBlocks(pB, sizeB, aBlocksB, &tailB) : -1;
    if ( numA < 0 || numB < 0 )
    {
        SAVELOAD_FILEDIFF("cannot read or parse %s or %s\n", pPathA, pPathB);
        goto done;
    }

    if ( memcmp(pA, pB, sizeof(NdsHeader)) )
    {
        SAVELOAD_FILEDIFF("headers differ\n");
    }
    if ( numA != numB )
    {
        SAVELOAD_FILEDIFF("%d / %d messages\n", numA, numB);
        goto done;
    }

    for ( int i = 0; i < numA; ++i )
    {
        const SaveloadBlock* pBlockA = &aBlocksA[i];
        const SaveloadBlock* pBlockB = &aBlocksB[i];
        if ( pBlockA->type != pBlockB->type )
        {
            SAVELOAD_FILEDIFF("message %d: type %d / %d\n", i, pBlockA->type, pBlockB->type);
            continue;
        }

        if ( pBlockA->type == SAVELOAD_DSS_AISTATUS )
        {
            // Some AI status message of B must equal this one.
            int bFound = 0;
            for ( int k = 0; k < numB && !bFound; ++k )
            {
                bFound = saveload_BlockEqual(pBlockA, &aBlocksB[k], 0);
            }
            if ( !bFound )
            {
                SAVELOAD_FILEDIFF("message %d: AI status not found in the second save\n", i);
            }
        }
        else if ( !saveload_BlockEqual(pBlockA, pBlockB, pBlockA->type == SAVELOAD_DSS_ANIMSTATUS ? sizeof(int32_t) : 0) )
        {
            SAVELOAD_FILEDIFF("message %d (type %d, %u bytes) differs\n", i, pBlockA->type, pBlockA->length);
            saveload_DumpBlocks(pBlockA, pBlockB);
        }
    }

    numDiffs += saveload_CompareSound(pA + tailA, sizeA - tailA, pB + tailB, sizeB - tailB);
#undef SAVELOAD_FILEDIFF

done:
    free(pA);
    free(pB);
    return numDiffs;
}

int main(int argc, char** argv)
{
    static const GUID saveload_guid = { 0x82CE4DA0, 0x9CBF, 0x11D1, { 0x90, 0x85, 0x00, 0x60, 0x97, 0x76, 0xEA, 0x02 } };

    harness_InstallHandlers();
    if ( argc < 2 )
    {
        fprintf(stderr, "usage: %s <data root> [frames before the save] [frames after the restore] [level number]\n", argv[0]);
        return 2;
    }
    unsigned long framesBefore = argc > 2 ? strtoul(argv[2], NULL, 10) : 300;
    unsigned long framesAfter  = argc > 3 ? strtoul(argv[3], NULL, 10) : 300;
    const char* pLevelNum      = argc > 4 ? argv[4] : ""; // the game's command line: the number of the level to start

    // What main.c does for `jones3d --data <root> --headless`, minus the user folders (the test sets Install Path).
    setenv("J3D_DATA", argv[1], 1);
    setenv("J3D_HEADLESS", "1", 1);
    if ( !getenv("J3D_SETTINGS") )
    {
        setenv("J3D_SETTINGS", "/dev/null", 1);
    }

    // The engine creates SaveGames/ but not the folder above it; jones3d's main does that (J3DHost_PrepareUserDirs).
    const char* pInstallPath = getenv("J3D_REG_Install_Path");
    if ( !pInstallPath || J3DHost_MakeDirs(pInstallPath) )
    {
        fprintf(stderr, "saveload: set J3D_REG_Install_Path to a folder the test may write to\n");
        return 2;
    }

    // What wkernel_Run does before its loop.
    stdWin95_SetWindow(NULL);
    stdWin95_SetInstance(NULL);
    stdWin95_SetGuid(&saveload_guid);
    if ( JonesMain_Startup(pLevelNum) )
    {
        fprintf(stderr, "saveload: JonesMain_Startup failed\n");
        return 1;
    }

    if ( saveload_RunFrames(framesBefore) )
    {
        return 1;
    }
    if ( !sithWorld_pCurrentWorld || !sithPlayer_pLocalPlayerThing )
    {
        fprintf(stderr, "saveload: no level is running after %lu frames\n", framesBefore);
        return 1;
    }

    char aPathA[J3D_PATHSIZE], aPathB[J3D_PATHSIZE];
    snprintf(aPathA, sizeof(aPathA), "%s/saveload_a.nds", sithGetSaveGamesDir());
    snprintf(aPathB, sizeof(aPathB), "%s/saveload_b.nds", sithGetSaveGamesDir());
    sithMakeDirs();

    printf("saveload: %s after %lu frames, game time %u ms\n", sithWorld_pCurrentWorld->aName, framesBefore,
        sithTime_msecGameTime);
    saveload_Capture(&saveload_before);
    if ( sithGamesave_SaveFile(aPathA) )
    {
        fprintf(stderr, "saveload: saving %s failed\n", aPathA);
        return 1;
    }
    printf("saved %s: %zu things\n", aPathA, saveload_before.numThings);

    if ( sithGamesave_RestoreFile(aPathA, /*bNotify=*/0) )
    {
        fprintf(stderr, "saveload: restoring %s failed\n", aPathA);
        return 1;
    }
    saveload_Capture(&saveload_after);
    printf("restored: %zu things\n", saveload_after.numThings);

    int failed = 0;
    int numDiffs = saveload_Compare(&saveload_before, &saveload_after);
    printf("world after restore: %s\n", numDiffs ? "DIFFERS" : "matches");
    failed |= numDiffs != 0;

    if ( sithGamesave_SaveFile(aPathB) )
    {
        fprintf(stderr, "saveload: saving %s failed\n", aPathB);
        return 1;
    }
    int filesDiffer = saveload_CompareFiles(aPathA, aPathB);
    printf("save of the restored game: %s\n", filesDiffer ? "DIFFERS" : "matches the first");
    failed |= filesDiffer;

    if ( saveload_RunFrames(framesAfter) )
    {
        return 1;
    }
    printf("ran %lu frames after the restore, game time %u ms\n", framesAfter, sithTime_msecGameTime);

    JonesMain_Shutdown();
    printf("%s\n", failed ? "FAILED" : "passed");
    return failed;
}
