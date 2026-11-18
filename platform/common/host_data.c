/*
 * The player's game data (PORT-PLAN.md P8.4, P8.5); see host_data.h.
 */
#include <copyfile.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <removefile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <CommonCrypto/CommonDigest.h>

#include <w32util/wuRegistry.h>

#include <platform/common/host_data.h>
#include <platform/common/host_dirs.h>
#include <platform/common/host_path.h>
#include <platform/common/host_product.h>

/* ---- What a release's data looks like ------------------------------------------------------------------------------- */

// The GOBs every release has, with the sizes seen. CD1.GOB differs in GOG's release; the CD's, Steam's and the copy
// DREAMM installs from a CD are the same.
static const struct
{
    const char* pName;
    long long   aSizes[2];
} hostdata_aGobs[] = {
    { "CD1.GOB",     { 353488544, 353666116 } }, // CD, Steam, DREAMM; GOG
    { "CD2.GOB",     { 438468652, 0 } },
    { "JONES3D.GOB", { 49305685,  0 } },
};

// RESOURCE/INDY3D.EXE by SHA-256 (docs/VERSIONS.md). The port runs the 1.2 engine; 1.0 and 1.1 data lack the 1.2
// update's level scripts.
static const struct
{
    const char* pSha256;
    const char* pVersion;
    int         bTested;
} hostdata_aExes[] = {
    { "55fb00c0a2cb30793ff451dc6fd17681c5128aa32419aa711015fd5117d608bf", "1.2",         1 },
    { "7faf42d593ab770c3bfbec14e0f1dbff738d3552d732e81561c78ac4054a75d4", "1.2 (Steam)", 1 },
    { "4075e655e0cf0db2d352265ba83a19a51fb373156cba8b3e43107fd6c6129ebb", "1.2 (GOG)",   1 },
    { "5567574791fd91c14874e73bfd1fc1e02967d954b55ffe6075396074db60e2c8", "1.1",         0 },
    { "3fbaf8cd401b4af80967cbe42e3420fb803288b336ebbe72a9a01b6dfd661a53", "1.0",         0 },
};

/* ---- Checking a folder ---------------------------------------------------------------------------------------------- */

// The file pName in pRoot's RESOURCE folder, any letter case. Returns 1 if it exists, with its size.
static int hostdata_StatResource(const char* pRoot, const char* pName, char* pPath, size_t size, long long* pSize)
{
    char aEnginePath[1100];
    snprintf(aEnginePath, sizeof(aEnginePath), "%s/resource/%s", pRoot, pName);
    J3DHost_ToPosixPath(pPath, size, aEnginePath);

    struct stat st;
    if ( stat(pPath, &st) != 0 || !S_ISREG(st.st_mode) )
    {
        return 0;
    }
    *pSize = (long long)st.st_size;
    return 1;
}

// SHA-256 of a file as 64 lower-case hex digits. Returns 0 if it cannot be read.
static int hostdata_HashFile(const char* pPath, char aHex[65])
{
    FILE* pFile = fopen(pPath, "rb");
    if ( !pFile )
    {
        return 0;
    }

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char aBuffer[65536];
    size_t numRead;
    while ( (numRead = fread(aBuffer, 1, sizeof(aBuffer), pFile)) > 0 )
    {
        CC_SHA256_Update(&ctx, aBuffer, (CC_LONG)numRead);
    }
    const int bError = ferror(pFile);
    fclose(pFile);
    if ( bError )
    {
        return 0;
    }

    unsigned char aDigest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(aDigest, &ctx);
    for ( int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++ )
    {
        snprintf(aHex + i * 2, 3, "%02x", aDigest[i]);
    }
    return 1;
}

int J3DHost_CheckData(const char* pRoot, J3DDataInfo* pInfo)
{
    memset(pInfo, 0, sizeof(*pInfo));
    snprintf(pInfo->aVersion, sizeof(pInfo->aVersion), "unknown");

    char aPath[1100];
    long long fileSize;
    struct stat st;
    snprintf(aPath, sizeof(aPath), "%s/resource", pRoot);
    char aResource[1100];
    J3DHost_ToPosixPath(aResource, sizeof(aResource), aPath);
    if ( !pRoot || !*pRoot || stat(aResource, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        snprintf(pInfo->aProblem, sizeof(pInfo->aProblem),
                 "There is no Resource folder in it. Choose the game's own folder, the one that holds Resource.");
        return 0;
    }

    // The demo has its one GOB, PREVIEW.GOB, and none of the game's
    if ( !hostdata_StatResource(pRoot, "JONES3D.GOB", aPath, sizeof(aPath), &fileSize)
         && hostdata_StatResource(pRoot, "PREVIEW.GOB", aPath, sizeof(aPath), &fileSize) )
    {
        snprintf(pInfo->aVersion, sizeof(pInfo->aVersion), "demo");
        snprintf(pInfo->aProblem, sizeof(pInfo->aProblem), "This is the demo, which is not supported. The full game is needed.");
        return 0;
    }

    int bKnownSizes = 1;
    for ( size_t i = 0; i < sizeof(hostdata_aGobs) / sizeof(hostdata_aGobs[0]); i++ )
    {
        if ( !hostdata_StatResource(pRoot, hostdata_aGobs[i].pName, aPath, sizeof(aPath), &fileSize) )
        {
            snprintf(pInfo->aProblem, sizeof(pInfo->aProblem),
                     "Resource has no %s. All three of CD1.GOB, CD2.GOB and JONES3D.GOB are needed in it; from the CDs, "
                     "copy CD1.GOB and CD2.GOB (in each CD's Resource folder) beside JONES3D.GOB.",
                     hostdata_aGobs[i].pName);
            return 0;
        }
        if ( fileSize != hostdata_aGobs[i].aSizes[0] && fileSize != hostdata_aGobs[i].aSizes[1] )
        {
            bKnownSizes = 0;
        }
    }
    pInfo->bUsable = 1;

    int bKnownExe = 0;
    char aHex[65];
    if ( hostdata_StatResource(pRoot, "INDY3D.EXE", aPath, sizeof(aPath), &fileSize) && hostdata_HashFile(aPath, aHex) )
    {
        for ( size_t i = 0; i < sizeof(hostdata_aExes) / sizeof(hostdata_aExes[0]); i++ )
        {
            if ( strcmp(aHex, hostdata_aExes[i].pSha256) == 0 )
            {
                snprintf(pInfo->aVersion, sizeof(pInfo->aVersion), "%s", hostdata_aExes[i].pVersion);
                bKnownExe = hostdata_aExes[i].bTested ? 1 : -1;
                break;
            }
        }
    }

    if ( bKnownExe < 0 )
    {
        snprintf(pInfo->aProblem, sizeof(pInfo->aProblem),
                 "This is version %s. %s is tested with version 1.2; LucasArts' 1.2 update brings its level fixes.",
                 pInfo->aVersion, J3D_PRODUCT_NAME);
    }
    else if ( !bKnownExe )
    {
        snprintf(pInfo->aProblem, sizeof(pInfo->aProblem),
                 "Its version is not known (Resource/Indy3D.exe is missing or not a release in docs/VERSIONS.md). "
                 "%s is tested with the English version 1.2.", J3D_PRODUCT_NAME);
    }
    else if ( !bKnownSizes )
    {
        snprintf(pInfo->aProblem, sizeof(pInfo->aProblem),
                 "Its GOB files are not the sizes of a known release (another language, or a modified copy?). %s is "
                 "tested with the English version 1.2.", J3D_PRODUCT_NAME);
    }
    pInfo->bTested = bKnownExe > 0 && bKnownSizes;
    return 1;
}

/* ---- Finding a folder ----------------------------------------------------------------------------------------------- */

#define HOSTDATA_GAME "Indiana Jones and the Infernal Machine"

// Folders the releases install to, under a Windows drive C: of CrossOver or Whisky
static const char* const hostdata_apWindowsDirs[] = {
    "Program Files (x86)/Steam/steamapps/common/" HOSTDATA_GAME,
    "Program Files/Steam/steamapps/common/" HOSTDATA_GAME,
    "GOG Games/" HOSTDATA_GAME,
    "Program Files (x86)/GOG Galaxy/Games/" HOSTDATA_GAME,
    "Program Files (x86)/GOG.com/" HOSTDATA_GAME,
    "Program Files (x86)/LucasArts/Infernal Machine",
    "Program Files/LucasArts/Infernal Machine",
};

typedef struct HostDataSearch
{
    char aTested[1024];
    char aUsable[1024];
} HostDataSearch;

static void hostdata_Try(HostDataSearch* pSearch, const char* pRoot)
{
    if ( pSearch->aTested[0] )
    {
        return;
    }

    J3DDataInfo info;
    if ( !J3DHost_CheckData(pRoot, &info) )
    {
        return;
    }
    if ( info.bTested )
    {
        snprintf(pSearch->aTested, sizeof(pSearch->aTested), "%s", pRoot);
    }
    else if ( !pSearch->aUsable[0] )
    {
        snprintf(pSearch->aUsable, sizeof(pSearch->aUsable), "%s", pRoot);
    }
}

// Every match of a glob(3) pattern (spaces need no escaping; '*' is the only wildcard used)
static void hostdata_TryPattern(HostDataSearch* pSearch, const char* pPattern)
{
    glob_t matches;
    if ( glob(pPattern, GLOB_NOSORT, NULL, &matches) == 0 )
    {
        for ( size_t i = 0; i < matches.gl_pathc; i++ )
        {
            hostdata_Try(pSearch, matches.gl_pathv[i]);
        }
    }
    globfree(&matches);
}

// Steam libraries other than the default one, from libraryfolders.vdf: its "path" values
static void hostdata_TrySteamLibraries(HostDataSearch* pSearch, const char* pHome)
{
    char aPath[1100];
    snprintf(aPath, sizeof(aPath), "%s/Library/Application Support/Steam/steamapps/libraryfolders.vdf", pHome);
    FILE* pFile = fopen(aPath, "r");
    if ( !pFile )
    {
        return;
    }

    char aLine[1100];
    while ( fgets(aLine, sizeof(aLine), pFile) )
    {
        char* pKey = strstr(aLine, "\"path\"");
        if ( !pKey )
        {
            continue;
        }
        char* pStart = strchr(pKey + 6, '"');
        char* pEnd = pStart ? strchr(pStart + 1, '"') : NULL;
        if ( !pEnd )
        {
            continue;
        }
        *pEnd = '\0';
        char aRoot[1100];
        snprintf(aRoot, sizeof(aRoot), "%s/steamapps/common/" HOSTDATA_GAME, pStart + 1);
        hostdata_Try(pSearch, aRoot);
    }
    fclose(pFile);
}

int J3DHost_FindData(char* pRoot, size_t size)
{
    const char* pHome = getenv("HOME");
    if ( !pHome || !*pHome )
    {
        return 0;
    }

    HostDataSearch search;
    memset(&search, 0, sizeof(search));
    char aPattern[1400];

    // Steam for the Mac installs the Windows release, as do GOG's offline installer and Galaxy into a folder of the
    // player's choosing; DREAMM installs the CD or a store release under its own folder, one folder per edition
    snprintf(aPattern, sizeof(aPattern), "%s/Library/Application Support/Steam/steamapps/common/" HOSTDATA_GAME, pHome);
    hostdata_Try(&search, aPattern);
    hostdata_TrySteamLibraries(&search, pHome);
    snprintf(aPattern, sizeof(aPattern), "%s/Library/Application Support/Aaron Giles/DREAMM/install/lec-infernal/*", pHome);
    hostdata_TryPattern(&search, aPattern);

    static const char* const apMacDirs[] = {
        "/Applications/" HOSTDATA_GAME,
        "%s/Applications/" HOSTDATA_GAME,
        "%s/Games/" HOSTDATA_GAME,
        "%s/GOG Games/" HOSTDATA_GAME,
        "/Applications/GOG Games/" HOSTDATA_GAME,
    };
    for ( size_t i = 0; i < sizeof(apMacDirs) / sizeof(apMacDirs[0]); i++ )
    {
        snprintf(aPattern, sizeof(aPattern), apMacDirs[i], pHome);
        hostdata_Try(&search, aPattern);
    }

    static const char* const apBottles[] = {
        "%s/Library/Application Support/CrossOver/Bottles/*/drive_c/",
        "%s/Library/Containers/com.isaacmarovitz.Whisky/Bottles/*/drive_c/",
    };
    for ( size_t i = 0; i < sizeof(apBottles) / sizeof(apBottles[0]); i++ )
    {
        for ( size_t j = 0; j < sizeof(hostdata_apWindowsDirs) / sizeof(hostdata_apWindowsDirs[0]); j++ )
        {
            char aBottle[1100];
            snprintf(aBottle, sizeof(aBottle), apBottles[i], pHome);
            snprintf(aPattern, sizeof(aPattern), "%s%s", aBottle, hostdata_apWindowsDirs[j]);
            hostdata_TryPattern(&search, aPattern);
        }
    }

    const char* pFound = search.aTested[0] ? search.aTested : search.aUsable;
    if ( !pFound[0] )
    {
        return 0;
    }
    snprintf(pRoot, size, "%s", pFound);
    return 1;
}

/* ---- A copy in the player's own folder ------------------------------------------------------------------------------ */

// The game runs from its own copy of the data, <user dir>/GameData, so it does not depend on where Steam, GOG or DREAMM
// put the game (or on that folder staying put). Copied: Resource (the GOBs, Cog, the movies, Indy3D.exe for the version
// check) and KeySets (the retail key sets, seeded from here). The store's folder is left as it was; copies are APFS
// clones where the file system allows, so on the same volume they take no space.
#define HOSTDATA_LOCALNAME "GameData"

static const char* const hostdata_apCopied[] = { "Resource", "KeySets" };

static int hostdata_IsDir(const char* pPath)
{
    struct stat st;
    return stat(pPath, &st) == 0 && S_ISDIR(st.st_mode);
}

static void hostdata_Remove(const char* pPath)
{
    if ( hostdata_IsDir(pPath) )
    {
        removefile(pPath, NULL, REMOVEFILE_RECURSIVE);
    }
}

// The folder in pRoot named pName in any letter case, as the retail installs vary (RESOURCE, Resource, KEYSETS)
static int hostdata_FindChild(const char* pRoot, const char* pName, char* pPath, size_t size)
{
    char aEnginePath[1100];
    snprintf(aEnginePath, sizeof(aEnginePath), "%s/%s", pRoot, pName);
    J3DHost_ToPosixPath(pPath, size, aEnginePath);
    return hostdata_IsDir(pPath);
}

// Copies pRoot's data into <user dir>/GameData and returns 1 with its path in pLocal, or 0 to keep using pRoot (already
// the copy, J3D_NO_DATA_COPY set, or the copy failed). Copied into GameData.partial first and checked, so a copy cut
// short is never used; a GameData from an earlier choice is replaced only once the new copy is whole.
static int hostdata_MakeLocal(const char* pRoot, char* pLocal, size_t size)
{
    const char* pNoCopy = getenv("J3D_NO_DATA_COPY");
    if ( pNoCopy && *pNoCopy && strcmp(pNoCopy, "0") != 0 )
    {
        return 0;
    }

    char aLocal[1100], aPartial[1100], aOld[1100];
    snprintf(aLocal, sizeof(aLocal), "%s" HOSTDATA_LOCALNAME, J3DHost_GetUserDir());
    snprintf(aPartial, sizeof(aPartial), "%s.partial", aLocal);
    snprintf(aOld, sizeof(aOld), "%s.old", aLocal);

    char aRealRoot[PATH_MAX], aRealLocal[PATH_MAX];
    if ( realpath(pRoot, aRealRoot) && realpath(aLocal, aRealLocal) && strcmp(aRealRoot, aRealLocal) == 0 )
    {
        return 0; // already the copy
    }

    if ( J3DHost_MakeDirs(J3DHost_GetUserDir()) )
    {
        return 0;
    }

    hostdata_Remove(aPartial); // a copy an earlier run did not finish
    if ( mkdir(aPartial, 0755) != 0 )
    {
        fprintf(stderr, J3D_PRODUCT_NAME ": could not create %s: %s\n", aPartial, strerror(errno));
        return 0;
    }

    printf("Game data: copying %s to %s\n", pRoot, aLocal);
    fflush(stdout);
    for ( size_t i = 0; i < sizeof(hostdata_apCopied) / sizeof(hostdata_apCopied[0]); i++ )
    {
        char aFrom[1100], aTo[1200];
        if ( !hostdata_FindChild(pRoot, hostdata_apCopied[i], aFrom, sizeof(aFrom)) )
        {
            continue; // KeySets is optional; CheckData below finds a missing Resource
        }

        snprintf(aTo, sizeof(aTo), "%s/%s", aPartial, hostdata_apCopied[i]);
        if ( copyfile(aFrom, aTo, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_CLONE) != 0 )
        {
            fprintf(stderr, J3D_PRODUCT_NAME ": could not copy %s to %s: %s; the game runs from %s\n", aFrom, aTo,
                    strerror(errno), pRoot);
            hostdata_Remove(aPartial);
            return 0;
        }
    }

    J3DDataInfo info;
    if ( !J3DHost_CheckData(aPartial, &info) )
    {
        fprintf(stderr, J3D_PRODUCT_NAME ": the copy in %s cannot be used (%s); the game runs from %s\n", aPartial,
                info.aProblem, pRoot);
        hostdata_Remove(aPartial);
        return 0;
    }

    hostdata_Remove(aOld);
    if ( hostdata_IsDir(aLocal) && rename(aLocal, aOld) != 0 )
    {
        fprintf(stderr, J3D_PRODUCT_NAME ": could not replace %s: %s\n", aLocal, strerror(errno));
        hostdata_Remove(aPartial);
        return 0;
    }
    if ( rename(aPartial, aLocal) != 0 )
    {
        fprintf(stderr, J3D_PRODUCT_NAME ": could not move %s to %s: %s\n", aPartial, aLocal, strerror(errno));
        rename(aOld, aLocal);
        hostdata_Remove(aPartial);
        return 0;
    }
    hostdata_Remove(aOld);

    wuRegistry_SaveStr("Data Source", pRoot); // where it came from, for the player's information
    snprintf(pLocal, size, "%s", aLocal);
    return 1;
}

/* ---- Making sure there is one --------------------------------------------------------------------------------------- */

static char hostdata_aVersion[64];

const char* J3DHost_GetDataVersion(void)
{
    return hostdata_aVersion;
}

static void hostdata_Use(const char* pRoot, const J3DDataInfo* pInfo, int bStore)
{
    char aLocal[1100];
    J3DDataInfo localInfo;
    if ( hostdata_MakeLocal(pRoot, aLocal, sizeof(aLocal)) && J3DHost_CheckData(aLocal, &localInfo) )
    {
        pRoot  = aLocal;
        pInfo  = &localInfo;
        bStore = 1;
    }

    if ( bStore )
    {
        wuRegistry_SaveStr("Data Root", pRoot);
    }
    snprintf(hostdata_aVersion, sizeof(hostdata_aVersion), "%s", pInfo->aVersion);
    printf("Game data: %s, version %s\n", pRoot, pInfo->aVersion);
    if ( pInfo->aProblem[0] )
    {
        printf("Game data: %s\n", pInfo->aProblem);
    }
    fflush(stdout);
}

int J3DHost_EnsureData(J3DHostAskData pfAsk, int bChooseAgain)
{
    J3DDataInfo info;
    char aRoot[1024];

    // Given on the command line: used as it is, as the tests and tools give it
    const char* pGiven = getenv("J3D_DATA");
    if ( pGiven && *pGiven )
    {
        if ( J3DHost_CheckData(pGiven, &info) )
        {
            snprintf(hostdata_aVersion, sizeof(hostdata_aVersion), "%s", info.aVersion);
        }
        else
        {
            fprintf(stderr, J3D_PRODUCT_NAME ": %s: %s\n", pGiven, info.aProblem);
        }
        return 0;
    }

    const char* pStored = J3DHost_GetDataRoot();
    const char* pProblem = NULL;
    if ( pStored[0] )
    {
        if ( J3DHost_CheckData(pStored, &info) && !bChooseAgain )
        {
            hostdata_Use(pStored, &info, 0);
            return 0;
        }
        if ( !info.bUsable )
        {
            fprintf(stderr, J3D_PRODUCT_NAME ": the game data folder %s cannot be used: %s\n", pStored, info.aProblem);
            pProblem = info.aProblem;
        }
    }

    if ( !bChooseAgain && J3DHost_FindData(aRoot, sizeof(aRoot)) && J3DHost_CheckData(aRoot, &info) )
    {
        hostdata_Use(aRoot, &info, 1);
        return 0;
    }

    if ( !pfAsk )
    {
        fprintf(stderr, J3D_PRODUCT_NAME ": no game data found. Give the folder that holds Resource with --data <folder>.\n");
        return 0;
    }

    char aProblem[sizeof(info.aProblem) + 1100];
    if ( pProblem )
    {
        snprintf(aProblem, sizeof(aProblem), "%s", pProblem);
        pProblem = aProblem;
    }
    for ( ;; )
    {
        if ( !pfAsk(pProblem, aRoot, sizeof(aRoot)) )
        {
            // Declined: a usable stored folder is kept when the player only meant to look at another
            if ( pStored[0] && J3DHost_CheckData(pStored, &info) )
            {
                hostdata_Use(pStored, &info, 0);
                return 0;
            }
            printf("Game data: none chosen, quitting\n");
            return 1;
        }
        if ( J3DHost_CheckData(aRoot, &info) )
        {
            hostdata_Use(aRoot, &info, 1);
            return 0;
        }
        snprintf(aProblem, sizeof(aProblem), "%s cannot be used. %s", aRoot, info.aProblem);
        pProblem = aProblem;
    }
}
