#ifndef J3D_PLATFORM_APPLE_J3DMENU_UI_H
#define J3D_PLATFORM_APPLE_J3DMENU_UI_H
/*
 * The interface between j3dmenu_apple.c and the SwiftUI menus (platform/apple/menu/, PORT-PLAN.md Phase 6). It is
 * the Swift target's bridging header, so it holds plain C types only and includes nothing of the game.
 *
 * Every J3DMenuUI function lays its menu over the game window (an NSWindow*), runs a modal session until the player
 * answers, and returns. Main thread only. Strings are UTF-8 or the game's Latin-1 text; the Swift side copies them.
 */
#include <stddef.h>
#include <stdint.h>

#include "../../common/j3dmenu.h" // the controls' types (plain C)

#define J3DMENU_THUMB_WIDTH  128
#define J3DMENU_THUMB_HEIGHT 96
#define J3DMENU_THUMB_BYTES  (J3DMENU_THUMB_WIDTH * J3DMENU_THUMB_HEIGHT * 4) // R, G, B, X in memory, top row first

#define J3DMENU_NAME_SIZE 128

// How a choice is laid out
#define J3DMENU_CHOICE_ROW    0  // a message with its buttons in a row under it
#define J3DMENU_CHOICE_COLUMN 1  // large buttons one under the other, as the launcher's

typedef struct J3DMenuSave
{
    char aName[J3DMENU_NAME_SIZE];  // the file's name, with its extension
    int64_t modified;               // seconds since 1970
} J3DMenuSave;

typedef struct J3DMenuSavePicker
{
    const char* pTitle;
    int32_t bSave;                  // 1: name a save (the name field can be typed in); 0: choose one to load
    const J3DMenuSave* pSaves;      // most recent first
    int32_t numSaves;
    const char* pInitialName;       // the name to start with, or NULL
    const char* pAcceptLabel;
    const char* pCancelLabel;
    const uint8_t* pCurrentThumb;   // J3DMENU_THUMB_BYTES of the game as it is now (saving), or NULL
    // Fills pPixels (J3DMENU_THUMB_BYTES) with a save's picture; returns 0 if it has none.
    int32_t (*pfLoadThumb)(const char* pName, uint8_t* pPixels);
} J3DMenuSavePicker;

// One line of a form of settings (J3DMenuUI_Form). value is read when the form opens and holds the player's choice
// when it closes with OK.
#define J3DMENU_ITEM_TOGGLE 0   // on or off
#define J3DMENU_ITEM_STEPS  1   // a slider over apNames, the chosen name shown above it (value: the index)
#define J3DMENU_ITEM_SLIDER 2   // a slider from 0 to maxValue with apNames as captions under it
#define J3DMENU_ITEM_CHOICE 3   // one of apNames, side by side (value: the index)
#define J3DMENU_ITEM_LIST   4   // one of apNames, from a pop-up list, for more names than fit side by side

typedef struct J3DMenuItem
{
    int32_t kind;
    const char* pGroup;             // starts a titled group with this item; NULL goes on in the current one
    const char* pLabel;             // the item's own label, or NULL when the group's title says it all
    int32_t value;
    int32_t maxValue;               // J3DMENU_ITEM_SLIDER
    int32_t bDisabled;
    const char* const* apNames;
    int32_t numNames;
} J3DMenuItem;

// Called when the player changes an item (a slider: when it is let go), with the items holding the values as they
// are now. It may change any item (values, names, bDisabled) and returns nonzero if it did, so the form reads them
// again.
typedef int32_t (*J3DMenuFormChanged)(J3DMenuItem* pItems, int32_t numItems, int32_t changedIndex);

// A page of figures (J3DMenuUI_Stats): a title under the picture, and rows of label and value. The picture is the
// game's IQ picture with the page's points drawn on it by the game, R, G, B, A in memory, top row first; when a page
// has none, iqPoints is written in its place.
#define J3DMENU_STATS_MAXROWS 6
typedef struct J3DMenuStatsPage
{
    const char* pTitle;             // or NULL
    int32_t iqPoints;
    const char* apLabels[J3DMENU_STATS_MAXROWS];
    char aaValues[J3DMENU_STATS_MAXROWS][32];
    int32_t numRows;
    const uint8_t* pPicture;        // or NULL
    int32_t pictureWidth;
    int32_t pictureHeight;
} J3DMenuStatsPage;

typedef struct J3DMenuStats
{
    const char* pTitle;
    const J3DMenuStatsPage* pPages;
    int32_t numPages;
    int32_t startPage;
    const char* pCloseLabel;
} J3DMenuStats;

// The store (J3DMenuUI_Store). The state is read through the action callback: J3DMENU_UI_STORE_READ fills it, and
// every other action (the store's buttons, numbered as j3dmenu.h's J3DMENU_STORE_*) fills it again after doing its
// work; a nonzero return closes the store. The codes are j3dmenu.h's J3DMENU_STORE_* (j3dmenu_apple.c checks).
#define J3DMENU_UI_STORE_READ   0
#define J3DMENU_UI_STORE_ADD    1
#define J3DMENU_UI_STORE_RETURN 2
#define J3DMENU_UI_STORE_CLEAR  3
#define J3DMENU_UI_STORE_DONE   4
typedef struct J3DMenuStoreRowUI
{
    int32_t item;
    const char* pName;
    int32_t value;
} J3DMenuStoreRowUI;

typedef struct J3DMenuStoreState
{
    const J3DMenuStoreRowUI* pOwned;
    int32_t numOwned;
    const J3DMenuStoreRowUI* pAvailable;
    int32_t numAvailable;
    const J3DMenuStoreRowUI* pCart;
    int32_t numCart;
    int32_t total;
    int32_t balance;
} J3DMenuStoreState;

typedef int32_t (*J3DMenuStoreAction)(int32_t action, int32_t index, J3DMenuStoreState* pState);

typedef struct J3DMenuStoreUI
{
    const char* pTitle;
    const char* pOwnedTitle;
    const char* pAvailableTitle;
    const char* pCartTitle;
    const char* pCountLabel;
    const char* pCostLabel;
    const char* pTotalLabel;
    const char* pBalanceLabel;
    const char* pAddLabel;
    const char* pReturnLabel;
    const char* pClearLabel;
    const char* pDoneLabel;
    const uint8_t* pIcons;          // numIcons icons of iconSize by iconSize, R, G, B, A, by item number; or NULL
    int32_t iconSize;
    int32_t numIcons;
} J3DMenuStoreUI;

// The launcher's Options window (J3DMenuUI_OptionsWindow): a page of form items a tab. The defaults button calls the
// shown page's pfChanged with -1, and is greyed for a page that has none.
typedef struct J3DMenuFormPage
{
    const char* pTitle;
    J3DMenuItem* pItems;
    int32_t numItems;
    J3DMenuFormChanged pfChanged;
} J3DMenuFormPage;

// The launcher (J3DMenuUI_Launcher), a window of its own before the game window is shown.
#define J3DMENU_LAUNCH_QUIT    0
#define J3DMENU_LAUNCH_NEWGAME 1
#define J3DMENU_LAUNCH_RESUME  2
typedef struct J3DMenuLauncherUI
{
    const char* pTitle;             // the window's
    const uint8_t* pPicture;        // the launch picture as a .bmp file, or NULL for the plain title card
    size_t pictureSize;
    const char* apLabels[5];        // Start New Game, Resume Play, Help, Options, Quit
    const char* pResumeDetail;      // what Resume Play would load, or NULL when there is nothing (it is greyed)
    const char* pStatus;            // the line under the buttons
    const char* pVersion;
    void (*pfOptions)(void* pLauncherWindow);   // the NSWindow*, to put Options in front of
    const char* pHelpPath;          // the file Help opens (the manual), or NULL when there is none (it is greyed)
} J3DMenuLauncherUI;

#ifndef J3DMENU_SWIFT // implemented in Swift (@_cdecl); these are the prototypes for C

// A title, a text and up to eight buttons. Returns the index of the button chosen. defaultIndex is the button Return
// presses, cancelIndex the one Escape presses (-1: none).
// disabledMask: bit i set greys button i out.
int32_t J3DMenuUI_Choice(void* pGameWindow, const char* pTitle, const char* pText, const char* const* apButtons, int32_t numButtons,
                         int32_t defaultIndex, int32_t cancelIndex, int32_t layout, uint32_t disabledMask);

// A form of settings with OK and Cancel, and a "defaults" button when pDefaultsLabel is given (it calls pfChanged
// with changedIndex -1). Returns 1 for OK, with the items' values set, else 0.
int32_t J3DMenuUI_Form(void* pGameWindow, const char* pTitle, J3DMenuItem* pItems, int32_t numItems, const char* pOkLabel,
                       const char* pCancelLabel, const char* pDefaultsLabel, J3DMenuFormChanged pfChanged);

// Pages of figures with a Close button, and arrows to turn the pages when there are several.
void J3DMenuUI_Stats(void* pGameWindow, const J3DMenuStats* pStats);

// The Options window: modal, in a window of its own centred on pParentWindow (an NSWindow*, or NULL). Returns 1 for
// OK with every page's items holding the values chosen, else 0.
int32_t J3DMenuUI_OptionsWindow(void* pParentWindow, const char* pTitle, J3DMenuFormPage* pPages, int32_t numPages,
                                const char* pOkLabel, const char* pCancelLabel, const char* pDefaultsLabel);

// The launcher, until the player chooses: J3DMENU_LAUNCH_*.
int32_t J3DMenuUI_Launcher(const J3DMenuLauncherUI* pUI);

// The control options (111), until pOptions->pfAction returns nonzero. apLabels: the title, the sets' heading, New,
// Edit, View, Delete, the mouse switch, the controller switch, OK, Cancel.
void J3DMenuUI_ControlOptions(void* pGameWindow, const char* const* apLabels, J3DMenuControlOptions* pOptions);

// A control set (115), until pSet->pfAction returns nonzero. apLabels: the keyboard's heading, the controller's, Add,
// Change, Remove, Default Settings, Save, Cancel, Close.
void J3DMenuUI_ControlSet(void* pGameWindow, const char* const* apLabels, J3DMenuControlSet* pSet);

// A name typed in (116). Returns 1 for OK with it in pName (size bytes, which also holds the starting text), else 0.
int32_t J3DMenuUI_AskName(void* pGameWindow, const char* pTitle, const char* pPrompt, const char* pOkLabel,
                          const char* pCancelLabel, char* pName, int32_t size);

// Waiting for a key or a button (117). pfInput is called with a key's USB HID usage when one is pressed, and with 0
// about ten times a second; it returns nonzero once it has what it waits for. Returns 1 then, 0 when cancelled.
int32_t J3DMenuUI_Capture(void* pGameWindow, const char* pTitle, const char* pPrompt, const char* pAction,
                          const char* pCancelLabel, int32_t (*pfInput)(int32_t hidUsage));

// How the menus read the controller: pfPoll returns 1 to 4 for a move (up, down, left, right), 5 for accept, 6 for
// cancel, 0 for nothing new. Set once; the menus call it about thirty times a second while they are up.
void J3DMenuUI_SetPadPoll(int32_t (*pfPoll)(void));

// The store, until pfAction returns nonzero.
void J3DMenuUI_Store(void* pGameWindow, const J3DMenuStoreUI* pUI, J3DMenuStoreAction pfAction);

// The save and load menu. Returns 1 with the chosen name in pOutName (J3DMENU_NAME_SIZE bytes), else 0.
int32_t J3DMenuUI_PickSave(void* pGameWindow, const J3DMenuSavePicker* pPicker, char* pOutName);

#endif

#endif
