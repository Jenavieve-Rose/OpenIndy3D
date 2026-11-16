#ifndef J3D_PLATFORM_APPLE_J3DMENU_UI_H
#define J3D_PLATFORM_APPLE_J3DMENU_UI_H
/*
 * The interface between j3dmenu_apple.c and the SwiftUI menus (platform/apple/menu/, PORT-PLAN.md Phase 6). It is
 * the Swift target's bridging header, so it holds plain C types only and includes nothing of the game.
 *
 * Every J3DMenuUI function lays its menu over the game window (an NSWindow*), runs a modal session until the player
 * answers, and returns. Main thread only. Strings are UTF-8 or the game's Latin-1 text; the Swift side copies them.
 */
#include <stdint.h>

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

#ifndef J3DMENU_SWIFT // implemented in Swift (@_cdecl); these are the prototypes for C

// A title, a text and up to eight buttons. Returns the index of the button chosen. defaultIndex is the button Return
// presses, cancelIndex the one Escape presses (-1: none).
int32_t J3DMenuUI_Choice(void* pGameWindow, const char* pTitle, const char* pText, const char* const* apButtons, int32_t numButtons,
                         int32_t defaultIndex, int32_t cancelIndex, int32_t layout);

// The save and load menu. Returns 1 with the chosen name in pOutName (J3DMENU_NAME_SIZE bytes), else 0.
int32_t J3DMenuUI_PickSave(void* pGameWindow, const J3DMenuSavePicker* pPicker, char* pOutName);

#endif

#endif
