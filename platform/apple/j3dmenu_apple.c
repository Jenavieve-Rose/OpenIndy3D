/*
 * j3dmenu on macOS: the game's menus as SwiftUI views over the game window (PORT-PLAN.md Phase 6, decision 20).
 *
 * This file is the game's side of each menu: what jonesConfig.c's dialog procedure did around its dialog (the
 * strings, the checks on a save's name, the confirmations), with the drawing and the input handed to the Swift code
 * in platform/apple/menu/ through j3dmenu_ui.h. The rules are retail's, taken from the dialog procedures named at
 * each function.
 *
 * While a menu is up the mouse is the player's (relative mode off, cursor shown) and the game's window gets no
 * events; both are put back when it closes.
 */
#include <platform/common/j3dmenu.h>

#include "menu/j3dmenu_ui.h"

#include <SDL3/SDL.h>

#include <Windows.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesLevel.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/jonesString.h>
#include <sith/Dss/sithGamesave.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>
#include <std/General/stdFnames.h>
#include <std/Win95/stdWin95.h>

#include <platform/common/host_keys.h>
#include <platform/common/host_path.h>
#include <platform/common/j3dgui/j3dgui.h>
#include <platform/common/host_record.h>
#include <platform/common/stdDisplay_common.h>

#include <dirent.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ; // posix_spawn's environment (unistd.h declares it only with _GNU_SOURCE-like settings)
#include <platform/common/host_dirs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MENU_SAVE_EXT "nds"

/* ---- Showing a menu ---------------------------------------------------------------------------------------------------- */

typedef struct MenuSession
{
    SDL_Window* pWindow;
    void* pNSWindow;
    bool bRelativeMouse;
    bool bCursorVisible;
} MenuSession;

// The game window's NSWindow when a menu can be laid over it in this run: not headless, not in a replay (which holds
// no answers), not before the window is on screen, and not when J3D_MENUS=classic asks for the Win32 dialogs.
static int menu_bLauncherUp; // the launcher is up: menus are laid over its window in front, not the hidden game window

static void* menu_GameNSWindow(SDL_Window** ppWindow)
{
    const char* pMode = getenv("J3D_MENUS");
    SDL_Window* pWindow = (SDL_Window*)stdWin95_GetWindow();
    if ( menu_bLauncherUp )
    {
        *ppWindow = NULL;
        return J3DMenuUI_FrontWindow();
    }
    if ( J3DDisplay_IsHeadless() || J3DRecord_IsReplaying() || !pWindow || (pMode && strcmp(pMode, "classic") == 0) )
    {
        return NULL;
    }
    if ( SDL_GetWindowFlags(pWindow) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED) )
    {
        return NULL;
    }
    *ppWindow = pWindow;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(pWindow), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

int J3DMenu_IsAvailable(void)
{
    SDL_Window* pWindow;
    return menu_GameNSWindow(&pWindow) != NULL;
}

// The controller in the menus (PORT-PLAN.md P6.41): the south button (A on an Xbox pad, cross on a PlayStation one)
// accepts, the east button cancels, the pad and the left stick move. A move repeats while held, as a key does. The
// gamepads are the ones the game opened (stdControl_sdl.c); their state is refreshed without pumping window events,
// which the menu's modal session is taking.
static int menu_padPrevCode = 0;
static int menu_bPadPrime = 1; // the first reading of a menu only records what is held (the button that opened it)

static int32_t menu_PadPoll(void)
{
    static Uint64 nextRepeat = 0;
    const Uint64 now = SDL_GetTicks();
    SDL_JoystickID* aIds;
    int numPads = 0;
    int code = 0;
    int i;

    SDL_UpdateGamepads();
    aIds = SDL_GetGamepads(&numPads);
    for ( i = 0; aIds && i < numPads && !code; ++i )
    {
        SDL_Gamepad* pPad = SDL_GetGamepadFromID(aIds[i]);
        Sint16 x;
        Sint16 y;
        if ( !pPad )
        {
            continue;
        }
        x = SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTX);
        y = SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTY);
        if ( SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_SOUTH) )
        {
            code = 5;
        }
        else if ( SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_EAST) )
        {
            code = 6;
        }
        else if ( SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -16000 )
        {
            code = 1;
        }
        else if ( SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 16000 )
        {
            code = 2;
        }
        else if ( SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -16000 )
        {
            code = 3;
        }
        else if ( SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 16000 )
        {
            code = 4;
        }
    }
    SDL_free(aIds);

    if ( menu_bPadPrime )
    {
        menu_bPadPrime = 0;
        menu_padPrevCode = code;
        nextRepeat = now + 400;
        return 0;
    }
    if ( code != menu_padPrevCode )
    {
        menu_padPrevCode = code;
        nextRepeat = now + 400;
        return code;
    }
    if ( code >= 1 && code <= 4 && now >= nextRepeat )
    {
        nextRepeat = now + 120;
        return code;
    }
    return 0; // held accept and cancel do not repeat: one press answers one menu
}

static int menu_Begin(MenuSession* pSession)
{
    static int bPadPollSet = 0;
    if ( !bPadPollSet )
    {
        J3DMenuUI_SetPadPoll(menu_PadPoll);
        bPadPollSet = 1;
    }
    menu_bPadPrime = 1;
    SDL_Window* pWindow = NULL;
    pSession->pNSWindow = menu_GameNSWindow(&pWindow);
    if ( !pSession->pNSWindow )
    {
        return 0;
    }
    pSession->pWindow = pWindow;
    if ( !pWindow )
    {
        return 1; // over the launcher: the game's window and its mouse are not in use yet
    }
    pSession->bRelativeMouse = SDL_GetWindowRelativeMouseMode(pWindow);
    pSession->bCursorVisible = SDL_CursorVisible();
    SDL_SetWindowRelativeMouseMode(pWindow, false);
    SDL_ShowCursor();
    return 1;
}

static void menu_End(MenuSession* pSession)
{
    float dx;
    float dy;
    if ( !pSession->pWindow )
    {
        return;
    }
    SDL_SetWindowRelativeMouseMode(pSession->pWindow, pSession->bRelativeMouse);
    if ( !pSession->bCursorVisible )
    {
        SDL_HideCursor();
    }
    // The keys and the mouse motion that answered the menu are not the game's.
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
    SDL_ResetKeyboard();
    SDL_GetRelativeMouseState(&dx, &dy);
}

// A string-table entry, or the fallback when the table has none, as a label: without the '&' that marks a Win32
// mnemonic. jonesString_GetString's result does not outlive the next call, so the text is copied; the copies are
// kept in turn, enough of them for everything one menu shows.
static const char* menu_String(const char* pName, const char* pFallback)
{
    static char aaCopies[16][512];
    static int next = 0;
    const char* pText = jonesString_GetString(pName);
    char* pCopy = aaCopies[next];
    size_t i = 0;
    next = (next + 1) % 16;
    if ( !pText || !pText[0] )
    {
        pText = pFallback;
    }
    for ( ; *pText && i < sizeof(aaCopies[0]) - 1; ++pText )
    {
        if ( *pText == '&' && pText[1] != '&' )
        {
            continue;
        }
        if ( *pText == '&' )
        {
            pText++;
        }
        pCopy[i++] = *pText;
    }
    pCopy[i] = 0;
    return pCopy;
}

// A message with one button.
static void menu_Message(MenuSession* pSession, const char* pTitle, const char* pText)
{
    const char* apButtons[1];
    apButtons[0] = menu_String("JONES_STR_OK", "OK");
    J3DMenuUI_Choice(pSession->pNSWindow, pTitle, pText, apButtons, 1, 0, 0, J3DMENU_CHOICE_ROW, 0);
}

// A question with Yes and No. Returns nonzero for Yes.
static int menu_Confirm(MenuSession* pSession, const char* pTitle, const char* pText)
{
    const char* apButtons[2];
    apButtons[0] = menu_String("JONES_STR_YES", "Yes");
    apButtons[1] = menu_String("JONES_STR_NO", "No");
    return J3DMenuUI_Choice(pSession->pNSWindow, pTitle, pText, apButtons, 2, 0, 1, J3DMENU_CHOICE_ROW, 0) == 0;
}

/* ---- Messages ---------------------------------------------------------------------------------------------------------- */

int J3DMenu_ShowMessage(const char* pTitle, const char* pText, int iconID, int* pResult)
{
    MenuSession session;
    (void)iconID;
    if ( !menu_Begin(&session) )
    {
        return 0;
    }
    menu_Message(&session, menu_String(pTitle, pTitle), pText);
    menu_End(&session);
    *pResult = 1;
    return 1;
}

int J3DMenu_Ask(const char* pTitle, const char* pText, int* pResult)
{
    MenuSession session;
    if ( !menu_Begin(&session) )
    {
        return 0;
    }
    *pResult = menu_Confirm(&session, menu_String(pTitle, pTitle), pText) ? 1 : 2;
    menu_End(&session);
    return 1;
}

int J3DMenu_ShowInsertCD(const char* pText, int* pResult)
{
    MenuSession session;
    const char* apButtons[2];
    if ( !menu_Begin(&session) )
    {
        return 0;
    }
    apButtons[0] = menu_String("JONES_STR_OK", "OK");
    apButtons[1] = menu_String("JONES_STR_QUIT", "Quit");
    *pResult = J3DMenuUI_Choice(session.pNSWindow, menu_String("JONES_STR_INSERTCD", "Insert CD"), pText, apButtons, 2, 0, 1,
                                J3DMENU_CHOICE_ROW, 0) == 0 ? 1 : 2;
    menu_End(&session);
    return 1;
}

/* ---- Saved games ------------------------------------------------------------------------------------------------------- */

static int menu_CompareSaves(const void* a, const void* b)
{
    const J3DMenuSave* pA = a;
    const J3DMenuSave* pB = b;
    if ( pA->modified != pB->modified )
    {
        return pA->modified > pB->modified ? -1 : 1; // most recent first
    }
    return strcasecmp(pA->aName, pB->aName);
}

// The saved games in the save folder. The caller frees *ppSaves.
static int menu_ListSaves(J3DMenuSave** ppSaves)
{
    char aHostDir[1024];
    J3DMenuSave* pSaves = NULL;
    int numSaves = 0;
    int capacity = 0;
    DIR* pDir;
    struct dirent* pEntry;

    *ppSaves = NULL;
    J3DHost_ToPosixPath(aHostDir, sizeof(aHostDir), sithGetSaveGamesDir());
    pDir = opendir(aHostDir);
    if ( !pDir )
    {
        return 0;
    }
    while ( (pEntry = readdir(pDir)) != NULL )
    {
        char aPath[2048];
        struct stat st;
        const char* pDot = strrchr(pEntry->d_name, '.');
        if ( pEntry->d_name[0] == '.' || !pDot || strcasecmp(pDot + 1, MENU_SAVE_EXT) != 0 || strlen(pEntry->d_name) >= J3DMENU_NAME_SIZE )
        {
            continue;
        }
        snprintf(aPath, sizeof(aPath), "%s/%s", aHostDir, pEntry->d_name);
        if ( stat(aPath, &st) != 0 || !S_ISREG(st.st_mode) )
        {
            continue;
        }
        if ( numSaves == capacity )
        {
            int newCapacity = capacity ? capacity * 2 : 32;
            J3DMenuSave* pNew = realloc(pSaves, (size_t)newCapacity * sizeof(*pSaves));
            if ( !pNew )
            {
                break;
            }
            pSaves = pNew;
            capacity = newCapacity;
        }
        memset(&pSaves[numSaves], 0, sizeof(pSaves[numSaves]));
        strcpy(pSaves[numSaves].aName, pEntry->d_name);
        pSaves[numSaves].modified = (int64_t)st.st_mtime;
        numSaves++;
    }
    closedir(pDir);
    if ( numSaves )
    {
        qsort(pSaves, (size_t)numSaves, sizeof(*pSaves), menu_CompareSaves);
    }
    *ppSaves = pSaves;
    return numSaves;
}

// A thumbnail bitmap (24 or 32 bits, blue first, as sithGamesave makes them) as the menu's pixels.
static int menu_ThumbFromBitmap(HBITMAP hBitmap, uint8_t* pPixels)
{
    BITMAP bmp;
    int x;
    int y;
    if ( !hBitmap || GetObject(hBitmap, sizeof(bmp), &bmp) != (int)sizeof(bmp) || !bmp.bmBits )
    {
        return 0;
    }
    if ( bmp.bmWidth != J3DMENU_THUMB_WIDTH || bmp.bmHeight != J3DMENU_THUMB_HEIGHT || (bmp.bmBitsPixel != 24 && bmp.bmBitsPixel != 32) )
    {
        return 0;
    }
    for ( y = 0; y < J3DMENU_THUMB_HEIGHT; ++y )
    {
        const uint8_t* pSrc = (const uint8_t*)bmp.bmBits + (size_t)y * (size_t)bmp.bmWidthBytes;
        uint8_t* pDst = pPixels + (size_t)y * J3DMENU_THUMB_WIDTH * 4;
        const int step = bmp.bmBitsPixel / 8;
        for ( x = 0; x < J3DMENU_THUMB_WIDTH; ++x )
        {
            pDst[x * 4 + 0] = pSrc[x * step + 2];
            pDst[x * 4 + 1] = pSrc[x * step + 1];
            pDst[x * 4 + 2] = pSrc[x * step + 0];
            pDst[x * 4 + 3] = 255;
        }
    }
    return 1;
}

static int32_t menu_LoadThumb(const char* pName, uint8_t* pPixels)
{
    char aPath[J3D_PATHSIZE];
    HBITMAP hBitmap;
    int bLoaded;
    stdFnames_MakePath(aPath, (int)sizeof(aPath), sithGetSaveGamesDir(), pName);
    hBitmap = sithGamesave_LoadThumbnail(aPath);
    bLoaded = menu_ThumbFromBitmap(hBitmap, pPixels);
    if ( hBitmap )
    {
        DeleteObject(hBitmap);
    }
    return bLoaded;
}

static int menu_SaveExists(const char* pName)
{
    char aPath[J3D_PATHSIZE];
    char aHostPath[1024];
    struct stat st;
    stdFnames_MakePath(aPath, (int)sizeof(aPath), sithGetSaveGamesDir(), pName);
    J3DHost_ToPosixPath(aHostPath, sizeof(aHostPath), aPath);
    return stat(aHostPath, &st) == 0 && S_ISREG(st.st_mode);
}

// The name as typed becomes a file name: blanks trimmed, the extension added when it has none. Returns 0 for a name
// that cannot be a save's: empty, with a wildcard or a path in it, or with another extension (retail's CDN_FILEOK
// checks, jonesConfig_SaveGameDialogHookProc).
static int menu_NormalizeSaveName(const char* pTyped, char* pName, size_t size)
{
    const char* pStart = pTyped;
    size_t len;
    const char* pDot;
    while ( *pStart == ' ' )
    {
        pStart++;
    }
    len = strlen(pStart);
    while ( len && pStart[len - 1] == ' ' )
    {
        len--;
    }
    if ( !len || len + sizeof("." MENU_SAVE_EXT) > size )
    {
        return 0;
    }
    memcpy(pName, pStart, len);
    pName[len] = 0;
    if ( strcspn(pName, "?*/\\:") != len )
    {
        return 0;
    }
    pDot = strrchr(pName, '.');
    if ( !pDot )
    {
        strcat(pName, "." MENU_SAVE_EXT);
    }
    else if ( strcasecmp(pDot + 1, MENU_SAVE_EXT) != 0 )
    {
        return 0;
    }
    return 1;
}

// The names the game keeps for itself: each level's automatic save and the quick save. The player cannot save over
// them (retail's CDN_FILEOK check).
static int menu_IsReservedSaveName(const char* pName)
{
    char aReserved[256];
    int levelNum;
    for ( levelNum = 0; levelNum < JONESLEVEL_BONUSLEVELNUM; ++levelNum )
    {
        const char* pLevelName = sithGetLevelSaveFilename(levelNum);
        if ( pLevelName )
        {
            snprintf(aReserved, sizeof(aReserved), "%s%s", sithGetAutoSaveFilePrefix(), pLevelName);
            if ( strcasecmp(pName, aReserved) == 0 )
            {
                return 1;
            }
        }
    }
    snprintf(aReserved, sizeof(aReserved), "%s.%s", sithGetQuickSaveFilePrefix(), MENU_SAVE_EXT);
    return strcasecmp(pName, aReserved) == 0;
}

static void menu_FormatName(char* pOut, size_t size, const char* pStringName, const char* pFallbackFormat, const char* pName)
{
    const char* pFormat = menu_String(pStringName, pFallbackFormat);
    // The table's formats take the one name; anything else falls back so a bad entry cannot misread the arguments.
    const char* pPercent = strchr(pFormat, '%');
    if ( !pPercent || pPercent[1] != 's' || strchr(pPercent + 1, '%') )
    {
        pFormat = pFallbackFormat;
    }
    snprintf(pOut, size, pFormat, pName);
}

// The save or load menu, asked again until the player cancels or gives a name the game accepts.
static int menu_PickSave(MenuSession* pSession, int bSave, char* pOutPath)
{
    const char* pTitle = bSave ? menu_String("JONES_STR_SAVEGM", "Save Game") : menu_String("JONES_STR_LOADGM", "Load Game");
    uint8_t* pCurrentThumb = NULL;
    char aTyped[J3DMENU_NAME_SIZE];
    char aName[J3DMENU_NAME_SIZE];
    char aText[512];
    int bChosen = 0;

    aTyped[0] = 0;
    if ( bSave )
    {
        // The picture of the game as it was when the menu opened, which the save will carry
        pCurrentThumb = malloc(J3DMENU_THUMB_BYTES);
        if ( pCurrentThumb && !menu_ThumbFromBitmap(sithGamesave_CreateThumbnail(), pCurrentThumb) )
        {
            free(pCurrentThumb);
            pCurrentThumb = NULL;
        }
    }
    else
    {
        // The save the game last wrote or read is the one offered
        const char* pLast = sithGamesave_GetLastFilename();
        if ( pLast && pLast[0] )
        {
            const char* pBase = strrchr(pLast, '\\');
            if ( strrchr(pLast, '/') > pBase )
            {
                pBase = strrchr(pLast, '/');
            }
            snprintf(aTyped, sizeof(aTyped), "%s", pBase ? pBase + 1 : pLast);
        }
    }

    for ( ;; )
    {
        J3DMenuSavePicker picker;
        J3DMenuSave* pSaves = NULL;
        int32_t bPicked;

        memset(&picker, 0, sizeof(picker));
        picker.pTitle = pTitle;
        picker.bSave = bSave;
        picker.numSaves = menu_ListSaves(&pSaves);
        picker.pSaves = pSaves;
        picker.pInitialName = aTyped[0] ? aTyped : NULL;
        picker.pAcceptLabel = bSave ? "Save" : "Load";
        picker.pCancelLabel = menu_String("JONES_STR_CANCEL", "Cancel");
        picker.pCurrentThumb = pCurrentThumb;
        picker.pfLoadThumb = menu_LoadThumb;
        bPicked = J3DMenuUI_PickSave(pSession->pNSWindow, &picker, aTyped);
        free(pSaves);
        if ( !bPicked )
        {
            break;
        }

        if ( !menu_NormalizeSaveName(aTyped, aName, sizeof(aName)) || (bSave && menu_IsReservedSaveName(aName)) )
        {
            menu_FormatName(aText, sizeof(aText), "JONES_STR_INVALIDFILE", "\"%s\" is not a valid name for a saved game.", aTyped);
            menu_Message(pSession, pTitle, aText);
            continue;
        }
        if ( bSave )
        {
            if ( menu_SaveExists(aName) )
            {
                menu_FormatName(aText, sizeof(aText), "JONES_STR_OVERWRITE", "Replace the saved game \"%s\"?", aName);
                if ( !menu_Confirm(pSession, pTitle, aText) )
                {
                    continue;
                }
            }
        }
        else
        {
            if ( !menu_SaveExists(aName) )
            {
                menu_FormatName(aText, sizeof(aText), "JONES_STR_NOFILE", "There is no saved game named \"%s\".", aName);
                menu_Message(pSession, pTitle, aText);
                continue;
            }
            // Loading throws away the game in progress, unless Indy is already dead (jonesConfig_LoadGameDialogHookProc)
            if ( JonesMain_HasStarted() && sithPlayer_pLocalPlayerThing
                 && (sithPlayer_pLocalPlayerThing->flags & (SITH_TF_DYING | SITH_TF_DESTROYED)) == 0
                 && !menu_Confirm(pSession, pTitle, menu_String("JONES_STR_PROGRESS1", "Your current progress will be lost. Continue?")) )
            {
                continue;
            }
        }
        stdFnames_MakePath(pOutPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, sithGetSaveGamesDir(), aName);
        bChosen = 1;
        break;
    }
    free(pCurrentThumb);
    return bChosen;
}

int J3DMenu_GetSaveGameFilePath(char* pOutFilePath, int* pResult)
{
    MenuSession session;
    if ( !pOutFilePath || !menu_Begin(&session) )
    {
        return 0;
    }
    *pResult = menu_PickSave(&session, 1, pOutFilePath) ? 1 : 2;
    menu_End(&session);
    return 1;
}

int J3DMenu_GetLoadGameFilePath(char* pDestNdsPath, int* pResult)
{
    MenuSession session;
    if ( !pDestNdsPath || !menu_Begin(&session) )
    {
        return 0;
    }
    *pResult = menu_PickSave(&session, 0, pDestNdsPath) ? 1 : 2;
    menu_End(&session);
    return 1;
}

/* ---- Leaving the game -------------------------------------------------------------------------------------------------- */

// jonesConfig_ExitGameDlgProc's three buttons, in its order: back to the game (the default), save and quit, quit.
int J3DMenu_ShowExitGame(char* pSaveGameFilePath, int* pResult)
{
    static const int aResults[3] = { 2, 1187, 1 };
    MenuSession session;
    const char* apButtons[3];
    int32_t choice;

    if ( !menu_Begin(&session) )
    {
        return 0;
    }
    apButtons[0] = menu_String("JONES_STR_EXIT3", "Return to Game");
    apButtons[1] = menu_String("JONES_STR_EXIT2", "Save and Quit");
    apButtons[2] = menu_String("JONES_STR_EXIT1", "Quit");
    choice = J3DMenuUI_Choice(session.pNSWindow, menu_String("JONES_STR_EXIT", "Exit"), NULL, apButtons, 3, 0, 0, J3DMENU_CHOICE_COLUMN, 0);
    *pResult = choice >= 0 && choice < 3 ? aResults[choice] : 2;
    if ( *pResult == 1187 && (!pSaveGameFilePath || !menu_PickSave(&session, 1, pSaveGameFilePath)) )
    {
        *pResult = 2; // no save was named: back to the game, as retail
    }
    menu_End(&session);
    return 1;
}

/* ---- Options ---------------------------------------------------------------------------------------------------------- */

// The options pages, each built from the game's settings into form items and read back from them. The in-game menus
// show one page; the launcher's Options window shows the four as tabs (J3DMenu_ShowOptions). Labels are copied here,
// as menu_String keeps only sixteen of its copies and a window of four pages needs more.
typedef struct MenuPage
{
    J3DMenuItem aItems[8];
    int numItems;
    char aaText[24][128];
    const char* apNames[3][8];  // the name lists of choice items
    int numText;
} MenuPage;

static const char* menu_PageText(MenuPage* pPage, const char* pName, const char* pFallback)
{
    char* pText;
    if ( pPage->numText >= (int)(sizeof(pPage->aaText) / sizeof(pPage->aaText[0])) )
    {
        return "";
    }
    pText = pPage->aaText[pPage->numText++];
    snprintf(pText, sizeof(pPage->aaText[0]), "%s", menu_String(pName, pFallback));
    return pText;
}

// Game play (template 112): difficulty, then the four switches, in the dialog's order.
static void menu_BuildGamePlay(MenuPage* pPage, const J3DMenuGamePlay* pOptions)
{
    static const char* const aDifficultyNames[6] = {
        "JONES_STR_HARD0", "JONES_STR_HARD1", "JONES_STR_HARD2", "JONES_STR_HARD3", "JONES_STR_HARD4", "JONES_STR_HARD5"
    };
    int numNames = pOptions->maxDifficulty + 1;
    int i;
    J3DMenuItem* aItems = pPage->aItems;

    memset(pPage, 0, sizeof(*pPage));
    numNames = numNames < 1 ? 1 : numNames > 6 ? 6 : numNames;
    for ( i = 0; i < numNames; ++i )
    {
        pPage->apNames[0][i] = menu_PageText(pPage, aDifficultyNames[i], aDifficultyNames[i]);
    }
    aItems[0].kind = J3DMENU_ITEM_STEPS;
    aItems[0].pGroup = menu_PageText(pPage, "JONES_STR_DFCLTY", "Difficulty");
    aItems[0].value = pOptions->difficulty < 0 ? 0 : pOptions->difficulty >= numNames ? numNames - 1 : pOptions->difficulty;
    aItems[0].apNames = pPage->apNames[0];
    aItems[0].numNames = numNames;
    aItems[0].bDisabled = numNames < 2;
    aItems[1].kind = J3DMENU_ITEM_TOGGLE;
    aItems[1].pGroup = menu_PageText(pPage, "JONES_STR_GMPLY_OPTS", "Game Play");
    aItems[1].pLabel = menu_PageText(pPage, "JONES_STR_HINTS", "Show Map Hints");
    aItems[1].value = pOptions->bShowHints != 0;
    aItems[2].kind = J3DMENU_ITEM_TOGGLE;
    aItems[2].pLabel = menu_PageText(pPage, "JONES_STR_MAPTURN", "Map Rotation");
    aItems[2].value = pOptions->bMapRotation != 0;
    aItems[3].kind = J3DMENU_ITEM_TOGGLE;
    aItems[3].pLabel = menu_PageText(pPage, "JONES_STR_RUNWALK", "Default to Run");
    aItems[3].value = pOptions->bDefaultRun != 0;
    aItems[4].kind = J3DMENU_ITEM_TOGGLE;
    aItems[4].pLabel = menu_PageText(pPage, "JONES_STR_TEXT", "Show Text");
    aItems[4].value = pOptions->bShowText != 0;
    pPage->numItems = 5;
}

static void menu_ReadGamePlay(const MenuPage* pPage, J3DMenuGamePlay* pOptions)
{
    pOptions->difficulty = pPage->aItems[0].value;
    pOptions->bShowHints = pPage->aItems[1].value;
    pOptions->bMapRotation = pPage->aItems[2].value;
    pOptions->bDefaultRun = pPage->aItems[3].value;
    pOptions->bShowText = pPage->aItems[4].value;
}

// Sound (template 113): the volume, heard when the slider is let go, and 3D sound. The dialog's reverse-stereo box is
// hidden in retail and is left out.
static J3DMenuSound* menu_pSound;

static int32_t menu_SoundChanged(J3DMenuItem* pItems, int32_t numItems, int32_t changedIndex)
{
    (void)numItems;
    if ( changedIndex == 0 && menu_pSound && menu_pSound->pfTestVolume )
    {
        menu_pSound->pfTestVolume(pItems[0].value);
    }
    return 0;
}

static void menu_BuildSound(MenuPage* pPage, const J3DMenuSound* pSettings)
{
    J3DMenuItem* aItems = pPage->aItems;
    memset(pPage, 0, sizeof(*pPage));
    pPage->apNames[0][0] = menu_PageText(pPage, "JONES_STR_LOW", "Low");
    pPage->apNames[0][1] = menu_PageText(pPage, "JONES_STR_MEDIUM", "Medium");
    pPage->apNames[0][2] = menu_PageText(pPage, "JONES_STR_HIGH", "High");
    aItems[0].kind = J3DMENU_ITEM_SLIDER;
    aItems[0].pGroup = menu_PageText(pPage, "JONES_STR_VOLUME", "Volume");
    aItems[0].value = pSettings->volume < 0 ? 0 : pSettings->volume > 100 ? 100 : pSettings->volume;
    aItems[0].maxValue = 100;
    aItems[0].apNames = pPage->apNames[0];
    aItems[0].numNames = 3;
    aItems[1].kind = J3DMENU_ITEM_TOGGLE;
    aItems[1].pGroup = "";
    aItems[1].pLabel = menu_PageText(pPage, "JONES_STR_3D_SND", "3D Sound");
    aItems[1].value = pSettings->b3DAvailable && pSettings->b3DSound;
    aItems[1].bDisabled = !pSettings->b3DAvailable;
    pPage->numItems = 2;
}

static void menu_ReadSound(const MenuPage* pPage, J3DMenuSound* pSettings)
{
    pSettings->volume = pPage->aItems[0].value;
    pSettings->b3DSound = pPage->aItems[1].value;
}

// Display (templates 114 and 148 as one page). Items: 0 resolution, 1 performance, 2 mip mapping, 3 fog, 4 fog density
// (greyed while fog is off), and in the launcher 5 fullscreen or a window.
static int menu_displayDefaultResolution;

static int32_t menu_DisplayChanged(J3DMenuItem* pItems, int32_t numItems, int32_t changedIndex)
{
    if ( changedIndex == -1 )
    {
        // The two dialogs' Default Settings buttons
        pItems[0].value = menu_displayDefaultResolution;
        pItems[1].value = 2;
        pItems[2].value = 1;
        pItems[3].value = 1;
        pItems[4].value = 100;
        if ( numItems > 5 )
        {
            pItems[5].value = 0; // fullscreen, retail's default
        }
    }
    pItems[4].bDisabled = !pItems[3].value;
    return 1;
}

static void menu_BuildDisplay(MenuPage* pPage, const J3DMenuDisplay* pSettings, int bWithWindowMode)
{
    static const char* const apFogCaptions[2] = { "0%", "100%" };
    J3DMenuItem* aItems = pPage->aItems;
    memset(pPage, 0, sizeof(*pPage));
    pPage->apNames[0][0] = menu_PageText(pPage, "JONES_STR_AVERAGE", "Average");
    pPage->apNames[0][1] = menu_PageText(pPage, "JONES_STR_FAST", "Fast");
    pPage->apNames[0][2] = menu_PageText(pPage, "JONES_STR_VFAST", "Very Fast");
    pPage->apNames[1][0] = menu_PageText(pPage, "JONES_STR_NO_MIPMAP", "None");
    pPage->apNames[1][1] = menu_PageText(pPage, "JONES_STR_BI_MIPMAP", "Bilinear");
    pPage->apNames[1][2] = menu_PageText(pPage, "JONES_STR_TRI_MIPMAP", "Trilinear");

    aItems[0].kind = J3DMENU_ITEM_STEPS;
    aItems[0].pGroup = menu_PageText(pPage, "JONES_STR_GMRES", "Game Resolution");
    aItems[0].value = pSettings->resolution < 0 || pSettings->resolution >= pSettings->numResolutions ? 0 : pSettings->resolution;
    aItems[0].apNames = pSettings->apResolutions;
    aItems[0].numNames = pSettings->numResolutions;
    aItems[0].bDisabled = pSettings->numResolutions < 2;
    aItems[1].kind = J3DMENU_ITEM_CHOICE;
    aItems[1].pGroup = menu_PageText(pPage, "JONES_STR_ADVDSPLY_SET", "Advanced Display Settings");
    aItems[1].pLabel = menu_PageText(pPage, "JONES_STR_SPEED", "Performance Level");
    aItems[1].value = pSettings->performance < 0 || pSettings->performance > 2 ? 2 : pSettings->performance;
    aItems[1].apNames = pPage->apNames[0];
    aItems[1].numNames = 3;
    aItems[2].kind = J3DMENU_ITEM_CHOICE;
    aItems[2].pLabel = menu_PageText(pPage, "JONES_STR_MIPMAP", "Mip Mapping");
    aItems[2].value = pSettings->filter < 0 || pSettings->filter > 2 ? 1 : pSettings->filter;
    aItems[2].apNames = pPage->apNames[1];
    aItems[2].numNames = 3;
    aItems[3].kind = J3DMENU_ITEM_TOGGLE;
    aItems[3].pGroup = menu_PageText(pPage, "JONES_STR_FOGDENSITY", "Fog Density");
    aItems[3].pLabel = menu_PageText(pPage, "JONES_STR_FOG", "Fog");
    aItems[3].value = pSettings->bFog != 0;
    aItems[4].kind = J3DMENU_ITEM_SLIDER;
    aItems[4].value = pSettings->fogDensity < 0 ? 0 : pSettings->fogDensity > 100 ? 100 : pSettings->fogDensity;
    aItems[4].maxValue = 100;
    aItems[4].apNames = apFogCaptions;
    aItems[4].numNames = 2;
    aItems[4].bDisabled = !aItems[3].value;
    pPage->numItems = 5;
    if ( bWithWindowMode )
    {
        // The developer dialog's "Open in a Window" (InWindow), which the Steam launcher did not offer (P8.7)
        pPage->apNames[2][0] = "Fullscreen";
        pPage->apNames[2][1] = "Window";
        aItems[5].kind = J3DMENU_ITEM_CHOICE;
        aItems[5].pGroup = "Screen";
        aItems[5].value = pSettings->bWindowMode != 0;
        aItems[5].apNames = pPage->apNames[2];
        aItems[5].numNames = 2;
        pPage->numItems = 6;
    }
    menu_displayDefaultResolution = pSettings->defaultResolution;
}

static void menu_ReadDisplay(const MenuPage* pPage, J3DMenuDisplay* pSettings)
{
    pSettings->resolution = pPage->aItems[0].value;
    pSettings->performance = pPage->aItems[1].value;
    pSettings->filter = pPage->aItems[2].value;
    pSettings->bFog = pPage->aItems[3].value;
    pSettings->fogDensity = pPage->aItems[4].value;
    if ( pPage->numItems > 5 )
    {
        pSettings->bWindowMode = pPage->aItems[5].value;
    }
}

// Controls (template 111's settings): the control set and the two switches.
static void menu_BuildControls(MenuPage* pPage, const J3DMenuControls* pControls)
{
    J3DMenuItem* aItems = pPage->aItems;
    memset(pPage, 0, sizeof(*pPage));
    aItems[0].kind = J3DMENU_ITEM_LIST;
    aItems[0].pGroup = menu_PageText(pPage, "JONES_STR_AVAIL_KEYCONFIG", "Control Sets");
    aItems[0].value = pControls->scheme < 0 || pControls->scheme >= pControls->numSchemes ? 0 : pControls->scheme;
    aItems[0].apNames = pControls->apSchemes;
    aItems[0].numNames = pControls->numSchemes;
    aItems[1].kind = J3DMENU_ITEM_TOGGLE;
    aItems[1].pGroup = "";
    aItems[1].pLabel = menu_PageText(pPage, "JONES_STR_CTRLMOUSE", "Enable Mouse");
    aItems[1].value = pControls->bMouse != 0;
    aItems[2].kind = J3DMENU_ITEM_TOGGLE;
    aItems[2].pLabel = menu_PageText(pPage, "JONES_STR_CTRLJOYSTICK", "Enable Gamepad/Joystick");
    aItems[2].value = pControls->bJoystickAvailable && pControls->bJoystick;
    aItems[2].bDisabled = !pControls->bJoystickAvailable;
    pPage->numItems = 3;
    if ( pControls->pfEditSets )
    {
        // The game's control options (sets viewed, created, edited, deleted), opened over this window
        aItems[3].kind = J3DMENU_ITEM_BUTTON;
        aItems[3].pGroup = "";
        aItems[3].pLabel = menu_PageText(pPage, "JONES_STR_CTRL_OPTS", "Control Options");
        pPage->numItems = 4;
    }
}

static J3DMenuControls* menu_pControls;

static int32_t menu_ControlsChanged(J3DMenuItem* pItems, int32_t numItems, int32_t changedIndex)
{
    (void)numItems;
    if ( changedIndex != 3 || !menu_pControls || !menu_pControls->pfEditSets )
    {
        return 0;
    }
    menu_pControls->pfEditSets(menu_pControls); // saves what it changes, and fills the struct in again
    pItems[0].value = menu_pControls->scheme;
    pItems[0].apNames = menu_pControls->apSchemes;
    pItems[0].numNames = menu_pControls->numSchemes;
    pItems[1].value = menu_pControls->bMouse != 0;
    pItems[2].value = menu_pControls->bJoystickAvailable && menu_pControls->bJoystick;
    return 1;
}

static void menu_ReadControls(const MenuPage* pPage, J3DMenuControls* pControls)
{
    pControls->scheme = pPage->aItems[0].value;
    pControls->bMouse = pPage->aItems[1].value;
    pControls->bJoystick = pPage->aItems[2].value;
}

// One page as an in-game menu. Returns what J3DMenuUI_Form did, -1 when no menu could be shown.
static int menu_ShowPage(MenuPage* pPage, const char* pTitleName, const char* pTitleFallback, const char* pDefaultsLabel,
                         J3DMenuFormChanged pfChanged)
{
    MenuSession session;
    int result;
    char aTitle[128];
    if ( !menu_Begin(&session) )
    {
        return -1;
    }
    snprintf(aTitle, sizeof(aTitle), "%s", menu_String(pTitleName, pTitleFallback));
    result = J3DMenuUI_Form(session.pNSWindow, aTitle, pPage->aItems, pPage->numItems, menu_String("JONES_STR_OK", "OK"),
                            menu_String("JONES_STR_CANCEL", "Cancel"), pDefaultsLabel, pfChanged);
    menu_End(&session);
    return result;
}

int J3DMenu_ShowGamePlayOptions(J3DMenuGamePlay* pOptions, int* pResult)
{
    static MenuPage page;
    int result;
    if ( !pOptions || !J3DMenu_IsAvailable() )
    {
        return 0;
    }
    menu_BuildGamePlay(&page, pOptions);
    result = menu_ShowPage(&page, "JONES_STR_GMPLY_OPTS", "Game Play Options", NULL, NULL);
    if ( result < 0 )
    {
        return 0;
    }
    if ( result == 1 )
    {
        menu_ReadGamePlay(&page, pOptions);
    }
    *pResult = result == 1 ? 1 : 2;
    return 1;
}

int J3DMenu_ShowSoundSettings(J3DMenuSound* pSettings, int* pResult)
{
    static MenuPage page;
    int result;
    if ( !pSettings || !J3DMenu_IsAvailable() )
    {
        return 0;
    }
    menu_BuildSound(&page, pSettings);
    menu_pSound = pSettings;
    result = menu_ShowPage(&page, "JONES_STR_SND_OPTS", "Sound Options", NULL, menu_SoundChanged);
    menu_pSound = NULL;
    if ( result < 0 )
    {
        return 0;
    }
    if ( result == 1 )
    {
        menu_ReadSound(&page, pSettings);
    }
    *pResult = result == 1 ? 1 : 2;
    return 1;
}

int J3DMenu_ShowDisplaySettings(J3DMenuDisplay* pSettings, int* pResult)
{
    static MenuPage page;
    char aDefaults[128];
    int result;
    if ( !pSettings || pSettings->numResolutions <= 0 || !J3DMenu_IsAvailable() )
    {
        return 0;
    }
    menu_BuildDisplay(&page, pSettings, 0);
    snprintf(aDefaults, sizeof(aDefaults), "%s", menu_String("JONES_STR_DEFAULTSET", "Default Settings"));
    result = menu_ShowPage(&page, "JONES_STR_DPLY_OPTS", "Display Options", aDefaults, menu_DisplayChanged);
    if ( result < 0 )
    {
        return 0;
    }
    if ( result == 1 )
    {
        menu_ReadDisplay(&page, pSettings);
    }
    *pResult = result == 1 ? 1 : 2;
    return 1;
}

/* ---- The launcher ------------------------------------------------------------------------------------------------------ */

static int menu_bQuitAtLaunch;
static void* menu_pLauncherWindow; // the launcher's NSWindow while it is up, to put Options in front of
static const J3DMenuLaunch* menu_pLaunch;

int J3DMenu_ShowOptions(J3DMenuDisplay* pDisplay, J3DMenuGamePlay* pGamePlay, J3DMenuSound* pSound, J3DMenuControls* pControls,
                        int* pResult)
{
    static MenuPage aPages[4];
    J3DMenuFormPage aFormPages[4];
    char aaTitles[4][64];
    char aWindowTitle[64];
    char aaButtons[3][64];
    int result;

    menu_BuildDisplay(&aPages[0], pDisplay, 1);
    menu_BuildGamePlay(&aPages[1], pGamePlay);
    menu_BuildSound(&aPages[2], pSound);
    menu_BuildControls(&aPages[3], pControls);
    // The Steam launcher's tab names: its Display, Game Play, Sound and Control Options dialogs
    snprintf(aaTitles[0], sizeof(aaTitles[0]), "Display");
    snprintf(aaTitles[1], sizeof(aaTitles[1]), "Game Play");
    snprintf(aaTitles[2], sizeof(aaTitles[2]), "Sound");
    snprintf(aaTitles[3], sizeof(aaTitles[3]), "Controls");
    snprintf(aWindowTitle, sizeof(aWindowTitle), "Options");
    snprintf(aaButtons[0], sizeof(aaButtons[0]), "%s", menu_String("JONES_STR_OK", "OK"));
    snprintf(aaButtons[1], sizeof(aaButtons[1]), "%s", menu_String("JONES_STR_CANCEL", "Cancel"));
    snprintf(aaButtons[2], sizeof(aaButtons[2]), "%s", menu_String("JONES_STR_DEFAULTSET", "Default Settings"));
    aFormPages[0] = (J3DMenuFormPage){ aaTitles[0], aPages[0].aItems, aPages[0].numItems, menu_DisplayChanged };
    aFormPages[1] = (J3DMenuFormPage){ aaTitles[1], aPages[1].aItems, aPages[1].numItems, NULL };
    aFormPages[2] = (J3DMenuFormPage){ aaTitles[2], aPages[2].aItems, aPages[2].numItems, menu_SoundChanged };
    aFormPages[3] = (J3DMenuFormPage){ aaTitles[3], aPages[3].aItems, aPages[3].numItems, pControls->pfEditSets ? menu_ControlsChanged : NULL };

    menu_pSound = pSound;
    menu_pControls = pControls;
    result = J3DMenuUI_OptionsWindow(menu_pLauncherWindow, aWindowTitle, aFormPages, 4, aaButtons[0], aaButtons[1], aaButtons[2]);
    menu_pSound = NULL;
    if ( result == 1 )
    {
        menu_ReadDisplay(&aPages[0], pDisplay);
        menu_ReadGamePlay(&aPages[1], pGamePlay);
        menu_ReadSound(&aPages[2], pSound);
        menu_ReadControls(&aPages[3], pControls);
    }
    *pResult = result == 1 ? 1 : 2;
    return 1;
}

static void menu_LauncherOptions(void* pWindow)
{
    menu_pLauncherWindow = pWindow;
    if ( menu_pLaunch && menu_pLaunch->pfOptions )
    {
        menu_pLaunch->pfOptions();
    }
    menu_pLauncherWindow = NULL;
}

// The help file: the manual the Steam and GOG releases ship beside the game, else none.
static int menu_FindHelp(char* pPath, size_t size)
{
    static const char* const aNames[] = { "manual.pdf", "Manual.pdf", "MANUAL.PDF" };
    size_t i;
    for ( i = 0; i < sizeof(aNames) / sizeof(aNames[0]); ++i )
    {
        struct stat st;
        snprintf(pPath, size, "%s%s", J3DHost_GetDataRoot(), aNames[i]);
        if ( stat(pPath, &st) == 0 && S_ISREG(st.st_mode) )
        {
            return 1;
        }
    }
    return 0;
}

// The launch picture: bitmap 141 of the Steam release's Jones3D.exe, else the CD and GOG releases' Install/Launch.bmp,
// both in the player's data folder; NULL for the plain title card. The caller frees it.
static uint8_t* menu_LoadLaunchPicture(size_t* pSize)
{
    char aPath[1100];
    uint8_t* pBmp = NULL;
    FILE* f;
    long size;
    if ( J3DGui_ReadExeBitmap("Jones3D.exe", 141, &pBmp, pSize) )
    {
        return pBmp;
    }
    snprintf(aPath, sizeof(aPath), "%sInstall/Launch.bmp", J3DHost_GetDataRoot());
    f = J3DHost_fopen(aPath, "rb");
    if ( !f )
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if ( size > 54 && size < (8 << 20) )
    {
        pBmp = malloc((size_t)size);
        if ( pBmp && fread(pBmp, 1, (size_t)size, f) == (size_t)size )
        {
            *pSize = (size_t)size;
        }
        else
        {
            free(pBmp);
            pBmp = NULL;
        }
    }
    fclose(f);
    return pBmp;
}

int J3DMenu_QuitAtLaunch(void)
{
    return menu_bQuitAtLaunch;
}

int J3DMenu_OpenHelp(void)
{
    char aPath[1100];
    char* aArgs[] = { "/usr/bin/open", aPath, NULL };
    pid_t pid;
    int status;

    // Not in a run nobody watches: a test or a replay must not open a viewer
    if ( !J3DMenu_IsAvailable() || !menu_FindHelp(aPath, sizeof(aPath)) )
    {
        return 0;
    }

    // open(1) hands the file to its viewer and returns at once, as ShellExecuteEx did
    if ( posix_spawn(&pid, aArgs[0], NULL, NULL, aArgs, environ) != 0 )
    {
        return 0;
    }
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int J3DMenu_RunLauncher(const J3DMenuLaunch* pLaunch, int* pChoice)
{
    J3DMenuLauncherUI ui;
    SDL_Window* pWindow = NULL;
    const char* pMode = getenv("J3D_MENUS");
    const char* pTestLaunch = getenv("J3D_MENU_TEST_LAUNCH");
    char aHelp[1100];
    char aStatus[1200];
    size_t pictureSize = 0;
    uint8_t* pPicture;

    // Not for runs that start themselves: tests and tools set the start mode, replays and scripts hold their own input
    if ( J3DDisplay_IsHeadless() || J3DRecord_IsReplaying() || getenv("J3D_INPUT_SCRIPT") || getenv("J3D_REG_Start_Mode")
         || getenv("J3D_NO_LAUNCHER") || (pMode && strcmp(pMode, "classic") == 0) )
    {
        return 0;
    }
    (void)pWindow;

    J3DMenuUI_SetPadPoll(menu_PadPoll);
    menu_bPadPrime = 1;
    pPicture = menu_LoadLaunchPicture(&pictureSize);
    memset(&ui, 0, sizeof(ui));
    ui.pTitle = "Indiana Jones and the Infernal Machine";
    ui.pPicture = pPicture;
    ui.pictureSize = pictureSize;
    // The Steam launcher's buttons, in its order (its Exit is Quit on the Mac)
    ui.apLabels[0] = "Start New Game";
    ui.apLabels[1] = "Resume Play";
    ui.apLabels[2] = "Help";
    ui.apLabels[3] = "Options";
    ui.apLabels[4] = "Quit";
    ui.pResumeDetail = pLaunch->pResumeDetail;
    snprintf(aStatus, sizeof(aStatus), "Game data: %s", J3DHost_GetDataRoot());
    ui.pStatus = aStatus;
    ui.pVersion = "Mac port";
    ui.pfOptions = menu_LauncherOptions;
    ui.pHelpPath = menu_FindHelp(aHelp, sizeof(aHelp)) ? aHelp : NULL;

    menu_pLaunch = pLaunch;
    menu_bLauncherUp = 1;
    if ( getenv("J3D_MENU_TEST_OPTIONS") )
    {
        menu_LauncherOptions(NULL); // for tests: the Options window first, which J3D_MENU_AUTOCANCEL closes and _SHOT keeps
    }
    if ( pTestLaunch )
    {
        // For tests: answer as the player would have, without showing the window ("newgame" or "resume")
        *pChoice = strcmp(pTestLaunch, "resume") == 0 ? J3DMENU_LAUNCH_RESUME : J3DMENU_LAUNCH_NEWGAME;
    }
    else
    {
        *pChoice = J3DMenuUI_Launcher(&ui);
    }
    menu_pLaunch = NULL;
    menu_bLauncherUp = 0;
    menu_bQuitAtLaunch = *pChoice == J3DMENU_LAUNCH_QUIT;
    free(pPicture);
    // The keys that answered the launcher are not the game's
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
    SDL_ResetKeyboard();
    return 1;
}

/* ---- The store ------------------------------------------------------------------------------------------------------- */

#define MENU_STORE_ICON 32 // the item icons are 16 by 16 in the dialog's lists; their resources carry 32 by 32 too

_Static_assert(J3DMENU_UI_STORE_ADD == J3DMENU_STORE_ADD && J3DMENU_UI_STORE_RETURN == J3DMENU_STORE_RETURN
               && J3DMENU_UI_STORE_CLEAR == J3DMENU_STORE_CLEAR && J3DMENU_UI_STORE_DONE == J3DMENU_STORE_DONE,
               "the store's actions are numbered alike on both sides");

static J3DMenuStore* menu_pStore;
static J3DMenuStoreRowUI menu_aStoreRows[3][J3DMENU_STORE_MAXROWS];

static int32_t menu_StoreAction(int32_t action, int32_t index, J3DMenuStoreState* pState)
{
    int32_t bClose = 0;
    int i;
    if ( action != J3DMENU_UI_STORE_READ && menu_pStore->pfAction )
    {
        bClose = menu_pStore->pfAction(menu_pStore, action, index) != 0;
    }
    for ( i = 0; i < menu_pStore->numOwned; ++i )
    {
        menu_aStoreRows[0][i] = (J3DMenuStoreRowUI){ menu_pStore->aOwned[i].item, menu_pStore->aOwned[i].aName, menu_pStore->aOwned[i].value };
    }
    for ( i = 0; i < menu_pStore->numAvailable; ++i )
    {
        menu_aStoreRows[1][i] = (J3DMenuStoreRowUI){ menu_pStore->aAvailable[i].item, menu_pStore->aAvailable[i].aName, menu_pStore->aAvailable[i].value };
    }
    for ( i = 0; i < menu_pStore->numCart; ++i )
    {
        menu_aStoreRows[2][i] = (J3DMenuStoreRowUI){ menu_pStore->aCart[i].item, menu_pStore->aCart[i].aName, menu_pStore->aCart[i].value };
    }
    pState->pOwned = menu_aStoreRows[0];
    pState->numOwned = menu_pStore->numOwned;
    pState->pAvailable = menu_aStoreRows[1];
    pState->numAvailable = menu_pStore->numAvailable;
    pState->pCart = menu_aStoreRows[2];
    pState->numCart = menu_pStore->numCart;
    pState->total = menu_pStore->total;
    pState->balance = menu_pStore->balance;
    return bClose;
}

// Each item's icon, from the player's Indy3D.exe through j3dgui, which scales it to MENU_STORE_ICON pixels.
static uint8_t* menu_StoreIcons(const int* aIconIDs, int numItems)
{
    uint8_t* pIcons = calloc((size_t)numItems, MENU_STORE_ICON * MENU_STORE_ICON * 4);
    int i;
    if ( !pIcons )
    {
        return NULL;
    }
    for ( i = 0; i < numItems; ++i )
    {
        HICON hIcon = (HICON)LoadImageA(NULL, MAKEINTRESOURCE(aIconIDs[i]), IMAGE_ICON, MENU_STORE_ICON, MENU_STORE_ICON, 0);
        const uint32_t* pPixels;
        int width;
        int height;
        int x;
        int y;
        if ( !hIcon || !J3DGui_GetIconPixels(hIcon, &width, &height, &pPixels) || width <= 0 || height <= 0 )
        {
            if ( hIcon )
            {
                DestroyIcon(hIcon);
            }
            continue;
        }
        for ( y = 0; y < MENU_STORE_ICON; ++y )
        {
            for ( x = 0; x < MENU_STORE_ICON; ++x )
            {
                uint32_t c = pPixels[(y * height / MENU_STORE_ICON) * width + x * width / MENU_STORE_ICON];
                uint8_t* pOut = pIcons + ((size_t)i * MENU_STORE_ICON * MENU_STORE_ICON + (size_t)y * MENU_STORE_ICON + (size_t)x) * 4;
                pOut[0] = (uint8_t)(c >> 16);
                pOut[1] = (uint8_t)(c >> 8);
                pOut[2] = (uint8_t)c;
                pOut[3] = (uint8_t)(c >> 24);
            }
        }
        DestroyIcon(hIcon); // LoadImage's copy, scaled to the size asked for
    }
    return pIcons;
}

int J3DMenu_ShowStore(J3DMenuStore* pStore, int* pResult)
{
    MenuSession session;
    J3DMenuStoreUI ui;
    uint8_t* pIcons;
    char aaLabels[12][128];
    static const char* const aaNames[12][2] = {
        { "JONES_STR_STORE", "Trading Post" }, { "JONES_STR_CURRENT", "Current Inventory" },
        { "JONES_STR_AVAILABLE", "Available Items" }, { "JONES_STR_CARTITEMS", "Items to Buy" },
        { "JONES_STR_COUNT", "Count" }, { "JONES_STR_COST", "Cost" }, { "JONES_STR_BALANCE", "Total" },
        { "JONES_STR_CREDIT", "Balance" }, { "JONES_STR_ADD_ITEM", "Add Item" }, { "JONES_STR_RETURN_ITEM", "Return Item" },
        { "JONES_STR_CLEAR", "Clear" }, { "JONES_STR_DONE", "Done" }
    };
    int i;

    if ( !pStore || menu_pStore || !menu_Begin(&session) )
    {
        return 0;
    }
    for ( i = 0; i < 12; ++i )
    {
        snprintf(aaLabels[i], sizeof(aaLabels[i]), "%s", menu_String(aaNames[i][0], aaNames[i][1]));
    }
    pIcons = pStore->aIconIDs ? menu_StoreIcons(pStore->aIconIDs, pStore->numItems) : NULL;
    memset(&ui, 0, sizeof(ui));
    ui.pTitle = aaLabels[0];
    ui.pOwnedTitle = aaLabels[1];
    ui.pAvailableTitle = aaLabels[2];
    ui.pCartTitle = aaLabels[3];
    ui.pCountLabel = aaLabels[4];
    ui.pCostLabel = aaLabels[5];
    ui.pTotalLabel = aaLabels[6];
    ui.pBalanceLabel = aaLabels[7];
    ui.pAddLabel = aaLabels[8];
    ui.pReturnLabel = aaLabels[9];
    ui.pClearLabel = aaLabels[10];
    ui.pDoneLabel = aaLabels[11];
    ui.pIcons = pIcons;
    ui.iconSize = MENU_STORE_ICON;
    ui.numIcons = pIcons ? pStore->numItems : 0;

    menu_pStore = pStore;
    J3DMenuUI_Store(session.pNSWindow, &ui, menu_StoreAction);
    menu_pStore = NULL;
    free(pIcons);
    menu_End(&session);
    *pResult = 1;
    return 1;
}

/* ---- The controls ---------------------------------------------------------------------------------------------------- */

int J3DMenu_ShowControlOptions(J3DMenuControlOptions* pOptions, int* pResult)
{
    MenuSession session;
    char aaLabels[10][128];
    const char* apLabels[10];
    static const char* const aaNames[10][2] = {
        { "JONES_STR_CTRL_OPTS", "Control Options" }, { "JONES_STR_AVAIL_KEYCONFIG", "Control Sets" },
        { "JONES_STR_NEW_SCHEME", "New" }, { "JONES_STR_EDIT_SCHEME", "Edit" }, { "JONES_STR_VIEW_SCHEME", "View" },
        { "JONES_STR_DEL_SCHEME", "Delete" }, { "JONES_STR_CTRLMOUSE", "Enable Mouse" },
        { "JONES_STR_CTRLJOYSTICK", "Enable Gamepad/Joystick" }, { "JONES_STR_OK", "OK" }, { "JONES_STR_CANCEL", "Cancel" }
    };
    int i;
    if ( !pOptions || !menu_Begin(&session) )
    {
        return 0;
    }
    for ( i = 0; i < 10; ++i )
    {
        snprintf(aaLabels[i], sizeof(aaLabels[i]), "%s", menu_String(aaNames[i][0], aaNames[i][1]));
        apLabels[i] = aaLabels[i];
    }
    J3DMenuUI_ControlOptions(session.pNSWindow, apLabels, pOptions);
    menu_End(&session);
    *pResult = 1; // the caller's action callback knows how it closed
    return 1;
}

int J3DMenu_EditControlSet(J3DMenuControlSet* pSet, int* pResult)
{
    MenuSession session;
    char aaLabels[9][128];
    const char* apLabels[9];
    static const char* const aaNames[9][2] = {
        { "JONES_STR_KEYBOARD", "Keyboard" }, { "JONES_STR_CONTROLLER", "Controller" }, { "JONES_STR_ADD_ASGNMNT", "Add" },
        { "JONES_STR_EDIT_ASGNMNT", "Change" }, { "JONES_STR_DEL_ASGNMNT", "Remove" }, { "JONES_STR_DEFAULTSET", "Default Settings" },
        { "JONES_STR_SAVE_SCHEME", "Save" }, { "JONES_STR_CANCEL", "Cancel" }, { "JONES_STR_CLOSE", "Close" }
    };
    int i;
    if ( !pSet || !menu_Begin(&session) )
    {
        return 0;
    }
    for ( i = 0; i < 9; ++i )
    {
        snprintf(aaLabels[i], sizeof(aaLabels[i]), "%s", menu_String(aaNames[i][0], aaNames[i][1]));
        apLabels[i] = aaLabels[i];
    }
    J3DMenuUI_ControlSet(session.pNSWindow, apLabels, pSet);
    menu_End(&session);
    // Saved is the set's own business (bModified); the menu reports which button closed it through its callback
    *pResult = 1;
    return 1;
}

int J3DMenu_AskName(const char* pTitle, const char* pPrompt, char* pName, int size, int* pResult)
{
    MenuSession session;
    char aTitle[128];
    char aPrompt[256];
    char aOk[64];
    if ( !pName || size < 2 || !menu_Begin(&session) )
    {
        return 0;
    }
    snprintf(aTitle, sizeof(aTitle), "%s", menu_String(pTitle, pTitle));
    snprintf(aPrompt, sizeof(aPrompt), "%s", pPrompt ? pPrompt : "");
    snprintf(aOk, sizeof(aOk), "%s", menu_String("JONES_STR_OK", "OK"));
    *pResult = J3DMenuUI_AskName(session.pNSWindow, aTitle, aPrompt, aOk, menu_String("JONES_STR_CANCEL", "Cancel"), pName, size) ? 1 : 2;
    menu_End(&session);
    return 1;
}

static int (*menu_pfCaptureInput)(int dik);

static int32_t menu_CaptureInput(int32_t hidUsage)
{
    if ( !menu_pfCaptureInput )
    {
        return 0;
    }
    if ( hidUsage == 0 )
    {
        // The controllers: their state, without pumping the window events the menu is taking
        SDL_UpdateGamepads();
        SDL_UpdateJoysticks();
        return menu_pfCaptureInput(-1) != 0;
    }
    {
        const uint8_t dik = J3DHost_HidToDik((uint32_t)hidUsage);
        return dik ? menu_pfCaptureInput(dik) != 0 : 0;
    }
}

int J3DMenu_CaptureControl(const char* pPrompt, const char* pAction, int (*pfInput)(int dik), int* pResult)
{
    MenuSession session;
    char aPrompt[256];
    char aAction[256];
    char aTitle[128];
    int (*pfOuter)(int) = menu_pfCaptureInput;
    if ( !pfInput || !menu_Begin(&session) )
    {
        return 0;
    }
    snprintf(aPrompt, sizeof(aPrompt), "%s", pPrompt ? pPrompt : "");
    snprintf(aAction, sizeof(aAction), "%s", pAction ? pAction : "");
    snprintf(aTitle, sizeof(aTitle), "%s", menu_String("JONES_STR_CTRL_OPTS", "Control Options"));
    menu_pfCaptureInput = pfInput;
    *pResult = J3DMenuUI_Capture(session.pNSWindow, aTitle, aPrompt, aAction, menu_String("JONES_STR_SLOWCANCEL", "Cancel"), menu_CaptureInput) ? 1 : 2;
    menu_pfCaptureInput = pfOuter;
    menu_End(&session);
    return 1;
}

/* ---- Game over --------------------------------------------------------------------------------------------------------- */

// Template 150's three buttons, in its order: start the level again, load a save (the default), quit. There is no
// cancel, as the dialog handled no IDCANCEL; a menu closed unanswered (J3D_MENU_AUTOCANCEL) counts as Load.
int J3DMenu_ShowGameOver(int bCanRestart, int* pResult)
{
    static const int aResults[3] = { 1177, 1178, 1179 };
    MenuSession session;
    const char* apButtons[3];
    int32_t choice;

    if ( !menu_Begin(&session) )
    {
        return 0;
    }
    apButtons[0] = menu_String("JONES_STR_RESTART", "Restart Level");
    apButtons[1] = menu_String("JONES_STR_LOAD", "Load Game");
    apButtons[2] = menu_String("JONES_STR_DEAD", "Quit");
    choice = J3DMenuUI_Choice(session.pNSWindow, menu_String("JONES_STR_DEATH", "Game Over"), NULL, apButtons, 3, 1, -1,
                              J3DMENU_CHOICE_COLUMN, bCanRestart ? 0u : 1u);
    *pResult = aResults[choice >= 0 && choice < 3 ? choice : 1];
    menu_End(&session);
    return 1;
}

/* ---- Figures ----------------------------------------------------------------------------------------------------------- */

// A picture and its mask (24 or 32 bits, blue first) as R, G, B, A, the mask's white transparent. NULL when the
// picture cannot be read; the caller frees the result.
static uint8_t* menu_PictureFromBitmaps(HBITMAP hImage, HBITMAP hMask, int* pWidth, int* pHeight)
{
    BITMAP bmp;
    BITMAP mask;
    uint8_t* pPixels;
    int bMask;
    int x;
    int y;
    if ( !hImage || GetObject(hImage, sizeof(bmp), &bmp) != (int)sizeof(bmp) || !bmp.bmBits
         || (bmp.bmBitsPixel != 24 && bmp.bmBitsPixel != 32) || bmp.bmWidth <= 0 || bmp.bmHeight == 0 )
    {
        return NULL;
    }
    bMask = hMask && GetObject(hMask, sizeof(mask), &mask) == (int)sizeof(mask) && mask.bmBits
            && mask.bmWidth == bmp.bmWidth && mask.bmHeight == bmp.bmHeight && (mask.bmBitsPixel == 24 || mask.bmBitsPixel == 32);
    *pWidth = bmp.bmWidth;
    *pHeight = bmp.bmHeight < 0 ? -bmp.bmHeight : bmp.bmHeight;
    pPixels = malloc((size_t)*pWidth * (size_t)*pHeight * 4);
    if ( !pPixels )
    {
        return NULL;
    }
    for ( y = 0; y < *pHeight; ++y )
    {
        const uint8_t* pSrc = (const uint8_t*)bmp.bmBits + (size_t)y * (size_t)bmp.bmWidthBytes;
        const uint8_t* pMask = bMask ? (const uint8_t*)mask.bmBits + (size_t)y * (size_t)mask.bmWidthBytes : NULL;
        uint8_t* pDst = pPixels + (size_t)y * (size_t)*pWidth * 4;
        const int step = bmp.bmBitsPixel / 8;
        const int maskStep = bMask ? mask.bmBitsPixel / 8 : 0;
        for ( x = 0; x < *pWidth; ++x )
        {
            const int bClear = pMask && pMask[x * maskStep] > 127 && pMask[x * maskStep + 1] > 127 && pMask[x * maskStep + 2] > 127;
            pDst[x * 4 + 0] = bClear ? 0 : pSrc[x * step + 2];
            pDst[x * 4 + 1] = bClear ? 0 : pSrc[x * step + 1];
            pDst[x * 4 + 2] = bClear ? 0 : pSrc[x * step + 0];
            pDst[x * 4 + 3] = bClear ? 0 : 255;
        }
    }
    return pPixels;
}

int J3DMenu_ShowFigures(const char* pTitle, const J3DMenuFigurePage* pPages, int numPages, int startPage,
                        J3DMenuDrawPicture pfDrawPicture, int* pResult)
{
    MenuSession session;
    J3DMenuStatsPage* pMenuPages;
    char (*paaLabels)[J3DMENU_STATS_MAXROWS][128];
    uint8_t** ppPictures;
    J3DMenuStats stats;
    int i;
    int r;

    if ( !pPages || numPages <= 0 || !menu_Begin(&session) )
    {
        return 0;
    }
    pMenuPages = calloc((size_t)numPages, sizeof(*pMenuPages));
    ppPictures = calloc((size_t)numPages, sizeof(*ppPictures));
    paaLabels = calloc((size_t)numPages, sizeof(*paaLabels));
    if ( !pMenuPages || !ppPictures || !paaLabels )
    {
        free(paaLabels);
        free(pMenuPages);
        free(ppPictures);
        menu_End(&session);
        return 0;
    }
    for ( i = 0; i < numPages; ++i )
    {
        J3DMenuStatsPage* pPage = &pMenuPages[i];
        pPage->pTitle = pPages[i].aTitle[0] ? pPages[i].aTitle : NULL;
        pPage->iqPoints = pPages[i].iqPoints;
        pPage->numRows = pPages[i].numRows > J3DMENU_STATS_MAXROWS ? J3DMENU_STATS_MAXROWS : pPages[i].numRows;
        for ( r = 0; r < pPage->numRows; ++r )
        {
            // menu_String's copies do not outlive sixteen more calls, so each label is kept here
            snprintf(paaLabels[i][r], sizeof(paaLabels[i][r]), "%s", menu_String(pPages[i].apLabels[r], pPages[i].apLabels[r]));
            pPage->apLabels[r] = paaLabels[i][r];
            snprintf(pPage->aaValues[r], sizeof(pPage->aaValues[r]), "%s", pPages[i].aaValues[r]);
        }
        if ( pfDrawPicture )
        {
            void* hImage = NULL;
            void* hMask = NULL;
            int width = 0;
            int height = 0;
            pfDrawPicture(pPages[i].iqPoints, &hImage, &hMask);
            ppPictures[i] = menu_PictureFromBitmaps((HBITMAP)hImage, (HBITMAP)hMask, &width, &height);
            pPage->pPicture = ppPictures[i];
            pPage->pictureWidth = width;
            pPage->pictureHeight = height;
        }
    }
    memset(&stats, 0, sizeof(stats));
    stats.pPages = pMenuPages;
    stats.numPages = numPages;
    stats.startPage = startPage < 0 ? 0 : startPage >= numPages ? numPages - 1 : startPage;
    {
        char aTitle[256];
        char aClose[64];
        snprintf(aTitle, sizeof(aTitle), "%s", menu_String(pTitle, pTitle));
        snprintf(aClose, sizeof(aClose), "%s", menu_String("JONES_STR_CLOSE", "Close"));
        stats.pTitle = aTitle;
        stats.pCloseLabel = aClose;
        J3DMenuUI_Stats(session.pNSWindow, &stats);
    }
    for ( i = 0; i < numPages; ++i )
    {
        free(ppPictures[i]);
    }
    free(ppPictures);
    free(paaLabels);
    free(pMenuPages);
    menu_End(&session);
    *pResult = 1;
    return 1;
}

// Not in jonesConfig.h: the game reaches the store only from the level-completed dialog
int J3DAPI jonesConfig_ShowStoreDialog(HWND hWnd, int* pBalance, int* pItemsState, int a4);

/* ---- For tests --------------------------------------------------------------------------------------------------------- */


static int menu_TestNoInput(int dik)
{
    (void)dik;
    return 0;
}
void J3DMenu_AfterFrame(unsigned long numFrames)
{
    static long testFrame = -1;
    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE];
    int result;
    int bShown;
    if ( testFrame == -1 )
    {
        const char* pFrame = getenv("J3D_MENU_TEST");
        testFrame = pFrame ? strtol(pFrame, NULL, 10) : 0;
    }
    if ( testFrame <= 0 || numFrames != (unsigned long)testFrame )
    {
        return;
    }
    {
        // A save's picture, as the load menu gets it: whether it loaded and how bright it is (a black one is 0)
        J3DMenuSave* pSaves = NULL;
        int numSaves = menu_ListSaves(&pSaves);
        uint8_t* pPixels = malloc(J3DMENU_THUMB_BYTES);
        int i;
        for ( i = 0; i < numSaves && pPixels; ++i )
        {
            unsigned long sum = 0;
            int k;
            int bLoaded = menu_LoadThumb(pSaves[i].aName, pPixels);
            for ( k = 0; bLoaded && k < J3DMENU_THUMB_BYTES; k += 4 )
            {
                sum += pPixels[k] + pPixels[k + 1] + pPixels[k + 2];
            }
            printf("J3D_MENU_TEST picture of %s: loaded %d, mean %lu\n", pSaves[i].aName, bLoaded, sum / (J3DMENU_THUMB_BYTES / 4 * 3));
        }
        free(pPixels);
        free(pSaves);
    }
    result = 0;
    bShown = J3DMenu_ShowMessage("JONES_STR_SAVEGM", "The game could not be saved.", 141, &result);
    printf("J3D_MENU_TEST message: shown %d, result %d\n", bShown, result);
    result = 0;
    aPath[0] = 0;
    bShown = J3DMenu_ShowExitGame(aPath, &result);
    printf("J3D_MENU_TEST exit: shown %d, result %d\n", bShown, result);
    result = 0;
    bShown = J3DMenu_GetSaveGameFilePath(aPath, &result);
    printf("J3D_MENU_TEST save: shown %d, result %d\n", bShown, result);
    result = 0;
    bShown = J3DMenu_GetLoadGameFilePath(aPath, &result);
    printf("J3D_MENU_TEST load: shown %d, result %d\n", bShown, result);
    // The rest through jonesConfig, as the game opens them, so what they prepare runs too; unanswered, each cancels
    printf("J3D_MENU_TEST game play: result %d\n", jonesConfig_ShowGamePlayOptions(stdWin95_GetWindow()));
    {
        // The store as after the first level: everything on sale, a little of each carried (the state's layout:
        // jonesConfig_StoreInitItemList)
        int aState[64];
        int balance = 1400;
        int k;
        memset(aState, 0, sizeof(aState));
        for ( k = 0; k < 14; ++k )
        {
            aState[k] = 1 | ((k % 3) << 4);
        }
        printf("J3D_MENU_TEST store: result %d\n", jonesConfig_ShowStoreDialog(stdWin95_GetWindow(), &balance, aState, 11));
    }
    printf("J3D_MENU_TEST controls: result %d\n", jonesConfig_ShowControlOptions(stdWin95_GetWindow()));
    {
        // The set table and the wait for a key, with sample rows (the real ones come from jonesConfig.c)
        J3DMenuControlSet* pSet = calloc(1, sizeof(*pSet));
        int k;
        for ( k = 0; pSet && k < 12; ++k )
        {
            J3DMenuControlRow* pRow = &pSet->aRows[pSet->numRows++];
            snprintf(pRow->aAction, sizeof(pRow->aAction), "Action %d", k + 1);
            snprintf(pRow->aKeys[pRow->numKeys++].aName, sizeof(pRow->aKeys[0].aName), "%c", 'A' + k);
            if ( k % 3 == 0 )
            {
                snprintf(pRow->aKeys[pRow->numKeys++].aName, sizeof(pRow->aKeys[0].aName), "Right Shift");
            }
            if ( k % 2 == 0 )
            {
                snprintf(pRow->aButtons[pRow->numButtons++].aName, sizeof(pRow->aButtons[0].aName), "Button %d", k / 2 + 1);
            }
        }
        if ( pSet )
        {
            snprintf(pSet->aName, sizeof(pSet->aName), "My Keys");
            bShown = J3DMenu_EditControlSet(pSet, &result);
            printf("J3D_MENU_TEST control set: shown %d\n", bShown);
            free(pSet);
        }
        bShown = J3DMenu_CaptureControl("Press the next key", "JUMP", menu_TestNoInput, &result);
        printf("J3D_MENU_TEST capture: shown %d, result %d\n", bShown, result);
    }
    printf("J3D_MENU_TEST display: result %d\n", jonesConfig_ShowDisplaySettingsDialog(stdWin95_GetWindow(), JonesMain_GetDisplayEnvironment(), JonesMain_GetDisplaySettings()));
    {
        JonesSoundSettings sound;
        memset(&sound, 0, sizeof(sound));
        sound.maxSoundVolume = 0.5f;
        printf("J3D_MENU_TEST sound: result %d\n", jonesConfig_ShowSoundSettingsDialog(stdWin95_GetWindow(), &sound));
    }
    result = 0;
    bShown = J3DMenu_ShowGameOver(1, &result);
    printf("J3D_MENU_TEST game over: shown %d, result %d\n", bShown, result);
    {
        SithGameStatistics* pStatistics = sithGamesave_GetGameStatistics();
        if ( pStatistics && sithGamesave_LockGameStatistics() )
        {
            printf("J3D_MENU_TEST statistics: result %d\n", jonesConfig_ShowStatisticsDialog(stdWin95_GetWindow(), pStatistics));
            sithGamesave_UnlockGameStatistics();
        }
    }
}
