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

#include <platform/common/host_path.h>
#include <platform/common/host_record.h>
#include <platform/common/stdDisplay_common.h>

#include <dirent.h>
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
static void* menu_GameNSWindow(SDL_Window** ppWindow)
{
    const char* pMode = getenv("J3D_MENUS");
    SDL_Window* pWindow = (SDL_Window*)stdWin95_GetWindow();
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

static int menu_Begin(MenuSession* pSession)
{
    SDL_Window* pWindow = NULL;
    pSession->pNSWindow = menu_GameNSWindow(&pWindow);
    if ( !pSession->pNSWindow )
    {
        return 0;
    }
    pSession->pWindow = pWindow;
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

// The game play options (template 112): difficulty, then the four switches, in the dialog's order.
int J3DMenu_ShowGamePlayOptions(J3DMenuGamePlay* pOptions, int* pResult)
{
    static const char* const aDifficultyNames[6] = {
        "JONES_STR_HARD0", "JONES_STR_HARD1", "JONES_STR_HARD2", "JONES_STR_HARD3", "JONES_STR_HARD4", "JONES_STR_HARD5"
    };
    static const char* const aFallbacks[6] = { "Freshman", "Sophomore", "Junior", "Senior", "Graduate", "Professor" };
    MenuSession session;
    const char* apNames[6];
    J3DMenuItem aItems[5];
    int numNames;
    int i;

    if ( !pOptions || !menu_Begin(&session) )
    {
        return 0;
    }
    numNames = pOptions->maxDifficulty + 1;
    if ( numNames < 1 )
    {
        numNames = 1;
    }
    if ( numNames > 6 )
    {
        numNames = 6;
    }
    for ( i = 0; i < numNames; ++i )
    {
        apNames[i] = menu_String(aDifficultyNames[i], aFallbacks[i]);
    }

    memset(aItems, 0, sizeof(aItems));
    aItems[0].kind = J3DMENU_ITEM_STEPS;
    aItems[0].pGroup = menu_String("JONES_STR_DFCLTY", "Difficulty");
    aItems[0].value = pOptions->difficulty < 0 ? 0 : pOptions->difficulty >= numNames ? numNames - 1 : pOptions->difficulty;
    aItems[0].apNames = apNames;
    aItems[0].numNames = numNames;
    aItems[0].bDisabled = numNames < 2;

    aItems[1].kind = J3DMENU_ITEM_TOGGLE;
    aItems[1].pGroup = menu_String("JONES_STR_GMPLY_OPTS", "Game Play");
    aItems[1].pLabel = menu_String("JONES_STR_HINTS", "Show Map Hints");
    aItems[1].value = pOptions->bShowHints != 0;
    aItems[2].kind = J3DMENU_ITEM_TOGGLE;
    aItems[2].pLabel = menu_String("JONES_STR_MAPTURN", "Map Rotation");
    aItems[2].value = pOptions->bMapRotation != 0;
    aItems[3].kind = J3DMENU_ITEM_TOGGLE;
    aItems[3].pLabel = menu_String("JONES_STR_RUNWALK", "Default to Run");
    aItems[3].value = pOptions->bDefaultRun != 0;
    aItems[4].kind = J3DMENU_ITEM_TOGGLE;
    aItems[4].pLabel = menu_String("JONES_STR_TEXT", "Show Text");
    aItems[4].value = pOptions->bShowText != 0;

    if ( J3DMenuUI_Form(session.pNSWindow, menu_String("JONES_STR_GMPLY_OPTS", "Game Play Options"), aItems, 5,
                        menu_String("JONES_STR_OK", "OK"), menu_String("JONES_STR_CANCEL", "Cancel"), NULL, NULL) )
    {
        pOptions->difficulty = aItems[0].value;
        pOptions->bShowHints = aItems[1].value;
        pOptions->bMapRotation = aItems[2].value;
        pOptions->bDefaultRun = aItems[3].value;
        pOptions->bShowText = aItems[4].value;
        *pResult = 1;
    }
    else
    {
        *pResult = 2;
    }
    menu_End(&session);
    return 1;
}

// The sound settings (template 113): the volume, heard when the slider is let go, and 3D sound. The dialog's
// reverse-stereo box is hidden in retail and is left out.
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

int J3DMenu_ShowSoundSettings(J3DMenuSound* pSettings, int* pResult)
{
    MenuSession session;
    const char* apCaptions[3];
    J3DMenuItem aItems[2];

    if ( !pSettings || !menu_Begin(&session) )
    {
        return 0;
    }
    apCaptions[0] = menu_String("JONES_STR_LOW", "Low");
    apCaptions[1] = menu_String("JONES_STR_MEDIUM", "Medium");
    apCaptions[2] = menu_String("JONES_STR_HIGH", "High");

    memset(aItems, 0, sizeof(aItems));
    aItems[0].kind = J3DMENU_ITEM_SLIDER;
    aItems[0].pGroup = menu_String("JONES_STR_VOLUME", "Volume");
    aItems[0].value = pSettings->volume < 0 ? 0 : pSettings->volume > 100 ? 100 : pSettings->volume;
    aItems[0].maxValue = 100;
    aItems[0].apNames = apCaptions;
    aItems[0].numNames = 3;
    aItems[1].kind = J3DMENU_ITEM_TOGGLE;
    aItems[1].pGroup = "";
    aItems[1].pLabel = menu_String("JONES_STR_3D_SND", "3D Sound");
    aItems[1].value = pSettings->b3DAvailable && pSettings->b3DSound;
    aItems[1].bDisabled = !pSettings->b3DAvailable;

    menu_pSound = pSettings;
    if ( J3DMenuUI_Form(session.pNSWindow, menu_String("JONES_STR_SND_OPTS", "Sound Options"), aItems, 2,
                        menu_String("JONES_STR_OK", "OK"), menu_String("JONES_STR_CANCEL", "Cancel"), NULL, menu_SoundChanged) )
    {
        pSettings->volume = aItems[0].value;
        pSettings->b3DSound = aItems[1].value;
        *pResult = 1;
    }
    else
    {
        *pResult = 2;
    }
    menu_pSound = NULL;
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

/* ---- For tests --------------------------------------------------------------------------------------------------------- */

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
