#ifndef J3D_PLATFORM_J3DGUI_H
#define J3D_PLATFORM_J3DGUI_H
/*
 * j3dgui: the Win32 dialog emulation (PORT-PLAN.md Phase 6, decision 20). What the rest of the platform calls.
 *
 * The game's dialog code calls the Win32 functions themselves (DialogBoxParam, SendMessage, GetDC, ...), which
 * j3dgui implements in platform/common/j3dgui/. A modal dialog runs a nested loop, as DialogBoxParam did: it pumps the
 * host's events through J3DGuiHost_Pump, which hands keyboard and mouse input to J3DGui_Input while a dialog is open,
 * and presents the back buffer with the dialogs drawn over the frame that was on screen when the first one opened.
 */
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- For the event loop ---------------------------------------------------------------------------------------------- */

// Nonzero while a modal dialog or message box is open: keyboard and mouse input belongs to it.
int J3DGui_IsModalActive(void);

// Keyboard and mouse input for the dialogs, as the game window would have received it: WM_KEYDOWN, WM_KEYUP,
// WM_SYSKEYDOWN, WM_SYSKEYUP, WM_CHAR, WM_MOUSEMOVE, WM_LBUTTONDOWN/UP, WM_RBUTTONDOWN/UP, WM_LBUTTONDBLCLK,
// WM_MOUSEWHEEL. Mouse messages carry the position in back-buffer pixels in lParam, as the game's own do.
void J3DGui_Input(UINT msg, WPARAM wParam, LPARAM lParam);

// Is hwnd one of j3dgui's windows (a dialog or a control)?
int J3DGui_IsWindow(HWND hwnd);

// PostMessage to a j3dgui window: delivered from the modal loop.
BOOL J3DGui_PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// For tests: called on every turn of every modal loop with the dialog, so a test can drive it (send it messages,
// J3DGui_Input) and end it. A run with a hook installed is one that can answer dialogs. NULL removes it.
typedef void (*J3DGuiModalHook)(HWND hDlg, int iteration);
void J3DGui_SetModalHook(J3DGuiModalHook pfHook);

// The back buffer's contents changed under the dialogs (a display restart): capture it again on the next present.
void J3DGui_RecaptureBackground(void);

/* ---- The host: implemented by the platform (wkernel_sdl.c, wkernel_null.c) ------------------------------------------- */

// The size of "the screen" the dialogs are placed on: the back buffer, or a default when no mode is set yet.
void J3DGuiHost_GetScreenSize(int* pWidth, int* pHeight);

// Can a player answer a dialog in this run? False for a headless run: its dialogs are dismissed as IDCANCEL (and
// message boxes printed) unless a test installed a modal hook.
int J3DGuiHost_IsInteractive(void);

// The game's window (wkernel's HWND), the owner of top-level dialogs.
HWND J3DGuiHost_GameWindow(void);

// Processes pending host events; with bWait, waits briefly for one first. Input goes to J3DGui_Input while a modal
// dialog is open, everything else to the game's window procedure. Returns nonzero when the application is quitting.
int J3DGuiHost_Pump(int bWait);

// Called before the first dialog is created and after the last one is gone: a host with no display mode set yet
// opens a temporary one so the dialog can be seen (the launcher comes up before the game's display), and closes it.
void J3DGuiHost_BeginScreen(void);
void J3DGuiHost_EndScreen(void);

// Shows the back buffer (stdDisplay_Update), or does nothing where there is no display.
void J3DGuiHost_Present(void);

// A dialog changed: the modal loop composes and presents at its next turn. (Implemented in j3dgui.)
void J3DGuiHost_RequestPresent(void);

#ifdef __cplusplus
}
#endif

#endif
