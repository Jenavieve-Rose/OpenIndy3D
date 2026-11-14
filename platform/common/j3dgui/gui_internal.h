#ifndef J3D_PLATFORM_J3DGUI_INTERNAL_H
#define J3D_PLATFORM_J3DGUI_INTERNAL_H
/*
 * j3dgui internals (PORT-PLAN.md Phase 6, decision 20): the slice of Win32's window manager, dialog manager, common
 * controls and GDI that the game's dialog code (Jones3D/Display/jonesConfig.c, Jones3D/Gui/JonesDialog.c,
 * Jones3D/Main/JonesDevDialog.c) uses, drawn in the classic Windows look into the game's back buffer.
 *
 * Coordinates. "Screen" is the back buffer, in its pixels: a top-level window's rectangle is in screen coordinates, a
 * child's in its parent's client coordinates, as in Win32. Mouse input arrives in screen coordinates.
 *
 * Pixels. A surface is 32-bit, B, G, R, X in memory: the back buffer's own layout (stdDisplay_common.c) and Win32's
 * 32-bit DIB layout. A COLORREF (0x00BBGGRR) converts with GUI_RGB32.
 *
 * Drawing. Each top-level window owns a surface the size of its window rectangle. A window marked dirty is repainted
 * into it (WM_NCPAINT, WM_ERASEBKGND, WM_PAINT, with the window procedures doing the drawing through GDI, as in
 * Win32), and every window above it in the tree after it; nothing else is redrawn. J3DGui_Compose then copies the
 * visible top-level surfaces over the frame captured when the first dialog opened.
 *
 * Threads: the game thread only.
 */
#include <Windows.h>
#include <CommCtrl.h>

#include <stddef.h>
#include <stdint.h>

#include "gui_templates.h"

#define GUI_WINDOW_MAGIC 0x574E444Au // "JDNW"

#define GUI_RGB32(cr) ((((cr) & 0xFF) << 16) | ((cr) & 0xFF00) | (((cr) >> 16) & 0xFF))
#define GUI_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GUI_MAX(a, b) ((a) > (b) ? (a) : (b))

/* ---- Surfaces ----------------------------------------------------------------------------------------------------- */

typedef struct J3DGuiSurface
{
    uint32_t* pPixels;
    int width;
    int height;
    int pitch;      // in pixels
} J3DGuiSurface;

J3DGuiSurface* J3DGui_NewSurface(int width, int height);
void J3DGui_FreeSurface(J3DGuiSurface* pSurface);

/* ---- Window classes ----------------------------------------------------------------------------------------------- */

typedef enum J3DGuiClass
{
    GUI_CLASS_NONE = 0,
    GUI_CLASS_DIALOG,       // "#32770"
    GUI_CLASS_BUTTON,
    GUI_CLASS_STATIC,
    GUI_CLASS_EDIT,
    GUI_CLASS_LISTBOX,
    GUI_CLASS_COMBOBOX,
    GUI_CLASS_COMBOLBOX,    // a drop-down combo box's list, a top-level popup
    GUI_CLASS_SCROLLBAR,
    GUI_CLASS_TRACKBAR,     // "msctls_trackbar32"
    GUI_CLASS_LISTVIEW,     // "SysListView32"
    GUI_CLASS_HEADER,       // "SysHeader32", the list view's column header
} J3DGuiClass;

typedef struct J3DGuiWindow J3DGuiWindow;
struct J3DGuiWindow
{
    uint32_t magic;
    J3DGuiClass cls;
    char aClassName[32];
    J3DGuiWindow* pParent;      // NULL for a top-level window
    HWND hOwner;                // a top-level window's owner (often the game's window, which is not a j3dgui window)
    J3DGuiWindow* pFirstChild;  // children in creation order (tab order, and painting order)
    J3DGuiWindow* pNext;
    int id;
    DWORD style;
    DWORD exStyle;
    char* pText;
    RECT rect;                  // window rectangle: screen coordinates for a top-level window, else the parent's client
    WNDPROC pfnWndProc;         // the window procedure (a subclass replaces it, GWLP_WNDPROC)
    WNDPROC pfnClassProc;       // the class's own procedure
    DLGPROC pfnDlgProc;         // dialogs
    LONG_PTR dlgUser;           // DWLP_USER
    LONG_PTR dlgMsgResult;      // DWLP_MSGRESULT
    LONG_PTR userData;          // GWLP_USERDATA
    HINSTANCE hInstance;
    HFONT hFont;                // WM_SETFONT
    int bDirty;                 // needs painting
    int bDestroying;
    void* pData;                // the class's own state (gui_controls.c)

    // top-level windows
    J3DGuiSurface* pSurface;
    int bModal;
    int bEnded;
    INT_PTR result;
    J3DGuiWindow* pLastFocus;   // the child that had the focus when the dialog was last active
    int baseUnitX;              // dialog base units of the template's font, for MapDialogRect
    int baseUnitY;
};

J3DGuiWindow* J3DGui_FromHwnd(HWND hwnd);  // NULL unless hwnd is a live j3dgui window
static inline HWND J3DGui_ToHwnd(J3DGuiWindow* pWindow)
{
    return (HWND)pWindow;
}

J3DGuiClass J3DGui_ClassFromName(const char* pName);
WNDPROC J3DGui_ClassProc(J3DGuiClass cls);

// Creates a window of a j3dgui class. rect is in the parent's client coordinates (screen for a top-level window).
J3DGuiWindow* J3DGui_CreateWindow(J3DGuiClass cls, const char* pClassName, const char* pText, DWORD style, DWORD exStyle,
                                  const RECT* pRect, J3DGuiWindow* pParent, HWND hOwner, int id, HINSTANCE hInstance, LPVOID pCreateParam);

J3DGuiWindow* J3DGui_TopLevel(J3DGuiWindow* pWindow);
void J3DGui_SetText(J3DGuiWindow* pWindow, const char* pText);

// The client rectangle in window coordinates (relative to the window rectangle's top-left): the window less its frame
// and caption.
void J3DGui_GetClientInWindow(const J3DGuiWindow* pWindow, RECT* pRect);
// The window's client origin and its window origin in screen coordinates.
void J3DGui_ClientOrigin(const J3DGuiWindow* pWindow, POINT* pOrigin);
void J3DGui_WindowOrigin(const J3DGuiWindow* pWindow, POINT* pOrigin);
int  J3DGui_IsReallyVisible(const J3DGuiWindow* pWindow);  // it and every ancestor WS_VISIBLE
int  J3DGui_IsReallyEnabled(const J3DGuiWindow* pWindow);

void J3DGui_Invalidate(J3DGuiWindow* pWindow);              // the window and everything above it repaint
void J3DGui_Paint(J3DGuiWindow* pTop);                      // repaints what is dirty into the top-level surface

/* ---- Windows, in z-order ------------------------------------------------------------------------------------------ */

// The visible top-level windows, bottom to top. Returns the count.
int J3DGui_GetTopLevels(J3DGuiWindow** apWindows, int max);
J3DGuiWindow* J3DGui_ActiveDialog(void);                    // the topmost enabled top-level window
J3DGuiWindow* J3DGui_WindowFromPoint(POINT screen);         // the deepest visible, enabled window under a point

/* ---- Focus and input ---------------------------------------------------------------------------------------------- */

J3DGuiWindow* J3DGui_GetFocusWindow(void);
void J3DGui_SetFocusWindow(J3DGuiWindow* pWindow);
J3DGuiWindow* J3DGui_GetCaptureWindow(void);

/* ---- GDI ---------------------------------------------------------------------------------------------------------- */

// A DC drawing into a window: its top-level surface, origin at the window's client (or window, bWindowDC) origin,
// clipped to it.
HDC J3DGui_GetWindowDC(J3DGuiWindow* pWindow, int bWindowDC);
void J3DGui_ReleaseWindowDC(HDC hdc);

// Fonts.
HFONT J3DGui_DefaultFont(void);                             // the dialogs' "MS Sans Serif" 8
HFONT J3DGui_GetFont(J3DGuiWindow* pWindow);                // the window's WM_SETFONT font, else the default
void J3DGui_TextExtent(HFONT hFont, const char* pText, int len, SIZE* pSize);
int  J3DGui_FontHeight(HFONT hFont);                        // the cell height (tmHeight)

// Drawing on a window's surface, in client coordinates of hdc.
void J3DGui_FillRect32(HDC hdc, const RECT* pRect, uint32_t color);
void J3DGui_DrawText32(HDC hdc, HFONT hFont, const char* pText, int len, int x, int y, uint32_t color, int bUnderlineMnemonic);
void J3DGui_DrawIconAt(HDC hdc, HICON hIcon, int x, int y, int cx, int cy);
uint32_t J3DGui_SysColor(int index);                        // GetSysColor, as 32-bit pixels

// The classic look (gui_draw.c).
void J3DGui_DrawEdge3D(HDC hdc, RECT* pRect, int style);    // style: GUI_EDGE_*
void J3DGui_DrawFocusRect32(HDC hdc, const RECT* pRect);
void J3DGui_DrawCheck(HDC hdc, const RECT* pBox, int bChecked, int bEnabled, int bPushed);
void J3DGui_DrawRadio(HDC hdc, const RECT* pBox, int bChecked, int bEnabled, int bPushed);
void J3DGui_DrawArrow(HDC hdc, const RECT* pRect, int direction, int bEnabled);  // 0 up, 1 down, 2 left, 3 right
void J3DGui_DrawScrollButton(HDC hdc, const RECT* pRect, int direction, int bPushed, int bEnabled);
void J3DGui_DrawEtchedText(HDC hdc, HFONT hFont, const char* pText, int x, int y, int bMnemonic); // disabled text
#define GUI_EDGE_RAISED_OUTER   1   // a button's or a window's outer frame
#define GUI_EDGE_RAISED         2   // a raised button (two pixels)
#define GUI_EDGE_SUNKEN         3   // an edit or list's client edge (two pixels)
#define GUI_EDGE_PUSHED         4   // a pushed button
#define GUI_EDGE_ETCHED         5   // a group box's etched line
#define GUI_EDGE_STATIC_SUNKEN  6   // SS_SUNKEN, one pixel
#define GUI_EDGE_DEFAULT        7   // a default push button: black outer line, then raised

void J3DGuiDraw_NonClient(J3DGuiWindow* pWindow);                 // the frame, caption and close button, or a control's edge
void J3DGuiDraw_CloseButtonRect(const J3DGuiWindow* pWindow, RECT* pRect); // in window coordinates
void J3DGui_DrawScrollTrack(HDC hdc, const RECT* pTrack);
HFONT J3DGui_CaptionFont(void);                                    // MS Sans Serif 8 bold

// gui_lists.c: a combo box's window height is its field's; the height asked for is its drop-down's.
int J3DGui_ComboAdjustHeight(J3DGuiWindow* pCombo, int cy);

// gui_images.c
void J3DGui_ComposeOverlay(uint32_t* pPixels, int width, int height, int pitchPixels);  // the drag image
void J3DGui_SetCursorPosition(int x, int y);
void J3DGui_TrackKey(UINT msg, WPARAM vk);

/* ---- Messages ------------------------------------------------------------------------------------------------------- */

LRESULT J3DGui_Send(J3DGuiWindow* pWindow, UINT msg, WPARAM wParam, LPARAM lParam);
void J3DGui_NotifyParent(J3DGuiWindow* pControl, WORD code);    // WM_COMMAND(id, code) to the parent

// Default dialog procedure (DefDlgProc).
LRESULT CALLBACK J3DGui_DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ---- The modal loop and the host (gui_dialog.c, j3dgui_host.h) ----------------------------------------------------- */

void J3DGui_RunModal(J3DGuiWindow* pDialog);
int  J3DGui_ModalDepth(void);

#endif
