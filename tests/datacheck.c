/*
 * datacheck: the game data check (PORT-PLAN.md P8.5, platform/common/host_data.c). Needs no game data; with a data
 * folder as its argument it also checks that folder is a tested release.
 *
 *   - a folder with no RESOURCE, the demo's (PREVIEW.GOB only) and one missing a GOB are refused, with a reason
 *   - the three GOBs at a known release's sizes, in any letter case, are usable; without INDY3D.EXE the version is
 *     unknown and the set untested
 *   - a GOB of another size is usable but untested
 *
 *   datacheck [<data-root>]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <platform/common/host_data.h>

static int datacheck_numFailed = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !(cond) )                                                                                                 \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++datacheck_numFailed;                                                                                     \
        }                                                                                                              \
    } while ( 0 )

// A sparse file of the given size
static int datacheck_MakeFile(const char* pDir, const char* pName, off_t size)
{
    char aPath[1100];
    snprintf(aPath, sizeof(aPath), "%s/%s", pDir, pName);
    FILE* pFile = fopen(aPath, "wb");
    if ( !pFile )
    {
        return 0;
    }
    fclose(pFile);
    return truncate(aPath, size) == 0;
}

int main(int argc, char** argv)
{
    const char* pTmp = getenv("TMPDIR");
    char aTemplate[1100];
    snprintf(aTemplate, sizeof(aTemplate), "%s/datacheck.XXXXXX", pTmp && *pTmp ? pTmp : "/tmp");
    char* pRoot = mkdtemp(aTemplate);
    if ( !pRoot )
    {
        perror("mkdtemp");
        return 1;
    }

    J3DDataInfo info;
    char aResource[1200];

    // No RESOURCE
    CHECK(!J3DHost_CheckData(pRoot, &info));
    CHECK(strstr(info.aProblem, "Resource") != NULL);

    // The demo
    snprintf(aResource, sizeof(aResource), "%s/RESOURCE", pRoot);
    CHECK(mkdir(aResource, 0755) == 0);
    CHECK(datacheck_MakeFile(aResource, "preview.gob", 66727866));
    CHECK(!J3DHost_CheckData(pRoot, &info));
    CHECK(strcmp(info.aVersion, "demo") == 0);

    // A GOB missing
    CHECK(datacheck_MakeFile(aResource, "Jones3D.GOB", 49305685));
    CHECK(datacheck_MakeFile(aResource, "cd1.gob", 353488544));
    CHECK(!J3DHost_CheckData(pRoot, &info));
    CHECK(strstr(info.aProblem, "CD2.GOB") != NULL);

    // All three, at the sizes of a release, but no INDY3D.EXE: usable, version unknown
    CHECK(datacheck_MakeFile(aResource, "CD2.gob", 438468652));
    CHECK(J3DHost_CheckData(pRoot, &info));
    CHECK(!info.bTested);
    CHECK(strcmp(info.aVersion, "unknown") == 0);
    CHECK(info.aProblem[0] != '\0');

    // GOG's CD1.GOB size is known too; an INDY3D.EXE that is not a release leaves the version unknown
    CHECK(datacheck_MakeFile(aResource, "cd1.gob", 353666116));
    CHECK(datacheck_MakeFile(aResource, "Indy3D.exe", 1000));
    CHECK(J3DHost_CheckData(pRoot, &info));
    CHECK(!info.bTested && strcmp(info.aVersion, "unknown") == 0);

    // A GOB of another size
    CHECK(datacheck_MakeFile(aResource, "cd1.gob", 1234));
    CHECK(J3DHost_CheckData(pRoot, &info));
    CHECK(!info.bTested);

    char aCommand[1300];
    snprintf(aCommand, sizeof(aCommand), "rm -rf '%s'", pRoot);
    if ( system(aCommand) != 0 )
    {
        fprintf(stderr, "datacheck: could not remove %s\n", pRoot);
    }

    // The player's data: a tested release
    if ( argc > 1 )
    {
        CHECK(J3DHost_CheckData(argv[1], &info));
        CHECK(info.bTested);
        printf("datacheck: %s is version %s%s%s\n", argv[1], info.aVersion, info.aProblem[0] ? ": " : "", info.aProblem);
    }

    if ( datacheck_numFailed )
    {
        fprintf(stderr, "datacheck: %d checks failed\n", datacheck_numFailed);
        return 1;
    }
    printf("datacheck: all checks passed\n");
    return 0;
}
