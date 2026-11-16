#ifndef J3D_PLATFORM_J3DMENU_H
#define J3D_PLATFORM_J3DMENU_H
/*
 * j3dmenu: the game's menus drawn by the platform instead of by the Win32 dialog code (PORT-PLAN.md Phase 6,
 * decision 20). On macOS they are SwiftUI views laid over the game window, in the launcher's look
 * (platform/apple/j3dmenu_apple.c, platform/apple/menu/).
 *
 * Each function stands for one jonesConfig entry point, which calls it first. It returns nonzero when it showed the
 * menu, with what the entry point would have returned in *pResult; zero when it did not (a headless run, a replay, no
 * window yet, the null platform, J3D_MENUS=classic), and the entry point goes on to its Win32 dialog.
 *
 * Threads: the game thread, which is the main thread.
 */

#ifdef __cplusplus
extern "C" {
#endif

// jonesConfig_ShowMessageDialog. pTitle is a string-table name or the title itself.
int J3DMenu_ShowMessage(const char* pTitle, const char* pText, int iconID, int* pResult);

// jonesConfig_ShowExitGameDialog: 1 quit, 2 back to the game, 1187 save first (pSaveGameFilePath filled).
int J3DMenu_ShowExitGame(char* pSaveGameFilePath, int* pResult);

// jonesConfig_GetSaveGameFilePath and jonesConfig_GetLoadGameFilePath: 1 with the path (JONESCONFIG_GAMESAVE_FILEPATHSIZE
// bytes), else 2.
int J3DMenu_GetSaveGameFilePath(char* pOutFilePath, int* pResult);
int J3DMenu_GetLoadGameFilePath(char* pDestNdsPath, int* pResult);

// For tests: called after each pass of the game loop with the number of passes so far. With J3D_MENU_TEST=<n> in the
// environment, at pass n every menu is opened once through the functions above and what each returned is printed
// (run it with J3D_MENU_AUTOCANCEL, platform/apple/menu/MenuHost.swift, so nobody has to answer).
void J3DMenu_AfterFrame(unsigned long numFrames);

#ifdef __cplusplus
}
#endif

#endif
