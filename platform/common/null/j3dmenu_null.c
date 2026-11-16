/*
 * Null j3dmenu: no platform menus, so every jonesConfig entry point goes on to its Win32 dialog (j3dgui). Written by
 * hand, not by tools/gen_null_platform.py. platform/apple/j3dmenu_apple.c replaces it.
 */
#include <platform/common/j3dmenu.h>

int J3DMenu_IsAvailable(void)
{
    return 0;
}

int J3DMenu_ShowMessage(const char* pTitle, const char* pText, int iconID, int* pResult)
{
    (void)pTitle;
    (void)pText;
    (void)iconID;
    (void)pResult;
    return 0;
}

int J3DMenu_ShowExitGame(char* pSaveGameFilePath, int* pResult)
{
    (void)pSaveGameFilePath;
    (void)pResult;
    return 0;
}

int J3DMenu_GetSaveGameFilePath(char* pOutFilePath, int* pResult)
{
    (void)pOutFilePath;
    (void)pResult;
    return 0;
}

int J3DMenu_GetLoadGameFilePath(char* pDestNdsPath, int* pResult)
{
    (void)pDestNdsPath;
    (void)pResult;
    return 0;
}

int J3DMenu_ShowGamePlayOptions(J3DMenuGamePlay* pOptions, int* pResult)
{
    (void)pOptions;
    (void)pResult;
    return 0;
}

int J3DMenu_ShowSoundSettings(J3DMenuSound* pSettings, int* pResult)
{
    (void)pSettings;
    (void)pResult;
    return 0;
}

int J3DMenu_ShowDisplaySettings(J3DMenuDisplay* pSettings, int* pResult)
{
    (void)pSettings;
    (void)pResult;
    return 0;
}

int J3DMenu_ShowGameOver(int bCanRestart, int* pResult)
{
    (void)bCanRestart;
    (void)pResult;
    return 0;
}

int J3DMenu_ShowFigures(const char* pTitle, const J3DMenuFigurePage* pPages, int numPages, int startPage,
                        J3DMenuDrawPicture pfDrawPicture, int* pResult)
{
    (void)pTitle;
    (void)pPages;
    (void)numPages;
    (void)startPage;
    (void)pfDrawPicture;
    (void)pResult;
    return 0;
}

void J3DMenu_AfterFrame(unsigned long numFrames)
{
    (void)numFrames;
}
