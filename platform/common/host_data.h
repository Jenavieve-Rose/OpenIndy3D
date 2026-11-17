#ifndef J3D_PLATFORM_HOST_DATA_H
#define J3D_PLATFORM_HOST_DATA_H
/*
 * The player's game data (PORT-PLAN.md P8.4, P8.5): checking a folder, finding one, and asking for one.
 *
 * A data folder is the folder that holds RESOURCE/ (any letter case) with the three GOBs, CD1.GOB, CD2.GOB and
 * JONES3D.GOB, all in that one folder: a Steam or GOG install, DREAMM's copy, or a CD install with the two CD GOBs
 * copied beside JONES3D.GOB. Its RESOURCE/INDY3D.EXE names the version (docs/VERSIONS.md). The port runs the 1.2
 * engine; it is tested with 1.2 data, as Steam, GOG and the LucasArts 1.2 update have it.
 */
#include <stddef.h>

typedef struct J3DDataInfo
{
    int  bUsable;           // the three GOBs are there
    int  bTested;           // and they and INDY3D.EXE are a known 1.2 set
    char aVersion[64];      // "1.2 (Steam)", "1.2 (GOG)", "1.2", "1.0", "1.1", or "unknown"
    char aProblem[320];     // why it cannot be used, or what is untested about it; "" when it is a tested set
} J3DDataInfo;

// Checks the folder pRoot. Returns info->bUsable.
int J3DHost_CheckData(const char* pRoot, J3DDataInfo* pInfo);

// Looks in the places the Steam, GOG and DREAMM releases install to, on the Mac and in CrossOver and Whisky bottles.
// Writes the first usable folder to pRoot (a tested one before an untested one) and returns 1, or returns 0.
int J3DHost_FindData(char* pRoot, size_t size);

// Asks the player for a data folder: pProblem says why (NULL the first time). Writes the folder chosen to pRoot and
// returns 1, or returns 0 when the player declines.
typedef int (*J3DHostAskData)(const char* pProblem, char* pRoot, size_t size);

// Makes sure "Data Root" names a usable folder before the game starts. J3D_DATA (--data) is taken as given. Otherwise
// a stored folder that is usable is kept unless bChooseAgain; then the usual places are searched; then pfAsk, if
// given, until the player chooses a usable folder or declines. A folder found or chosen is stored in the settings.
// Returns 0 to go on, 1 when the player declined (quit). Needs the settings module started.
int J3DHost_EnsureData(J3DHostAskData pfAsk, int bChooseAgain);

// The version of the current data root as J3DHost_CheckData names it, or "" when there is none. For the launcher.
const char* J3DHost_GetDataVersion(void);

#endif
