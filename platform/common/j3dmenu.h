#ifndef J3D_PLATFORM_J3DMENU_H
#define J3D_PLATFORM_J3DMENU_H
/*
 * j3dmenu: the game's menus drawn by the platform instead of by the Win32 dialog code (PORT-PLAN.md Phase 6,
 * decisions 20 and 21). On macOS they are SwiftUI views laid over the game window, in the launcher's look
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

// Can the platform show its menus in this run? For an entry point that must prepare before it knows (the game over
// menu plays music first).
int J3DMenu_IsAvailable(void);

// A question with Yes and No, as the game's confirmations (for example template 212): *pResult 1 for Yes, 2 for No.
int J3DMenu_Ask(const char* pTitle, const char* pText, int* pResult);

// jonesConfig_ShowDialogInsertCD (template 167): pText with OK (*pResult 1) and Quit (2).
int J3DMenu_ShowInsertCD(const char* pText, int* pResult);

// jonesConfig_ShowMessageDialog. pTitle is a string-table name or the title itself.
int J3DMenu_ShowMessage(const char* pTitle, const char* pText, int iconID, int* pResult);

// jonesConfig_ShowExitGameDialog: 1 quit, 2 back to the game, 1187 save first (pSaveGameFilePath filled).
int J3DMenu_ShowExitGame(char* pSaveGameFilePath, int* pResult);

// jonesConfig_GetSaveGameFilePath and jonesConfig_GetLoadGameFilePath: 1 with the path (JONESCONFIG_GAMESAVE_FILEPATHSIZE
// bytes), else 2.
int J3DMenu_GetSaveGameFilePath(char* pOutFilePath, int* pResult);
int J3DMenu_GetLoadGameFilePath(char* pDestNdsPath, int* pResult);

// jonesConfig_ShowGamePlayOptions. The caller fills the settings in and, when *pResult is 1 (OK), applies them as
// the dialog's OK did; 2 is Cancel. difficulty runs from 0 to maxDifficulty (JONES_STR_HARD0 to 5).
typedef struct J3DMenuGamePlay
{
    int difficulty;
    int maxDifficulty;
    int bShowHints;
    int bMapRotation;
    int bDefaultRun;
    int bShowText;
} J3DMenuGamePlay;
int J3DMenu_ShowGamePlayOptions(J3DMenuGamePlay* pOptions, int* pResult);

// jonesConfig_ShowSoundSettingsDialog: volume 0 to 100. b3DAvailable: can 3D sound be chosen. pfTestVolume is called
// with the volume the player let the slider go at, so it can be heard. *pResult 1 (OK, the caller applies the
// settings) or 2.
typedef struct J3DMenuSound
{
    int volume;
    int b3DSound;
    int b3DAvailable;
    void (*pfTestVolume)(int volume);
} J3DMenuSound;
int J3DMenu_ShowSoundSettings(J3DMenuSound* pSettings, int* pResult);

// jonesConfig_ShowDisplaySettingsDialog and its advanced settings, as one menu: the resolution (one of apResolutions),
// the performance level (0 average, 1 fast, 2 very fast), the mip-map filter (0 none, 1 bilinear, 2 trilinear) and
// fog with its density (0 to 100). The defaults button sets defaultResolution, very fast, bilinear and fog at 100.
// *pResult 1 (OK, the caller applies the settings) or 2.
typedef struct J3DMenuDisplay
{
    const char* const* apResolutions;
    int numResolutions;
    int resolution;
    int defaultResolution;
    int performance;
    int filter;
    int bFog;
    int fogDensity;
    int bWindowMode;                // the launcher's Options only: fullscreen (0) or a window (1)
} J3DMenuDisplay;
int J3DMenu_ShowDisplaySettings(J3DMenuDisplay* pSettings, int* pResult);

// jonesConfig_ShowStoreDialog (template 190): what Indy carries, what the store sells, and the cart. The caller keeps
// the rows and the figures; pfAction does what a button did (J3DMENU_STORE_ADD the available row index,
// J3DMENU_STORE_RETURN the cart row index, J3DMENU_STORE_CLEAR, J3DMENU_STORE_DONE, which is also Escape) and returns
// nonzero to close the store. aIconIDs holds each item's icon (an Indy3D.rc ID), by item number.
#define J3DMENU_STORE_ADD    1
#define J3DMENU_STORE_RETURN 2
#define J3DMENU_STORE_CLEAR  3
#define J3DMENU_STORE_DONE   4
#define J3DMENU_STORE_MAXROWS 256
typedef struct J3DMenuStoreRow
{
    int item;
    char aName[128];
    int value;                      // how many (owned), or the price
} J3DMenuStoreRow;
typedef struct J3DMenuStore
{
    const int* aIconIDs;
    int numItems;
    J3DMenuStoreRow aOwned[J3DMENU_STORE_MAXROWS];
    int numOwned;
    J3DMenuStoreRow aAvailable[J3DMENU_STORE_MAXROWS];
    int numAvailable;
    J3DMenuStoreRow aCart[J3DMENU_STORE_MAXROWS];
    int numCart;
    int total;
    int balance;
    int (*pfAction)(struct J3DMenuStore* pStore, int action, int index);
} J3DMenuStore;
int J3DMenu_ShowStore(J3DMenuStore* pStore, int* pResult);

// The control options' settings (template 111): the control set (one of apSchemes), mouse and gamepad.
typedef struct J3DMenuControls
{
    const char* const* apSchemes;
    int numSchemes;
    int scheme;
    int bMouse;
    int bJoystick;
    int bJoystickAvailable;
    // The launcher's Options: opens the game's control options (J3DMenu_ShowControlOptions), which save what they
    // change, and fills this in again from what they saved. NULL: no such button.
    void (*pfEditSets)(struct J3DMenuControls* pControls);
} J3DMenuControls;

// The launcher's Options window (PORT-PLAN.md P6.31 to P6.33): the display, game play, sound and control settings as
// tabs, filled by the caller as for the in-game menus. *pResult 1 (OK, the caller applies them all) or 2.
int J3DMenu_ShowOptions(J3DMenuDisplay* pDisplay, J3DMenuGamePlay* pGamePlay, J3DMenuSound* pSound, J3DMenuControls* pControls,
                        int* pResult);

// The launcher (PORT-PLAN.md 6B), in a window of its own before the game window is shown. Returns 0 when this run has
// none (headless, a replay or input script, a start mode given in the environment, J3D_NO_LAUNCHER, J3D_MENUS=classic,
// the null platform), else 1 with *pChoice J3DMENU_LAUNCH_QUIT, _NEWGAME or _RESUME. pResumeDetail names what Resume
// Play loads, or is NULL when there is nothing to resume; pfOptions shows the Options (J3DMenu_ShowOptions).
#define J3DMENU_LAUNCH_QUIT    0
#define J3DMENU_LAUNCH_NEWGAME 1
#define J3DMENU_LAUNCH_RESUME  2
typedef struct J3DMenuLaunch
{
    const char* pResumeDetail;
    void (*pfOptions)(void);
} J3DMenuLaunch;
int J3DMenu_RunLauncher(const J3DMenuLaunch* pLaunch, int* pChoice);

// Nonzero once the player quit from the launcher: the game's startup then stops, which is a normal exit, not a failure.
int J3DMenu_QuitAtLaunch(void);

// The HUD's Help (JonesHud_OpenHelp): opens the manual the launcher's Help opens (manual.pdf in the data folder) in the
// system's viewer. Returns 0 when there is none or this platform cannot (PORT-PLAN.md P7.7).
int J3DMenu_OpenHelp(void);

/* The controls (templates 111, 115, 116, 117, 120). jonesConfig.c keeps the control sets and retail's rules; the
 * platform shows them and reports the buttons, through an action callback that changes the state and fills it again.
 * Plain C types only: platform/apple/menu/j3dmenu_ui.h includes this part for Swift. */
#define J3DMENU_CTRL_MAXBINDS   8
#define J3DMENU_CTRL_MAXACTIONS 40
#define J3DMENU_CTRL_MAXSETS    64

typedef struct J3DMenuBind
{
    int id;                         // the control id (stdControl's key id, or a joystick axis direction)
    char aName[48];
} J3DMenuBind;

typedef struct J3DMenuControlRow
{
    char aAction[64];
    J3DMenuBind aKeys[J3DMENU_CTRL_MAXBINDS];
    int numKeys;
    J3DMenuBind aButtons[J3DMENU_CTRL_MAXBINDS];
    int numButtons;
} J3DMenuControlRow;

// A control set being viewed or edited (115). side: 0 the keyboard and mouse, 1 the controller.
#define J3DMENU_CTRLSET_ADD      1  // a binding for row on side
#define J3DMENU_CTRLSET_CHANGE   2  // binding bind of row on side
#define J3DMENU_CTRLSET_REMOVE   3  // binding bind of row on side
#define J3DMENU_CTRLSET_DEFAULTS 4
#define J3DMENU_CTRLSET_SAVE     5
#define J3DMENU_CTRLSET_CANCEL   6
typedef struct J3DMenuControlSet
{
    char aName[128];
    int bReadOnly;                  // one of the game's own sets: viewed, not edited
    J3DMenuControlRow aRows[J3DMENU_CTRL_MAXACTIONS];
    int numRows;
    int (*pfAction)(struct J3DMenuControlSet* pSet, int action, int row, int side, int bind); // nonzero closes
} J3DMenuControlSet;
int J3DMenu_EditControlSet(J3DMenuControlSet* pSet, int* pResult);

// The control options (111): the sets (the game's own first), the chosen one, mouse and controller.
#define J3DMENU_CTRLOPT_SELECT   1  // value: the set
#define J3DMENU_CTRLOPT_NEW      2
#define J3DMENU_CTRLOPT_EDIT     3
#define J3DMENU_CTRLOPT_DELETE   4
#define J3DMENU_CTRLOPT_OK       5
#define J3DMENU_CTRLOPT_CANCEL   6
#define J3DMENU_CTRLOPT_MOUSE    7  // value: on or off
#define J3DMENU_CTRLOPT_JOYSTICK 8  // value: on or off
typedef struct J3DMenuControlOptions
{
    char aaNames[J3DMENU_CTRL_MAXSETS][128];
    int numSets;
    int numFixedSets;
    int selected;
    int bMouse;
    int bJoystick;
    int bJoystickAvailable;
    int (*pfAction)(struct J3DMenuControlOptions* pOptions, int action, int value); // nonzero closes
} J3DMenuControlOptions;
int J3DMenu_ShowControlOptions(J3DMenuControlOptions* pOptions, int* pResult);

// A name typed in (116): *pResult 1 with it in pName (size bytes, which also holds the starting text), else 2.
int J3DMenu_AskName(const char* pTitle, const char* pPrompt, char* pName, int size, int* pResult);

// Waiting for a key or a button (117). pfInput gets the DirectInput key code of a key pressed, or -1 about ten times a
// second to look at the controllers itself; it returns nonzero once it has what it waited for. *pResult 1 when it
// did, 2 when the player cancelled.
int J3DMenu_CaptureControl(const char* pPrompt, const char* pAction, int (*pfInput)(int dik), int* pResult);

// jonesConfig_ShowGameOverDialog's buttons: *pResult is 1177 (start the level again; offered only with bCanRestart),
// 1178 (load a save) or 1179 (quit). The caller does what the button did.
int J3DMenu_ShowGameOver(int bCanRestart, int* pResult);

// The statistics (jonesConfig_ShowStatisticsDialog) and the summary at the end of a level
// (jonesConfig_ShowLevelCompletedDialog): pages of label and value beside the game's IQ picture.
// pfDrawPicture draws iqPoints on the picture and returns it and its mask (HBITMAPs that stay the caller's; the mask
// is white where the picture is transparent); it is called once a page before the menu opens.
#define J3DMENU_FIGURES_MAXROWS 6
typedef struct J3DMenuFigurePage
{
    char aTitle[128];               // under the picture, or empty
    int iqPoints;
    const char* apLabels[J3DMENU_FIGURES_MAXROWS]; // string-table names or the text itself
    char aaValues[J3DMENU_FIGURES_MAXROWS][32];
    int numRows;
} J3DMenuFigurePage;
typedef void (*J3DMenuDrawPicture)(int iqPoints, void** phImage, void** phMask);
int J3DMenu_ShowFigures(const char* pTitle, const J3DMenuFigurePage* pPages, int numPages, int startPage,
                        J3DMenuDrawPicture pfDrawPicture, int* pResult);

// For tests: called after each pass of the game loop with the number of passes so far. With J3D_MENU_TEST=<n> in the
// environment, at pass n every menu is opened once through the functions above and what each returned is printed
// (run it with J3D_MENU_AUTOCANCEL, platform/apple/menu/MenuHost.swift, so nobody has to answer).
void J3DMenu_AfterFrame(unsigned long numFrames);

#ifdef __cplusplus
}
#endif

#endif
