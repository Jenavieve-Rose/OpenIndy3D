/*
 * The port's per-user folders; see host_dirs.h.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <w32util/wuRegistry.h>

#include <platform/common/host_dirs.h>
#include <platform/common/host_path.h>

#define HOSTDIRS_APPNAME "J3DPort"
#define HOSTDIRS_KEYSETS "KeySets" // SITHSTRING_KEYSETS in misc\sithstrings.uni

static const char* hostdirs_GetHome(void)
{
    const char* pHome = getenv("HOME");
    return pHome && *pHome ? pHome : ".";
}

const char* J3DHost_GetUserDir(void)
{
    static char aDir[1024];
    if ( !aDir[0] )
    {
        snprintf(aDir, sizeof(aDir), "%s/Library/Application Support/" HOSTDIRS_APPNAME "/", hostdirs_GetHome());
    }
    return aDir;
}

const char* J3DHost_GetLogDir(void)
{
    static char aDir[1024];
    if ( !aDir[0] )
    {
        const char* pOverride = getenv("J3D_LOGS");
        if ( pOverride && *pOverride )
        {
            size_t len = strlen(pOverride);
            snprintf(aDir, sizeof(aDir), "%s%s", pOverride, pOverride[len - 1] == '/' ? "" : "/");
        }
        else
        {
            snprintf(aDir, sizeof(aDir), "%s/Library/Logs/" HOSTDIRS_APPNAME "/", hostdirs_GetHome());
        }
        J3DHost_MakeDirs(aDir);
    }
    return aDir;
}

int J3DHost_MakeDirs(const char* pPath)
{
    char aDir[1024];
    J3DHost_ToPosixPath(aDir, sizeof(aDir), pPath);

    size_t len = strlen(aDir);
    if ( !len )
    {
        return 1;
    }

    // Every prefix that ends at a '/', then the whole path.
    for ( char* p = aDir + 1; *p; ++p )
    {
        if ( *p == '/' )
        {
            *p = '\0';
            if ( mkdir(aDir, 0755) != 0 && errno != EEXIST )
            {
                return 1;
            }
            *p = '/';
        }
    }

    if ( aDir[len - 1] != '/' && mkdir(aDir, 0755) != 0 && errno != EEXIST )
    {
        return 1;
    }
    return 0;
}

static int hostdirs_CopyFile(const char* pSrc, const char* pDst)
{
    FILE* pIn = fopen(pSrc, "rb");
    if ( !pIn )
    {
        return 1;
    }

    FILE* pOut = fopen(pDst, "wb");
    if ( !pOut )
    {
        fclose(pIn);
        return 1;
    }

    char aBuffer[16384];
    size_t numRead;
    int result = 0;
    while ( (numRead = fread(aBuffer, 1, sizeof(aBuffer), pIn)) > 0 )
    {
        if ( fwrite(aBuffer, 1, numRead, pOut) != numRead )
        {
            result = 1;
            break;
        }
    }

    fclose(pIn);
    if ( fclose(pOut) != 0 )
    {
        result = 1;
    }
    return result;
}

// Copies <pFromRoot>/KeySets/*.kfg into <pToRoot>/KeySets/ when that folder does not exist yet. A folder that exists
// is left alone, even if empty, so keysets the player deletes stay deleted.
static void hostdirs_SeedKeysets(const char* pFromRoot, const char* pToRoot)
{
    char aFrom[1024], aTo[1024], aPath[1024];

    snprintf(aPath, sizeof(aPath), "%s/" HOSTDIRS_KEYSETS, pToRoot);
    J3DHost_ToPosixPath(aTo, sizeof(aTo), aPath);

    snprintf(aPath, sizeof(aPath), "%s/" HOSTDIRS_KEYSETS, pFromRoot);
    J3DHost_ToPosixPath(aFrom, sizeof(aFrom), aPath); // the retail folder is KEYSETS; matched case-insensitively

    struct stat st;
    if ( stat(aTo, &st) == 0 || stat(aFrom, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        return;
    }

    char aRealFrom[PATH_MAX], aRealTo[PATH_MAX];
    if ( J3DHost_MakeDirs(aTo) || !realpath(aFrom, aRealFrom) || !realpath(aTo, aRealTo) || strcmp(aRealFrom, aRealTo) == 0 )
    {
        return; // could not create it, or it is the same folder
    }

    DIR* pDir = opendir(aFrom);
    if ( !pDir )
    {
        return;
    }

    const struct dirent* pEntry;
    while ( (pEntry = readdir(pDir)) != NULL )
    {
        size_t len = strlen(pEntry->d_name);
        if ( len < 5 || strcasecmp(pEntry->d_name + len - 4, ".kfg") != 0 )
        {
            continue;
        }

        char aSrc[2048], aDst[2048];
        snprintf(aSrc, sizeof(aSrc), "%s/%s", aFrom, pEntry->d_name);
        snprintf(aDst, sizeof(aDst), "%s/%s", aTo, pEntry->d_name);
        if ( hostdirs_CopyFile(aSrc, aDst) )
        {
            fprintf(stderr, "J3DPort: could not copy keyset %s to %s\n", aSrc, aDst);
        }
    }
    closedir(pDir);
}

void J3DHost_PrepareUserDirs(void)
{
    wuRegistry_Startup(NULL, NULL); // no-op once started; JonesMain_Startup's own call then finds it started

    char aInstallPath[1024];
    if ( wuRegistry_GetStr("Install Path", aInstallPath, sizeof(aInstallPath), NULL) || !aInstallPath[0] )
    {
        return;
    }

    if ( J3DHost_MakeDirs(aInstallPath) )
    {
        fprintf(stderr, "J3DPort: could not create %s; saves will fail\n", aInstallPath);
        return;
    }

    // The retail keysets live in the install folder, which for the port is the data root ("User Path").
    char aDataRoot[1024];
    if ( !wuRegistry_GetStr("User Path", aDataRoot, sizeof(aDataRoot), NULL) && aDataRoot[0] )
    {
        hostdirs_SeedKeysets(aDataRoot, aInstallPath);
    }
}
