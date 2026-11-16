/*
 * wkernel over SDL3 (PORT-PLAN.md P2.2): the game window and its message queue.
 *
 * Retail wkernel.c created a Win32 window and ran the game from a GetMessage/DispatchMessage loop whose window procedure
 * (wkernel_MainWndProc) handled WM_CLOSE and WM_DESTROY itself and passed everything else to the procedure the game
 * installs (JonesMain_GameWndProc or JonesMain_IntroWndProc). This file keeps that structure and its return values:
 *
 *   - The window is an SDL window; its HWND is the SDL_Window pointer, stored with stdWin95_SetWindow as before.
 *   - The message queue holds posted messages (PostMessage, which the game uses to close itself) and the messages
 *     translated from SDL events, in arrival order. WM_PAINT and WM_TIMER are generated when the queue is empty and
 *     WM_QUIT once PostQuitMessage has been called, as GetMessage did.
 *   - wkernel_PeekProcessEvents dispatches everything waiting; wkernel_ProcessEvents first waits for a message (the
 *     game uses it while it is deactivated).
 *
 * SDL events become these messages:
 *
 *   key down/up          WM_KEYDOWN/WM_KEYUP (WM_SYSKEYDOWN/WM_SYSKEYUP with Alt held, for Alt itself, and for F10),
 *                        wParam the VK_ code, lParam the repeat count, the scan code (bits 16-23) and extended flag
 *                        (bit 24) from the DIK table (host_keys.h, P3.3), and the transition bits.
 *   text input           WM_CHAR per character in the Windows-1252 range. Backspace, Tab, Return and Escape produce
 *                        their WM_CHAR from the key, as TranslateMessage did, since SDL reports no text for them.
 *   mouse                WM_MOUSEMOVE, WM_[LRM]BUTTONDOWN/UP and WM_MOUSEWHEEL, in window coordinates.
 *   focus gained/lost    WM_ACTIVATEAPP and WM_ACTIVATE, in Windows' order, on a change only: the window starts
 *                        active, as retail's did before the game installed its window procedure.
 *   resized, minimized,  WM_SIZE with SIZE_RESTORED, SIZE_MINIMIZED or SIZE_MAXIMIZED and the window size.
 *   maximized, restored
 *   exposed              WM_PAINT.
 *   close requested,     WM_CLOSE: wkernel_MainWndProc shuts the game down, destroys the window (WM_DESTROY), and
 *   quit (Cmd-Q, SIGINT) WM_DESTROY posts the quit that ends the loop.
 *
 * Every SDL event is also handed to stdControl (stdControl_sdl.h) first: the mouse buttons and wheel, which it reads as
 * DirectInput's buffered data, and controllers connecting and disconnecting.
 *
 * Input recording and replay (host_record.h, P3.10): each pass of the pump is marked in a recording, and every message
 * that came from outside the game (input, activation, timers, close) is recorded as it is dispatched. A replay takes
 * those from the recording instead: SDL's input and focus events and the timers are ignored, and the recorded messages
 * of each pass are dispatched after anything the game posted itself, as they were. An input script's vkey events are
 * posted at the pass of their frame. When the replay ends the window is closed as a --frames run's is.
 *
 * SetTimer and KillTimer work for the game window, on the engine's millisecond clock (host_clock.h), which is what the
 * quicksave message uses. Timers on other windows belong to the dialogs of Phase 6 and are refused.
 *
 * J3D_FRAMES=<n> (jones3d --frames <n>) ends the run after n passes of the game loop by delivering WM_CLOSE, and
 * wkernel_Run then returns 0, as the null backend does. J3D_HEADLESS selects SDL's "dummy" video driver unless
 * SDL_VIDEO_DRIVER names one, so a headless run opens no window but runs this same event loop.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <platform/common/j3dmenu.h>

#include <Windows.h>

#include <wkernel/wkernel.h>
#include <Jones3D/Main/JonesMain.h>
#include <w32util/wuRegistry.h>
#include <std/Win95/stdWin95.h>

#include <platform/apple/stdControl_sdl.h>
#include <platform/apple/stdDisplay_sdl.h>
#include <platform/common/host_audio.h>
#include <platform/common/host_keys.h>
#include <platform/common/host_record.h>
#include <platform/common/host_screenshot.h>
#include <platform/common/j3dgui/j3dgui.h>
#include <platform/common/stdDisplay_common.h>
#include <std/Win95/stdDisplay.h>

#define WKERNEL_WINDOW_WIDTH  640
#define WKERNEL_WINDOW_HEIGHT 480

#define WKERNEL_QUEUESIZE 256 // messages; SDL's own queue holds the rest
#define WKERNEL_MAXTIMERS 8

// wkernel_GetMessage results
#define WKERNEL_MSG_NONE  -1
#define WKERNEL_MSG_QUIT   0
#define WKERNEL_MSG_OK     1

typedef struct WkernelTimer
{
    int      bUsed;
    UINT_PTR id;
    UINT     elapse;
    DWORD    dueTime;
} WkernelTimer;

static const GUID wkernel_guid = { 0x82CE4DA0, 0x9CBF, 0x11D1, { 0x90, 0x85, 0x00, 0x60, 0x97, 0x76, 0x0EA, 0x02 } };

static WKERNELWNDPROC wkernel_pfWndProc = NULL;
static SDL_Window*    wkernel_pWindow   = NULL;
static HWND           wkernel_hwnd      = NULL;

static MSG    wkernel_aQueue[WKERNEL_QUEUESIZE];
static int    wkernel_aQueueExternal[WKERNEL_QUEUESIZE]; // the message came from outside the game (recorded)
static size_t wkernel_queueHead  = 0;
static size_t wkernel_queueCount = 0;

static WkernelTimer wkernel_aTimers[WKERNEL_MAXTIMERS];

static int wkernel_bActive       = 1; // the window starts active (see the header comment)
static int wkernel_bMinimized    = 0;
static int wkernel_bPaintPending = 0;
static int wkernel_bQuit         = 0;

static LRESULT wkernel_MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* ---- Message queue ------------------------------------------------------------------------------------------------ */

static void wkernel_QueueMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, int bExternal)
{
    if ( wkernel_queueCount == WKERNEL_QUEUESIZE )
    {
        fprintf(stderr, "wkernel: message queue full, message 0x%04X dropped\n", uMsg);
        return;
    }

    const size_t index = (wkernel_queueHead + wkernel_queueCount) % WKERNEL_QUEUESIZE;
    wkernel_aQueueExternal[index] = bExternal;
    MSG* pMsg     = &wkernel_aQueue[index];
    pMsg->hwnd    = hWnd;
    pMsg->message = uMsg;
    pMsg->wParam  = wParam;
    pMsg->lParam  = lParam;
    pMsg->time    = GetTickCount();
    pMsg->pt.x    = 0;
    pMsg->pt.y    = 0;
    ++wkernel_queueCount;
}

// A message from outside the game: input, activation, the window's close button.
static void wkernel_Post(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    wkernel_QueueMessage(wkernel_hwnd, uMsg, wParam, lParam, 1);
}

static int wkernel_TakeQueuedMessage(MSG* pMsg, int* pbExternal)
{
    if ( !wkernel_queueCount )
    {
        return 0;
    }

    *pbExternal = wkernel_aQueueExternal[wkernel_queueHead];
    *pMsg = wkernel_aQueue[wkernel_queueHead];
    wkernel_queueHead = (wkernel_queueHead + 1) % WKERNEL_QUEUESIZE;
    --wkernel_queueCount;
    return 1;
}

/* ---- Keyboard ----------------------------------------------------------------------------------------------------- */

// The Windows virtual-key code for an SDL key, or 0 for keys the game's Windows build could not see. Letters follow
// the keyboard layout (the key's symbol), as Windows' VK codes do; every other key is mapped by position, as on a US
// keyboard.
static WPARAM wkernel_GetVirtualKey(const SDL_KeyboardEvent* pEvent)
{
    if ( pEvent->key >= SDLK_A && pEvent->key <= SDLK_Z )
    {
        return 'A' + (pEvent->key - SDLK_A);
    }

    const SDL_Scancode sc = pEvent->scancode;
    if ( sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z )
    {
        return 'A' + (sc - SDL_SCANCODE_A);
    }
    if ( sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9 )
    {
        return '1' + (sc - SDL_SCANCODE_1);
    }
    if ( sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12 )
    {
        return VK_F1 + (sc - SDL_SCANCODE_F1);
    }
    if ( sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9 )
    {
        return VK_NUMPAD1 + (sc - SDL_SCANCODE_KP_1);
    }

    switch ( sc )
    {
        case SDL_SCANCODE_0:              return '0';
        case SDL_SCANCODE_RETURN:         return VK_RETURN;
        case SDL_SCANCODE_ESCAPE:         return VK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE:      return VK_BACK;
        case SDL_SCANCODE_TAB:            return VK_TAB;
        case SDL_SCANCODE_SPACE:          return VK_SPACE;
        case SDL_SCANCODE_MINUS:          return VK_OEM_MINUS;
        case SDL_SCANCODE_EQUALS:         return VK_OEM_PLUS;
        case SDL_SCANCODE_LEFTBRACKET:    return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET:   return VK_OEM_6;
        case SDL_SCANCODE_BACKSLASH:      return VK_OEM_5;
        case SDL_SCANCODE_SEMICOLON:      return VK_OEM_1;
        case SDL_SCANCODE_APOSTROPHE:     return VK_OEM_7;
        case SDL_SCANCODE_GRAVE:          return VK_OEM_3;
        case SDL_SCANCODE_COMMA:          return VK_OEM_COMMA;
        case SDL_SCANCODE_PERIOD:         return VK_OEM_PERIOD;
        case SDL_SCANCODE_SLASH:          return VK_OEM_2;
        case SDL_SCANCODE_NONUSBACKSLASH: return VK_OEM_102;
        case SDL_SCANCODE_CAPSLOCK:       return VK_CAPITAL;
        case SDL_SCANCODE_PRINTSCREEN:    return VK_SNAPSHOT;
        case SDL_SCANCODE_SCROLLLOCK:     return VK_SCROLL;
        case SDL_SCANCODE_PAUSE:          return VK_PAUSE;
        case SDL_SCANCODE_INSERT:         return VK_INSERT;
        case SDL_SCANCODE_HOME:           return VK_HOME;
        case SDL_SCANCODE_PAGEUP:         return VK_PRIOR;
        case SDL_SCANCODE_DELETE:         return VK_DELETE;
        case SDL_SCANCODE_END:            return VK_END;
        case SDL_SCANCODE_PAGEDOWN:       return VK_NEXT;
        case SDL_SCANCODE_RIGHT:          return VK_RIGHT;
        case SDL_SCANCODE_LEFT:           return VK_LEFT;
        case SDL_SCANCODE_DOWN:           return VK_DOWN;
        case SDL_SCANCODE_UP:             return VK_UP;
        case SDL_SCANCODE_NUMLOCKCLEAR:   return VK_NUMLOCK;
        case SDL_SCANCODE_KP_DIVIDE:      return VK_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY:    return VK_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS:       return VK_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS:        return VK_ADD;
        case SDL_SCANCODE_KP_ENTER:       return VK_RETURN;
        case SDL_SCANCODE_KP_0:           return VK_NUMPAD0;
        case SDL_SCANCODE_KP_PERIOD:      return VK_DECIMAL;
        case SDL_SCANCODE_APPLICATION:    return VK_APPS;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:          return VK_CONTROL;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:         return VK_SHIFT;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:           return VK_MENU;
        case SDL_SCANCODE_LGUI:           return VK_LWIN;
        case SDL_SCANCODE_RGUI:           return VK_RWIN;
        default:                          return 0;
    }
}

// Keys a PC keyboard sends with the E0 prefix: lParam bit 24.
static int wkernel_IsExtendedKey(SDL_Scancode sc)
{
    switch ( sc )
    {
        case SDL_SCANCODE_RCTRL:
        case SDL_SCANCODE_RALT:
        case SDL_SCANCODE_INSERT:
        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_HOME:
        case SDL_SCANCODE_END:
        case SDL_SCANCODE_PAGEUP:
        case SDL_SCANCODE_PAGEDOWN:
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_KP_DIVIDE:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_NUMLOCKCLEAR:
        case SDL_SCANCODE_PRINTSCREEN:
        case SDL_SCANCODE_LGUI:
        case SDL_SCANCODE_RGUI:
        case SDL_SCANCODE_APPLICATION:
            return 1;
        default:
            return 0;
    }
}

// The character TranslateMessage made from a key that SDL reports no text for, or 0.
static WPARAM wkernel_GetControlChar(WPARAM vk)
{
    switch ( vk )
    {
        case VK_BACK:   return 0x08;
        case VK_TAB:    return 0x09;
        case VK_RETURN: return 0x0D;
        case VK_ESCAPE: return 0x1B;
        default:        return 0;
    }
}

// The fullscreen toggle (P2.19): Alt+Return, as on Windows, or Cmd+Ctrl+F, the Mac's own shortcut.
static int wkernel_IsFullscreenToggle(const SDL_KeyboardEvent* pEvent)
{
    const SDL_Keymod mod = pEvent->mod;
    if ( pEvent->scancode == SDL_SCANCODE_RETURN && (mod & SDL_KMOD_ALT) && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) )
    {
        return 1;
    }
    return pEvent->scancode == SDL_SCANCODE_F && (mod & SDL_KMOD_GUI) && (mod & SDL_KMOD_CTRL);
}

// Retail switched between fullscreen and a window only through its display settings dialog, which flips "InWindow",
// saves it and restarts the display. The toggle does the same through the game's own restart (JonesMain_Process
// handles JonesMain_RefreshDisplayDevice at the end of the frame), so the mode the settings name is kept.
static void wkernel_ToggleFullscreen(void)
{
    JonesDisplaySettings* pSettings = JonesMain_GetDisplaySettings();
    if ( !pSettings )
    {
        return;
    }
    pSettings->bWindowMode = !pSettings->bWindowMode;
    wuRegistry_SaveIntEx("InWindow", pSettings->bWindowMode);
    JonesMain_RefreshDisplayDevice();
}

static void wkernel_TranslateKey(const SDL_KeyboardEvent* pEvent)
{
    const WPARAM vk = wkernel_GetVirtualKey(pEvent);
    if ( !vk )
    {
        return;
    }

    const int bAlt = (pEvent->mod & SDL_KMOD_ALT) != 0;
    const int bSys = bAlt || vk == VK_MENU || vk == VK_F10;

    // Bits 0-15 repeat count, 16-23 scan code, 24 extended, 29 Alt held, 30 key was already down, 31 key is being
    // released. The scan code and extended flag are the key's DirectInput code (host_keys.h).
    DWORD keyData = 1;
    const uint8_t dik = J3DHost_HidToDik((uint32_t)pEvent->scancode);
    keyData |= (DWORD)(dik & 0x7F) << 16;
    if ( (dik & 0x80) || wkernel_IsExtendedKey(pEvent->scancode) )
    {
        keyData |= 1u << 24;
    }
    if ( bAlt && pEvent->down )
    {
        keyData |= 1u << 29;
    }
    if ( !pEvent->down || pEvent->repeat )
    {
        keyData |= 1u << 30;
    }
    if ( !pEvent->down )
    {
        keyData |= 1u << 31;
    }

    UINT uMsg;
    if ( pEvent->down )
    {
        uMsg = bSys ? WM_SYSKEYDOWN : WM_KEYDOWN;
    }
    else
    {
        uMsg = bSys ? WM_SYSKEYUP : WM_KEYUP;
    }
    wkernel_Post(uMsg, vk, (LPARAM)keyData);

    if ( pEvent->down && !bSys )
    {
        const WPARAM chr = wkernel_GetControlChar(vk);
        if ( chr )
        {
            wkernel_Post(WM_CHAR, chr, (LPARAM)keyData);
        }
    }
}

// The Windows-1252 code for a Unicode code point, or 0 if it has none. The game's fonts and strings are 1252.
static WPARAM wkernel_ToCodePage1252(Uint32 cp)
{
    if ( (cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp <= 0xFF) )
    {
        return cp;
    }

    static const struct { Uint32 cp; WPARAM chr; } aMap[] = {
        { 0x20AC, 0x80 }, { 0x201A, 0x82 }, { 0x0192, 0x83 }, { 0x201E, 0x84 }, { 0x2026, 0x85 }, { 0x2020, 0x86 },
        { 0x2021, 0x87 }, { 0x02C6, 0x88 }, { 0x2030, 0x89 }, { 0x0160, 0x8A }, { 0x2039, 0x8B }, { 0x0152, 0x8C },
        { 0x017D, 0x8E }, { 0x2018, 0x91 }, { 0x2019, 0x92 }, { 0x201C, 0x93 }, { 0x201D, 0x94 }, { 0x2022, 0x95 },
        { 0x2013, 0x96 }, { 0x2014, 0x97 }, { 0x02DC, 0x98 }, { 0x2122, 0x99 }, { 0x0161, 0x9A }, { 0x203A, 0x9B },
        { 0x0153, 0x9C }, { 0x017E, 0x9E }, { 0x0178, 0x9F },
    };
    for ( size_t i = 0; i < sizeof(aMap) / sizeof(aMap[0]); i++ )
    {
        if ( aMap[i].cp == cp )
        {
            return aMap[i].chr;
        }
    }

    return 0;
}

static void wkernel_TranslateText(const SDL_TextInputEvent* pEvent)
{
    const char* pText = pEvent->text;
    size_t      len   = strlen(pText);
    while ( len )
    {
        const WPARAM chr = wkernel_ToCodePage1252(SDL_StepUTF8(&pText, &len));
        if ( chr )
        {
            wkernel_Post(WM_CHAR, chr, 1);
        }
    }
}

/* ---- Mouse -------------------------------------------------------------------------------------------------------- */

static WPARAM wkernel_GetMouseKeyFlags(void)
{
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(NULL, NULL);
    const SDL_Keymod           mod     = SDL_GetModState();

    WPARAM flags = 0;
    if ( buttons & SDL_BUTTON_LMASK )
    {
        flags |= MK_LBUTTON;
    }
    if ( buttons & SDL_BUTTON_RMASK )
    {
        flags |= MK_RBUTTON;
    }
    if ( buttons & SDL_BUTTON_MMASK )
    {
        flags |= MK_MBUTTON;
    }
    if ( mod & SDL_KMOD_SHIFT )
    {
        flags |= MK_SHIFT;
    }
    if ( mod & SDL_KMOD_CTRL )
    {
        flags |= MK_CONTROL;
    }
    return flags;
}

// A mouse position as the game's messages carry it: back-buffer pixels (P2.15), packed as Win32 packs them.
static LPARAM wkernel_MakePointParam(float x, float y)
{
    J3DApple_WindowToBackBuffer(&x, &y);
    return MAKELPARAM((WORD)(int16_t)x, (WORD)(int16_t)y);
}

static void wkernel_TranslateMouseButton(const SDL_MouseButtonEvent* pEvent)
{
    UINT uMsg;
    switch ( pEvent->button )
    {
        case SDL_BUTTON_LEFT:   uMsg = pEvent->down ? WM_LBUTTONDOWN : WM_LBUTTONUP; break;
        case SDL_BUTTON_RIGHT:  uMsg = pEvent->down ? WM_RBUTTONDOWN : WM_RBUTTONUP; break;
        case SDL_BUTTON_MIDDLE: uMsg = pEvent->down ? WM_MBUTTONDOWN : WM_MBUTTONUP; break;
        default:                return;
    }

    wkernel_Post(uMsg, wkernel_GetMouseKeyFlags(), wkernel_MakePointParam(pEvent->x, pEvent->y));
}

static void wkernel_TranslateMouseWheel(const SDL_MouseWheelEvent* pEvent)
{
    // WHEEL_DELTA (120) per notch, positive away from the user.
    float y = pEvent->y;
    if ( pEvent->direction == SDL_MOUSEWHEEL_FLIPPED )
    {
        y = -y;
    }

    const int16_t delta = (int16_t)(y * 120.0f);
    if ( delta )
    {
        wkernel_Post(WM_MOUSEWHEEL, MAKEWPARAM(wkernel_GetMouseKeyFlags(), (WORD)delta),
                     wkernel_MakePointParam(pEvent->mouse_x, pEvent->mouse_y));
    }
}

/* ---- Window ------------------------------------------------------------------------------------------------------- */

static void wkernel_SetActive(int bActive)
{
    if ( bActive == wkernel_bActive )
    {
        return;
    }

    wkernel_bActive = bActive;
    J3DControl_SetWindowFocus(bActive); // the mouse is the game's only while the window has focus (P3.9)
    J3DAudio_SetFocus(bActive);         // and only buffers made with global focus are heard in the background (P4.9)
    if ( bActive )
    {
        wkernel_Post(WM_ACTIVATEAPP, TRUE, 0);
        wkernel_Post(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, wkernel_bMinimized), 0);
    }
    else
    {
        wkernel_Post(WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, wkernel_bMinimized), 0);
        wkernel_Post(WM_ACTIVATEAPP, FALSE, 0);
    }
}

static void wkernel_PostSize(WPARAM type)
{
    int width  = 0;
    int height = 0;
    SDL_GetWindowSize(wkernel_pWindow, &width, &height);
    wkernel_Post(WM_SIZE, type, MAKELPARAM((WORD)width, (WORD)height));
}

// The events a replay takes from the recording instead.
static int wkernel_IsRecordedEvent(Uint32 type)
{
    switch ( type )
    {
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return 1;
        default:
            return 0;
    }
}

static void wkernel_TranslateEvent(const SDL_Event* pEvent)
{
    if ( !wkernel_pWindow )
    {
        return; // destroyed: nothing is listening any more
    }

    J3DControl_HandleEvent(pEvent);

    if ( J3DRecord_IsReplaying() && wkernel_IsRecordedEvent(pEvent->type) )
    {
        // The fullscreen toggle still works while watching a replay; it is not game input.
        if ( pEvent->type == SDL_EVENT_KEY_DOWN && !pEvent->key.repeat && wkernel_IsFullscreenToggle(&pEvent->key) )
        {
            wkernel_ToggleFullscreen();
        }
        return;
    }

    switch ( pEvent->type )
    {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            wkernel_Post(WM_CLOSE, 0, 0);
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            wkernel_SetActive(1);
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            wkernel_SetActive(0);
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
            wkernel_bMinimized = 1;
            wkernel_Post(WM_SIZE, SIZE_MINIMIZED, 0);
            break;

        case SDL_EVENT_WINDOW_MAXIMIZED:
            wkernel_bMinimized = 0;
            wkernel_PostSize(SIZE_MAXIMIZED);
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            wkernel_bMinimized = 0;
            wkernel_PostSize(SIZE_RESTORED);
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            wkernel_PostSize(SIZE_RESTORED);
            break;

        case SDL_EVENT_WINDOW_EXPOSED:
            wkernel_bPaintPending = 1;
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if ( wkernel_IsFullscreenToggle(&pEvent->key) )
            {
                if ( pEvent->type == SDL_EVENT_KEY_DOWN && !pEvent->key.repeat )
                {
                    wkernel_ToggleFullscreen();
                }
                break; // the game never sees the combination
            }
            wkernel_TranslateKey(&pEvent->key);
            break;

        case SDL_EVENT_TEXT_INPUT:
            wkernel_TranslateText(&pEvent->text);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            wkernel_Post(WM_MOUSEMOVE, wkernel_GetMouseKeyFlags(),
                         wkernel_MakePointParam(pEvent->motion.x, pEvent->motion.y));
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            wkernel_TranslateMouseButton(&pEvent->button);
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            wkernel_TranslateMouseWheel(&pEvent->wheel);
            break;

        default:
            break;
    }
}

static void wkernel_DestroyWindow(void)
{
    if ( !wkernel_pWindow )
    {
        return;
    }

    // DestroyWindow sent WM_DESTROY while the window still existed.
    wkernel_MainWndProc(wkernel_hwnd, WM_DESTROY, 0, 0);

    SDL_StopTextInput(wkernel_pWindow);
    SDL_DestroyWindow(wkernel_pWindow);
    wkernel_pWindow = NULL;
    memset(wkernel_aTimers, 0, sizeof(wkernel_aTimers));
}

/* ---- GetMessage --------------------------------------------------------------------------------------------------- */

// Milliseconds until the next timer is due, or -1 when no timer is set.
static Sint32 wkernel_GetTimerWait(DWORD now)
{
    Sint32 wait = -1;
    for ( size_t i = 0; i < WKERNEL_MAXTIMERS; i++ )
    {
        if ( wkernel_aTimers[i].bUsed )
        {
            const int32_t remaining = (int32_t)(wkernel_aTimers[i].dueTime - now); // wraps like GetTickCount
            const Sint32  timerWait = remaining > 0 ? remaining : 0;
            if ( wait < 0 || timerWait < wait )
            {
                wait = timerWait;
            }
        }
    }
    return wait;
}

static int wkernel_TakeTimerMessage(MSG* pMsg)
{
    const DWORD now = GetTickCount();
    for ( size_t i = 0; i < WKERNEL_MAXTIMERS; i++ )
    {
        WkernelTimer* pTimer = &wkernel_aTimers[i];
        if ( pTimer->bUsed && (int32_t)(pTimer->dueTime - now) <= 0 )
        {
            pTimer->dueTime = now + pTimer->elapse;

            memset(pMsg, 0, sizeof(*pMsg));
            pMsg->hwnd    = wkernel_hwnd;
            pMsg->message = WM_TIMER;
            pMsg->wParam  = pTimer->id;
            pMsg->time    = now;
            return 1;
        }
    }
    return 0;
}

// GetMessage (bWait) or PeekMessage with PM_REMOVE (not bWait). Returns WKERNEL_MSG_OK with *pMsg filled,
// WKERNEL_MSG_QUIT once the quit has been posted and nothing is left before it, or WKERNEL_MSG_NONE when not waiting
// and nothing is there.
static int wkernel_GetMessage(MSG* pMsg, int* pbExternal, int bWait)
{
    *pbExternal = 0;
    while ( 1 )
    {
        SDL_Event event;
        while ( SDL_PollEvent(&event) )
        {
            wkernel_TranslateEvent(&event);
        }

        if ( wkernel_TakeQueuedMessage(pMsg, pbExternal) )
        {
            return WKERNEL_MSG_OK;
        }

        if ( J3DRecord_IsReplaying() )
        {
            uint32_t msg;
            uint64_t wParam;
            int64_t  lParam;
            if ( J3DRecord_NextMessage(&msg, &wParam, &lParam) )
            {
                memset(pMsg, 0, sizeof(*pMsg));
                pMsg->hwnd    = wkernel_hwnd;
                pMsg->message = msg;
                pMsg->wParam  = (WPARAM)wParam;
                pMsg->lParam  = (LPARAM)lParam;
                pMsg->time    = GetTickCount();
                return WKERNEL_MSG_OK;
            }
        }

        if ( wkernel_bQuit )
        {
            return WKERNEL_MSG_QUIT;
        }

        if ( wkernel_bPaintPending && wkernel_pWindow )
        {
            wkernel_bPaintPending = 0;
            memset(pMsg, 0, sizeof(*pMsg));
            pMsg->hwnd    = wkernel_hwnd;
            pMsg->message = WM_PAINT;
            pMsg->time    = GetTickCount();
            return WKERNEL_MSG_OK;
        }

        if ( !J3DRecord_IsReplaying() && wkernel_TakeTimerMessage(pMsg) )
        {
            *pbExternal = 1;
            return WKERNEL_MSG_OK;
        }

        // A replay never waits: what the recorded pass received is already in the recording.
        if ( !bWait || J3DRecord_IsReplaying() )
        {
            return WKERNEL_MSG_NONE;
        }

        if ( SDL_WaitEventTimeout(&event, wkernel_GetTimerWait(GetTickCount())) )
        {
            wkernel_TranslateEvent(&event);
        }
    }
}

static int wkernel_IsInputMessage(UINT msg)
{
    return (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
}

static void wkernel_DispatchMessage(const MSG* pMsg)
{
    // While a dialog is open its keyboard and mouse are its own (PORT-PLAN.md Phase 6); the rest still reaches the game.
    if ( J3DGui_IsModalActive() && pMsg->hwnd == wkernel_hwnd && wkernel_IsInputMessage(pMsg->message) )
    {
        J3DGui_Input(pMsg->message, pMsg->wParam, pMsg->lParam);
        return;
    }

    // A message posted to no window (PostThreadMessage) or to a destroyed one reaches no window procedure.
    if ( pMsg->hwnd && pMsg->hwnd == wkernel_hwnd && wkernel_pWindow )
    {
        wkernel_MainWndProc(pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
    }
}

// The body of retail's wkernel_PeekProcessEvents and wkernel_ProcessEvents: 1 once WM_QUIT is retrieved, else 0.
static int wkernel_Pump(int bWait)
{
    J3DRecord_Pump();

    // The input script's key presses due by this frame (host_record.h).
    J3DScriptEvent event;
    while ( J3DRecord_TakeScriptEvent(1, &event) )
    {
        // A single press: repeat count 1, then the release (bits 30 and 31). No scan code: the game reads the VK code.
        wkernel_Post(WM_KEYDOWN, (WPARAM)event.a, 1);
        wkernel_Post(WM_KEYUP, (WPARAM)event.a, (LPARAM)(1u | (3u << 30)));
    }

    MSG msg;
    int bExternal;
    int result = wkernel_GetMessage(&msg, &bExternal, bWait);
    while ( result != WKERNEL_MSG_NONE )
    {
        if ( result == WKERNEL_MSG_QUIT )
        {
            return 1;
        }

        if ( bExternal && J3DRecord_IsRecording() )
        {
            J3DRecord_Message(msg.message, (uint64_t)msg.wParam, (int64_t)msg.lParam);
        }
        wkernel_DispatchMessage(&msg);
        result = wkernel_GetMessage(&msg, &bExternal, 0);
    }
    return 0;
}

/* ---- Window procedure --------------------------------------------------------------------------------------------- */

static LRESULT wkernel_MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int retValue;
    int bHandled = 0;
    int bProcess = 1;

    switch ( uMsg )
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            bProcess = 0;
            break;

        case WM_CLOSE:
            JonesMain_Shutdown();
            bProcess = 0;
            break;
    }

    if ( bProcess && wkernel_pfWndProc )
    {
        bHandled = wkernel_pfWndProc(hWnd, uMsg, wParam, lParam, &retValue);
        if ( bHandled )
        {
            return retValue;
        }
    }

    // DefWindowProc's one effect that matters here: WM_CLOSE destroys the window.
    if ( uMsg == WM_CLOSE )
    {
        wkernel_DestroyWindow();
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

/* ---- wkernel.h ---------------------------------------------------------------------------------------------------- */

static int wkernel_IsHeadless(void)
{
    const char* pValue = getenv("J3D_HEADLESS");
    return pValue && *pValue && *pValue != '0';
}

static int wkernel_CreateWindow(LPCSTR lpWindowName)
{
    if ( wkernel_IsHeadless() && !getenv("SDL_VIDEO_DRIVER") )
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    }

    // Fullscreen (stdDisplay_sdl.c) covers the display in place, as DirectDraw's exclusive mode did, rather than
    // moving the window to a Space of its own: a Spaces transition is animated, and macOS ignores it for an app that
    // is not frontmost, such as one started from a script. P2.19 revisits the fullscreen toggle.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");

    // A fullscreen game minimizes when another app takes focus, as DirectDraw's exclusive mode made it; clicking it in
    // the Dock restores it and its activation brings fullscreen back (stdDisplay_Refresh). P2.19.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "1");

    if ( !SDL_Init(SDL_INIT_VIDEO) )
    {
        fprintf(stderr, "wkernel: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // A Metal window, for the presenter (stdDisplay_sdl.c), on any driver that has Metal views; the dummy driver has none.
    const char*     pDriver = SDL_GetCurrentVideoDriver();
    // High pixel density: on a Retina display the drawable has the display's real pixels, so a mode at the native size
    // is drawn 1:1 (P2.15). Window coordinates stay in points; mouse positions are converted (stdDisplay_sdl.h).
    SDL_WindowFlags flags   = SDL_WINDOW_HIGH_PIXEL_DENSITY | (pDriver && strcmp(pDriver, "dummy") != 0 ? SDL_WINDOW_METAL : 0);
    wkernel_pWindow = SDL_CreateWindow(lpWindowName, WKERNEL_WINDOW_WIDTH, WKERNEL_WINDOW_HEIGHT, flags);
    if ( !wkernel_pWindow )
    {
        fprintf(stderr, "wkernel: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    wkernel_hwnd = (HWND)wkernel_pWindow;

    // Text input on, so typed characters arrive as SDL text events (WM_CHAR).
    SDL_StartTextInput(wkernel_pWindow);
    return 0;
}

int J3DAPI wkernel_Run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd, LPCSTR lpWindowName)
{
    int result = 0;
    (void)hPrevInstance; (void)nShowCmd;

    if ( wkernel_CreateWindow(lpWindowName) )
    {
        return -1;
    }

    stdWin95_SetWindow(wkernel_hwnd);
    stdWin95_SetInstance(hInstance);
    stdWin95_SetGuid(&wkernel_guid);

    if ( JonesMain_Startup(lpCmdLine) )
    {
        JonesMain_Shutdown();
        wkernel_DestroyWindow();
        SDL_Quit();
        return J3DMenu_QuitAtLaunch() ? 1 : -1; // Quit in the launcher is the player's normal way out (WM_QUIT's 1)
    }

    const char*   pFrames   = getenv("J3D_FRAMES");
    unsigned long maxFrames = pFrames ? strtoul(pFrames, NULL, 10) : 0; // 0: no limit
    unsigned long numFrames = 0;

    while ( result == 0 )
    {
        if ( (maxFrames && numFrames == maxFrames) || J3DRecord_ShouldQuit() )
        {
            // The window is closed as if by its close button; the run counts as complete. (A replay that went out of
            // step also ends here; main() reports it.)
            wkernel_MainWndProc(wkernel_hwnd, WM_CLOSE, 0, 0);
            break;
        }

        result = JonesMain_Process();
        ++numFrames;
        J3DHost_AfterFrame(numFrames); // --screenshot-at
        J3DMenu_AfterFrame(numFrames); // J3D_MENU_TEST
    }

    wkernel_DestroyWindow();
    SDL_Quit();
    return result;
}

void J3DAPI wkernel_SetWindowStyle(LONG dwNewLong)
{
    if ( wkernel_pWindow )
    {
        // A caption (WS_OVERLAPPEDWINDOW, the windowed mode) gets the title bar and border; WS_POPUP gets neither.
        SDL_SetWindowBordered(wkernel_pWindow, (dwNewLong & WS_CAPTION) == WS_CAPTION);
    }
}

BOOL J3DAPI wkernel_SetWindowSize(int width, int height)
{
    // Retail added its frame to the size; SDL sizes the client area directly, which is the same result.
    if ( !wkernel_pWindow )
    {
        return FALSE;
    }

    // In a fullscreen mode retail's window already matched the screen (DirectDraw had set the display to the mode's
    // size), so the call changed nothing. A fullscreen SDL window would shrink to the size given; it keeps the screen
    // and the present path scales the back buffer to it (stdDisplay_sdl.c).
    if ( (SDL_GetWindowFlags(wkernel_pWindow) & SDL_WINDOW_FULLSCREEN) != 0 )
    {
        return TRUE;
    }

    return SDL_SetWindowSize(wkernel_pWindow, width, height);
}

void J3DAPI wkernel_SetWindowProc(WKERNELWNDPROC pfProc)
{
    wkernel_pfWndProc = pfProc;
}

/* ---- The dialog system's host (j3dgui.h) ---------------------------------------------------------------------------- */

void J3DGuiHost_GetScreenSize(int* pWidth, int* pHeight)
{
    uint32_t w = 0;
    uint32_t h = 0;
    stdDisplay_GetBackBufferSize(&w, &h);
    *pWidth = w ? (int)w : 640;
    *pHeight = h ? (int)h : 480;
}

int J3DGuiHost_IsInteractive(void)
{
    // A headless run has nobody to answer; a replay must not wait for input the recording does not hold.
    return !J3DDisplay_IsHeadless() && !J3DRecord_IsReplaying();
}

HWND J3DGuiHost_GameWindow(void)
{
    return wkernel_hwnd;
}

int J3DGuiHost_Pump(int bWait)
{
    return wkernel_Pump(bWait) || wkernel_bQuit;
}

void J3DGuiHost_BeginScreen(void)
{
    uint32_t w = 0;
    uint32_t h = 0;
    stdDisplay_GetBackBufferSize(&w, &h);
    if ( (!w || !h) && J3DGuiHost_IsInteractive() )
    {
        J3DApple_OpenTemporaryScreen(800, 600);
    }
}

void J3DGuiHost_EndScreen(void)
{
    J3DApple_CloseTemporaryScreen();
}

void J3DGuiHost_Present(void)
{
    stdDisplay_Update();
}

int J3DAPI wkernel_PeekProcessEvents(void)
{
    return wkernel_Pump(0);
}

int J3DAPI wkernel_ProcessEvents(void)
{
    return wkernel_Pump(1);
}

/* ---- Win32: the game window's message queue (replaces win32_msg_null.c) ------------------------------------------- */

BOOL WINAPI PostMessageA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if ( uMsg == WM_QUIT )
    {
        wkernel_bQuit = 1;
        return TRUE;
    }

    // The game window's queue is here; a dialog's is the dialog system's (Phase 6).
    if ( J3DGui_IsWindow(hWnd) )
    {
        return J3DGui_PostMessage(hWnd, uMsg, wParam, lParam);
    }
    if ( hWnd == wkernel_hwnd && wkernel_pWindow )
    {
        wkernel_QueueMessage(hWnd, uMsg, wParam, lParam, 0); // the game's own: not recorded
    }
    return TRUE;
}

void WINAPI PostQuitMessage(int nExitCode)
{
    (void)nExitCode;
    wkernel_bQuit = 1;
}

UINT_PTR WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc)
{
    // The game sets timers on its own window with no callback; anything else is the dialogs' (Phase 6).
    if ( !hWnd || hWnd != wkernel_hwnd || !wkernel_pWindow || lpTimerFunc )
    {
        return 0;
    }

    WkernelTimer* pFree = NULL;
    for ( size_t i = 0; i < WKERNEL_MAXTIMERS; i++ )
    {
        WkernelTimer* pTimer = &wkernel_aTimers[i];
        if ( pTimer->bUsed && pTimer->id == nIDEvent )
        {
            pFree = pTimer; // replaces the timer with that ID, as SetTimer does
            break;
        }
        if ( !pTimer->bUsed && !pFree )
        {
            pFree = pTimer;
        }
    }

    if ( !pFree )
    {
        return 0;
    }

    pFree->bUsed   = 1;
    pFree->id      = nIDEvent;
    pFree->elapse  = uElapse;
    pFree->dueTime = GetTickCount() + uElapse;
    return nIDEvent;
}

BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent)
{
    if ( !hWnd || hWnd != wkernel_hwnd )
    {
        return FALSE;
    }

    for ( size_t i = 0; i < WKERNEL_MAXTIMERS; i++ )
    {
        if ( wkernel_aTimers[i].bUsed && wkernel_aTimers[i].id == uIDEvent )
        {
            wkernel_aTimers[i].bUsed = 0;
            return TRUE;
        }
    }
    return FALSE;
}
