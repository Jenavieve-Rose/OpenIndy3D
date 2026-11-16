/*
 * gamedialogs: the game's own dialogs, opened through their real entry points (PORT-PLAN.md P6.11 to P6.22), in the
 * Win32 form they keep for runs with no platform menu (decision 21).
 *
 *   gamedialogs <data root>
 *
 * Starts the game on the player's data, headless, as jones3d --headless does (the null platform, as saveload), runs the first level for a few frames,
 * then opens every dialog the game can show from its jonesConfig entry point, over the running level: each runs its own dialog procedure (the JONES_STR_ strings from the game's string table,
 * the game's own rescaling and font, its icons). A test hook (J3DGui_SetModalHook) checks that the dialog came up with
 * its strings resolved (no control text still starting "JONES_STR_"), optionally saves it, and cancels it, so nested
 * dialogs and confirmation boxes are reached and cancelled the same way.
 *
 * GAMEDIALOGS_DUMP=<dir> writes every dialog as a BMP.
 */
#include <platform/common/j3dgui/gui_internal.h>
#include <platform/common/j3dgui/j3dgui.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <sith/Dss/sithGamesave.h>
#include <sith/types.h>

#include <platform/common/host_dirs.h>
#include <std/Win95/stdWin95.h>

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void J3DAPI JonesDevDialog_LoadSettings(StdDisplayEnvironment* pDisplayEnv, JonesState* pConfig); // JonesDevDialog.c

static int gamedialogs_failures = 0;
static int gamedialogs_seen = 0;
static const char* gamedialogs_pDumpDir = NULL;
static const char* gamedialogs_pName = "";

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            gamedialogs_failures++;                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static void gamedialogs_WriteBmp(J3DGuiWindow* p, const char* pName)
{
    char aPath[1024];
    FILE* f;
    J3DGuiSurface* s = p->pSurface;
    unsigned char aHeader[54] = { 'B', 'M' };
    uint32_t fileSize;
    int h;
    int y;
    if ( !gamedialogs_pDumpDir || !s )
    {
        return;
    }
    J3DGui_Paint(p);
    snprintf(aPath, sizeof(aPath), "%s/%s.bmp", gamedialogs_pDumpDir, pName);
    f = fopen(aPath, "wb");
    if ( !f )
    {
        return;
    }
    fileSize = 54u + (uint32_t)s->width * 4u * (uint32_t)s->height;
    memcpy(aHeader + 2, &fileSize, 4);
    aHeader[10] = 54;
    aHeader[14] = 40;
    memcpy(aHeader + 18, &s->width, 4);
    h = -s->height;
    memcpy(aHeader + 22, &h, 4);
    aHeader[26] = 1;
    aHeader[28] = 32;
    fwrite(aHeader, 1, sizeof(aHeader), f);
    for ( y = 0; y < s->height; ++y )
    {
        fwrite(s->pPixels + (size_t)y * (size_t)s->pitch, 4, (size_t)s->width, f);
    }
    fclose(f);
}

static void gamedialogs_Hook(HWND hDlg, int iteration)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hDlg);
    J3DGuiWindow* q;
    char aName[128];
    if ( iteration > 0 )
    {
        // still open after the cancel below: end it
        EndDialog(hDlg, IDCANCEL);
        return;
    }
    gamedialogs_seen++;
    CHECK(strncmp(p->pText, "JONES_STR_", 10) != 0, "%s: the caption is still %s", gamedialogs_pName, p->pText);
    for ( q = p->pFirstChild; q; q = q->pNext )
    {
        CHECK(strncmp(q->pText, "JONES_STR_", 10) != 0, "%s: control %d still reads %s", gamedialogs_pName, q->id, q->pText);
    }
    snprintf(aName, sizeof(aName), "%s-%d", gamedialogs_pName, gamedialogs_seen);
    gamedialogs_WriteBmp(p, aName);
    // Cancel as a player would: Escape (the dialog's IDCANCEL), then the hook ends anything still open
    J3DGui_Input(WM_KEYDOWN, VK_ESCAPE, 1);
    J3DGui_Input(WM_KEYUP, VK_ESCAPE, (LPARAM)(1u | (3u << 30)));
}

#define SHOW(name, call)                                                                                               \
    do {                                                                                                               \
        int before = gamedialogs_seen;                                                                                 \
        gamedialogs_pName = name;                                                                                      \
        call;                                                                                                          \
        CHECK(gamedialogs_seen > before, "%s: no dialog came up", name);                                               \
    } while (0)

int main(int argc, char** argv)
{
    SithGameStatistics stats;
    char aPath[256];
    int balance = 1400;
    int aItems[64];
    tSoundChannelHandle hChannel = 0;
    JonesSoundSettings sound;

    static const GUID guid = { 0x82CE4DA0, 0x9CBF, 0x11D1, { 0x90, 0x85, 0x00, 0x60, 0x97, 0x76, 0xEA, 0x02 } };
    int frame;

    harness_InstallHandlers();
    if ( argc < 2 )
    {
        fprintf(stderr, "usage: gamedialogs <data root>\n");
        return 2;
    }
    // What main.c does for `jones3d --data <root> --headless`; the level starts without the launcher.
    setenv("J3D_DATA", argv[1], 1);
    setenv("J3D_HEADLESS", "1", 1);
    setenv("J3D_REG_Start_Mode", "0", 1);
    if ( !getenv("J3D_SETTINGS") )
    {
        setenv("J3D_SETTINGS", "/dev/null", 1);
    }
    if ( !getenv("J3D_REG_Install_Path") || J3DHost_MakeDirs(getenv("J3D_REG_Install_Path")) )
    {
        fprintf(stderr, "gamedialogs: set J3D_REG_Install_Path to a folder the test may write to\n");
        return 2;
    }
    stdWin95_SetWindow(NULL);
    stdWin95_SetInstance(NULL);
    stdWin95_SetGuid(&guid);
    if ( JonesMain_Startup("") )
    {
        printf("gamedialogs: the game did not start\n");
        return 1;
    }
    for ( frame = 0; frame < 60; ++frame )
    {
        if ( JonesMain_Process() )
        {
            printf("gamedialogs: the game stopped after %d frames\n", frame);
            return 1;
        }
    }
    gamedialogs_pDumpDir = getenv("GAMEDIALOGS_DUMP");
    J3DGui_SetModalHook(gamedialogs_Hook);

    memset(&stats, 0, sizeof(stats));
    memset(aItems, 0, sizeof(aItems));
    memset(&sound, 0, sizeof(sound));
    sound.maxSoundVolume = 1.0f;

    SHOW("message", jonesConfig_ShowMessageDialog(stdWin95_GetWindow(), "JONES_STR_SAVEGM", "The game could not be saved.", 145)); // the text is literal

    SHOW("exit", jonesConfig_ShowExitGameDialog(stdWin95_GetWindow(), aPath));
    SHOW("gameplay", jonesConfig_ShowGamePlayOptions(NULL));
    SHOW("sound", jonesConfig_ShowSoundSettingsDialog(stdWin95_GetWindow(), &sound));
    SHOW("display", jonesConfig_ShowDisplaySettingsDialog(stdWin95_GetWindow(), JonesMain_GetDisplayEnvironment(), JonesMain_GetDisplaySettings()));
    SHOW("controls", jonesConfig_ShowControlOptions(NULL));
    SHOW("statistics", jonesConfig_ShowStatisticsDialog(stdWin95_GetWindow(), &stats));
    {
        // The game shows this once a level is done, when the statistics already count it (JonesHud.c reads the
        // finished level as curLevelNum - 1)
        SithGameStatistics* pGameStats = sithGamesave_GetGameStatistics();
        int curLevelNum = pGameStats ? pGameStats->curLevelNum : 0;
        if ( pGameStats )
        {
            pGameStats->curLevelNum = 1;
        }
        SHOW("levelcompleted", jonesConfig_ShowLevelCompletedDialog(stdWin95_GetWindow(), &balance, aItems, 0, 125, 10, 2, 300, 1000));
        if ( pGameStats )
        {
            pGameStats->curLevelNum = curLevelNum;
        }
    }
    SHOW("gameover", jonesConfig_ShowGameOverDialog(stdWin95_GetWindow(), aPath, 0, &hChannel));
    SHOW("insertcd", jonesConfig_ShowDialogInsertCD(stdWin95_GetWindow(), 1));
    {
        // The developer dialog (101) that Start Mode 2 opens before the game window exists; it stays a Win32 dialog
        // (P6.13). Its settings as JonesMain_Startup loads them.
        static JonesState devState;
        memset(&devState, 0, sizeof(devState));
        JonesDevDialog_LoadSettings(JonesMain_GetDisplayEnvironment(), &devState);
        SHOW("developer", JonesDevDialog_ShowDevDialog(stdWin95_GetWindow(), &devState));
    }
    // Saving and loading have no Win32 dialog to fall back to: the platform's menu is their file picker (decision 21)
    // and GetSaveFileName/GetOpenFileName are not emulated, so where there is no menu (this headless run) they cancel.
    {
        int before = gamedialogs_seen;
        int result;
        aPath[0] = 0;
        result = jonesConfig_GetSaveGameFilePath(stdWin95_GetWindow(), aPath);
        CHECK(result != 1 && aPath[0] == 0, "save: returned %d with \"%s\" where it should cancel", result, aPath);
        result = jonesConfig_GetLoadGameFilePath(stdWin95_GetWindow(), aPath);
        CHECK(result != 1 && aPath[0] == 0, "load: returned %d with \"%s\" where it should cancel", result, aPath);
        CHECK(gamedialogs_seen == before, "save and load: a dialog came up");
    }

    J3DGui_SetModalHook(NULL);
    JonesMain_Shutdown();
    if ( gamedialogs_failures )
    {
        printf("gamedialogs: %d check(s) failed\n", gamedialogs_failures);
        return 1;
    }
    printf("gamedialogs: %d dialogs came up with their strings and were cancelled\n", gamedialogs_seen);
    return 0;
}
