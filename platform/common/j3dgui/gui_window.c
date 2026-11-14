/*
 * j3dgui windows (PORT-PLAN.md P6.3): window objects and the Win32 window functions over them.
 *
 * An HWND the game holds is either a j3dgui window (a pointer into the registry below, checked before every use), the
 * game's own window (wkernel's), or the desktop (GetDesktopWindow). The last two are "the screen": the back buffer.
 *
 * Non-client metrics are Windows 98's classic ones: a dialog frame 3 pixels wide, an 18-pixel caption, a 1-pixel
 * border, and the 2-pixel sunken client edge that DS_3DLOOK gives a bordered control in a dialog.
 */
#include "gui_internal.h"
#include "j3dgui.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GUI_MAXWINDOWS 1024

static J3DGuiWindow* gui_apWindows[GUI_MAXWINDOWS]; // every live window, creation order
static int gui_numWindows = 0;
static J3DGuiWindow* gui_pFocus = NULL;
static J3DGuiWindow* gui_pCapture = NULL;
static J3DGuiWindow* gui_pActive = NULL;

#define GUI_DESKTOP ((HWND)(uintptr_t)0x0D35C7F0u) // the token GetDesktopWindow hands out

// Calls aimed at a window that is not j3dgui's (the game's own, or the desktop) answer as the null platform did before
// the dialog system existed, so the game's own window code is unchanged; only its rectangle is the screen's.
static int gui_IsGameWindow(HWND hwnd)
{
    return hwnd && (hwnd == J3DGuiHost_GameWindow() || hwnd == GUI_DESKTOP);
}

/* ---- Registry ------------------------------------------------------------------------------------------------------ */

J3DGuiWindow* J3DGui_FromHwnd(HWND hwnd)
{
    int i;
    if ( !hwnd )
    {
        return NULL;
    }
    for ( i = 0; i < gui_numWindows; ++i )
    {
        if ( (HWND)gui_apWindows[i] == hwnd )
        {
            return gui_apWindows[i]->bDestroying > 1 ? NULL : gui_apWindows[i];
        }
    }
    return NULL;
}

int J3DGui_IsWindow(HWND hwnd)
{
    return J3DGui_FromHwnd(hwnd) != NULL;
}

int J3DGui_GetTopLevels(J3DGuiWindow** apWindows, int max)
{
    int n = 0;
    int i;
    for ( i = 0; i < gui_numWindows && n < max; ++i )
    {
        J3DGuiWindow* p = gui_apWindows[i];
        if ( !p->pParent && (p->style & WS_VISIBLE) && !p->bDestroying )
        {
            apWindows[n++] = p;
        }
    }
    return n;
}

J3DGuiWindow* J3DGui_ActiveDialog(void)
{
    J3DGuiWindow* ap[64];
    int n = J3DGui_GetTopLevels(ap, 64);
    while ( n-- > 0 )
    {
        if ( !(ap[n]->style & WS_DISABLED) )
        {
            return ap[n];
        }
    }
    return NULL;
}

/* ---- Classes ------------------------------------------------------------------------------------------------------- */

J3DGuiClass J3DGui_ClassFromName(const char* pName)
{
    static const struct { const char* pName; J3DGuiClass cls; } aClasses[] = {
        { "#32770", GUI_CLASS_DIALOG },     { "Button", GUI_CLASS_BUTTON },           { "Static", GUI_CLASS_STATIC },
        { "Edit", GUI_CLASS_EDIT },         { "ListBox", GUI_CLASS_LISTBOX },         { "ComboBox", GUI_CLASS_COMBOBOX },
        { "ComboLBox", GUI_CLASS_COMBOLBOX }, { "ScrollBar", GUI_CLASS_SCROLLBAR },   { "msctls_trackbar32", GUI_CLASS_TRACKBAR },
        { "SysListView32", GUI_CLASS_LISTVIEW }, { "SysHeader32", GUI_CLASS_HEADER },
    };
    size_t i;
    if ( (uintptr_t)pName < 0x10000 )
    {
        return (uintptr_t)pName == 0x8002 ? GUI_CLASS_DIALOG : GUI_CLASS_NONE; // MAKEINTATOM(WC_DIALOG)
    }
    for ( i = 0; i < sizeof(aClasses) / sizeof(aClasses[0]); ++i )
    {
        if ( strcasecmp(pName, aClasses[i].pName) == 0 )
        {
            return aClasses[i].cls;
        }
    }
    return GUI_CLASS_NONE;
}

static const char* gui_ClassName(J3DGuiClass cls)
{
    switch ( cls )
    {
        case GUI_CLASS_DIALOG:    return "#32770";
        case GUI_CLASS_BUTTON:    return "Button";
        case GUI_CLASS_STATIC:    return "Static";
        case GUI_CLASS_EDIT:      return "Edit";
        case GUI_CLASS_LISTBOX:   return "ListBox";
        case GUI_CLASS_COMBOBOX:  return "ComboBox";
        case GUI_CLASS_COMBOLBOX: return "ComboLBox";
        case GUI_CLASS_SCROLLBAR: return "ScrollBar";
        case GUI_CLASS_TRACKBAR:  return "msctls_trackbar32";
        case GUI_CLASS_LISTVIEW:  return "SysListView32";
        case GUI_CLASS_HEADER:    return "SysHeader32";
        default:                  return "";
    }
}

/* ---- Geometry ------------------------------------------------------------------------------------------------------ */

static void gui_ScreenRect(RECT* pRect)
{
    int w;
    int h;
    J3DGuiHost_GetScreenSize(&w, &h);
    SetRect(pRect, 0, 0, w, h);
}

// The frame's width on each side and the caption's height, from the style.
static void gui_NonClient(const J3DGuiWindow* p, int* pFrame, int* pCaption)
{
    int frame = 0;
    int caption = 0;

    if ( (p->style & WS_CAPTION) == WS_CAPTION )
    {
        caption = 18;
    }
    if ( !p->pParent )
    {
        if ( (p->style & DS_MODALFRAME) || (p->style & WS_DLGFRAME) || (p->exStyle & WS_EX_DLGMODALFRAME) )
        {
            frame = 3;
        }
        else if ( p->style & WS_BORDER )
        {
            frame = 1;
        }
    }
    else if ( (p->exStyle & WS_EX_CLIENTEDGE) || ((p->style & WS_BORDER) && p->cls != GUI_CLASS_STATIC && p->cls != GUI_CLASS_BUTTON) )
    {
        frame = 2; // the 3D client edge Windows gives a bordered control in a DS_3DLOOK dialog
    }
    else if ( p->style & WS_BORDER )
    {
        frame = 1;
    }
    *pFrame = frame;
    *pCaption = caption;
}

void J3DGui_GetClientInWindow(const J3DGuiWindow* p, RECT* pRect)
{
    int frame;
    int caption;
    gui_NonClient(p, &frame, &caption);
    pRect->left   = frame;
    pRect->top    = frame + caption;
    pRect->right  = GUI_MAX(pRect->left, p->rect.right - p->rect.left - frame);
    pRect->bottom = GUI_MAX(pRect->top, p->rect.bottom - p->rect.top - frame);
}

void J3DGui_WindowOrigin(const J3DGuiWindow* p, POINT* pOrigin)
{
    POINT origin = { p->rect.left, p->rect.top };
    if ( p->pParent )
    {
        POINT parent;
        J3DGui_ClientOrigin(p->pParent, &parent);
        origin.x += parent.x;
        origin.y += parent.y;
    }
    *pOrigin = origin;
}

void J3DGui_ClientOrigin(const J3DGuiWindow* p, POINT* pOrigin)
{
    RECT client;
    J3DGui_WindowOrigin(p, pOrigin);
    J3DGui_GetClientInWindow(p, &client);
    pOrigin->x += client.left;
    pOrigin->y += client.top;
}

J3DGuiWindow* J3DGui_TopLevel(J3DGuiWindow* p)
{
    while ( p && p->pParent )
    {
        p = p->pParent;
    }
    return p;
}

int J3DGui_IsReallyVisible(const J3DGuiWindow* p)
{
    for ( ; p; p = p->pParent )
    {
        if ( !(p->style & WS_VISIBLE) )
        {
            return 0;
        }
    }
    return 1;
}

int J3DGui_IsReallyEnabled(const J3DGuiWindow* p)
{
    for ( ; p; p = p->pParent )
    {
        if ( p->style & WS_DISABLED )
        {
            return 0;
        }
    }
    return 1;
}

void J3DGui_Invalidate(J3DGuiWindow* p)
{
    if ( p )
    {
        p->bDirty = 1;
        J3DGuiHost_RequestPresent();
    }
}

static void gui_InvalidateTree(J3DGuiWindow* p)
{
    J3DGuiWindow* pChild;
    p->bDirty = 1;
    for ( pChild = p->pFirstChild; pChild; pChild = pChild->pNext )
    {
        gui_InvalidateTree(pChild);
    }
}

// A top-level window's surface follows its size; a resize repaints it whole.
static void gui_SizeSurface(J3DGuiWindow* p)
{
    int w = p->rect.right - p->rect.left;
    int h = p->rect.bottom - p->rect.top;
    if ( p->pParent )
    {
        return;
    }
    if ( p->pSurface && p->pSurface->width == w && p->pSurface->height == h )
    {
        return;
    }
    J3DGui_FreeSurface(p->pSurface);
    p->pSurface = J3DGui_NewSurface(GUI_MAX(w, 1), GUI_MAX(h, 1));
    gui_InvalidateTree(p);
}

J3DGuiWindow* J3DGui_WindowFromPoint(POINT pt)
{
    J3DGuiWindow* ap[64];
    int n = J3DGui_GetTopLevels(ap, 64);
    while ( n-- > 0 )
    {
        J3DGuiWindow* p = ap[n];
        if ( PtInRect(&p->rect, pt) )
        {
            // descend into the topmost child under the point (later children are above earlier ones)
            for ( ;; )
            {
                J3DGuiWindow* pHit = NULL;
                J3DGuiWindow* pChild;
                POINT origin;
                J3DGui_ClientOrigin(p, &origin);
                for ( pChild = p->pFirstChild; pChild; pChild = pChild->pNext )
                {
                    POINT local = { pt.x - origin.x, pt.y - origin.y };
                    if ( (pChild->style & WS_VISIBLE) && PtInRect(&pChild->rect, local) && !(pChild->cls == GUI_CLASS_BUTTON && (pChild->style & 0xF) == BS_GROUPBOX) )
                    {
                        pHit = pChild; // the last match is the topmost
                    }
                }
                if ( !pHit )
                {
                    return p;
                }
                p = pHit;
            }
        }
    }
    return NULL;
}

/* ---- Creation and destruction ------------------------------------------------------------------------------------- */

void J3DGui_SetText(J3DGuiWindow* p, const char* pText)
{
    free(p->pText);
    p->pText = strdup(pText ? pText : "");
}

J3DGuiWindow* J3DGui_CreateWindow(J3DGuiClass cls, const char* pClassName, const char* pText, DWORD style, DWORD exStyle,
                                  const RECT* pRect, J3DGuiWindow* pParent, HWND hOwner, int id, HINSTANCE hInstance, LPVOID pCreateParam)
{
    J3DGuiWindow* p;
    CREATESTRUCTA cs;

    if ( gui_numWindows >= GUI_MAXWINDOWS )
    {
        return NULL;
    }
    p = calloc(1, sizeof(*p));
    if ( !p )
    {
        return NULL;
    }
    p->magic = GUI_WINDOW_MAGIC;
    p->cls = cls;
    strncpy(p->aClassName, pClassName ? pClassName : gui_ClassName(cls), sizeof(p->aClassName) - 1);
    p->pText = strdup(pText ? pText : "");
    p->style = style;
    p->exStyle = exStyle;
    p->rect = *pRect;
    p->pParent = pParent;
    p->hOwner = hOwner;
    p->id = id;
    p->hInstance = hInstance;
    p->pfnClassProc = J3DGui_ClassProc(cls);
    p->pfnWndProc = p->pfnClassProc;
    p->bDirty = 1;
    if ( pParent )
    {
        J3DGuiWindow** pp = &pParent->pFirstChild;
        while ( *pp )
        {
            pp = &(*pp)->pNext;
        }
        *pp = p;
    }
    gui_apWindows[gui_numWindows++] = p;
    gui_SizeSurface(p);

    memset(&cs, 0, sizeof(cs));
    cs.lpCreateParams = pCreateParam;
    cs.hInstance = hInstance;
    cs.hwndParent = pParent ? J3DGui_ToHwnd(pParent) : hOwner;
    cs.cx = pRect->right - pRect->left;
    cs.cy = pRect->bottom - pRect->top;
    cs.x = pRect->left;
    cs.y = pRect->top;
    cs.style = (LONG)style;
    cs.lpszName = p->pText;
    cs.lpszClass = p->aClassName;
    cs.dwExStyle = exStyle;
    J3DGui_Send(p, WM_NCCREATE, 0, (LPARAM)&cs);
    if ( J3DGui_Send(p, WM_CREATE, 0, (LPARAM)&cs) == -1 )
    {
        DestroyWindow(J3DGui_ToHwnd(p));
        return NULL;
    }
    J3DGui_Invalidate(p);
    return p;
}

static void gui_Unlink(J3DGuiWindow* p)
{
    int i;
    if ( p->pParent )
    {
        J3DGuiWindow** pp = &p->pParent->pFirstChild;
        while ( *pp && *pp != p )
        {
            pp = &(*pp)->pNext;
        }
        if ( *pp )
        {
            *pp = p->pNext;
        }
        J3DGui_Invalidate(p->pParent);
    }
    for ( i = 0; i < gui_numWindows; ++i )
    {
        if ( gui_apWindows[i] == p )
        {
            memmove(&gui_apWindows[i], &gui_apWindows[i + 1], (size_t)(gui_numWindows - i - 1) * sizeof(gui_apWindows[0]));
            gui_numWindows--;
            break;
        }
    }
    if ( gui_pFocus == p )
    {
        gui_pFocus = NULL;
    }
    if ( gui_pCapture == p )
    {
        gui_pCapture = NULL;
    }
    if ( gui_pActive == p )
    {
        gui_pActive = NULL;
    }
}

BOOL WINAPI DestroyWindow(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    int i;

    if ( !p || p->bDestroying )
    {
        return FALSE;
    }
    p->bDestroying = 1;
    J3DGui_Send(p, WM_DESTROY, 0, 0);
    while ( p->pFirstChild )
    {
        J3DGuiWindow* pChild = p->pFirstChild;
        if ( !DestroyWindow(J3DGui_ToHwnd(pChild)) )
        {
            p->pFirstChild = pChild->pNext; // already going: just unlink
        }
    }
    // windows this one owns (a combo box's list) go with it
    for ( i = gui_numWindows - 1; i >= 0; --i )
    {
        if ( i < gui_numWindows && gui_apWindows[i]->hOwner == hwnd && !gui_apWindows[i]->pParent )
        {
            DestroyWindow(J3DGui_ToHwnd(gui_apWindows[i]));
        }
    }
    J3DGui_Send(p, WM_NCDESTROY, 0, 0);
    gui_Unlink(p);
    p->bDestroying = 2;
    J3DGui_FreeSurface(p->pSurface);
    free(p->pText);
    p->magic = 0;
    free(p);
    J3DGuiHost_RequestPresent();
    return TRUE;
}

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR pClassName, LPCSTR pName, DWORD style, int x, int y, int cx, int cy,
                            HWND hParent, HMENU hMenu, HINSTANCE hInstance, LPVOID pParam)
{
    J3DGuiClass cls = J3DGui_ClassFromName(pClassName);
    J3DGuiWindow* pParent = (style & WS_CHILD) ? J3DGui_FromHwnd(hParent) : NULL;
    RECT rect;
    J3DGuiWindow* p;

    if ( cls == GUI_CLASS_NONE )
    {
        return NULL; // only j3dgui's classes; the game's own window is wkernel's
    }
    if ( (style & WS_CHILD) && !pParent )
    {
        return NULL;
    }
    SetRect(&rect, x, y, x + cx, y + cy);
    p = J3DGui_CreateWindow(cls, NULL, pName, style, exStyle, &rect, pParent, pParent ? NULL : hParent,
                            (style & WS_CHILD) ? (int)(intptr_t)hMenu : 0, hInstance, pParam);
    return p ? J3DGui_ToHwnd(p) : NULL;
}

/* ---- Messages ------------------------------------------------------------------------------------------------------ */

LRESULT J3DGui_Send(J3DGuiWindow* p, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if ( !p || !p->pfnWndProc || p->bDestroying > 1 )
    {
        return 0;
    }
    return p->pfnWndProc(J3DGui_ToHwnd(p), msg, wParam, lParam);
}

LRESULT WINAPI SendMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( p )
    {
        return J3DGui_Send(p, msg, wParam, lParam);
    }
    (void)msg; (void)wParam; (void)lParam;
    return 0;
}

LRESULT WINAPI SendDlgItemMessageA(HWND hDlg, int id, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return SendMessageA(GetDlgItem(hDlg, id), msg, wParam, lParam);
}

LRESULT WINAPI CallWindowProcA(WNDPROC pfn, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return pfn ? pfn(hwnd, msg, wParam, lParam) : 0;
}

void J3DGui_NotifyParent(J3DGuiWindow* pControl, WORD code)
{
    if ( pControl->pParent )
    {
        J3DGui_Send(pControl->pParent, WM_COMMAND, MAKEWPARAM(pControl->id, code), (LPARAM)J3DGui_ToHwnd(pControl));
    }
}

LRESULT WINAPI DefWindowProcA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !p )
    {
        return 0;
    }
    switch ( msg )
    {
        case WM_NCCREATE:
            return TRUE;
        case WM_SETTEXT:
            J3DGui_SetText(p, (const char*)lParam);
            J3DGui_Invalidate(p);
            return TRUE;
        case WM_GETTEXT:
        {
            size_t len;
            if ( wParam == 0 || !lParam )
            {
                return 0;
            }
            len = strlen(p->pText);
            if ( len > wParam - 1 )
            {
                len = wParam - 1;
            }
            memcpy((char*)lParam, p->pText, len);
            ((char*)lParam)[len] = 0;
            return (LRESULT)len;
        }
        case WM_GETTEXTLENGTH:
            return (LRESULT)strlen(p->pText);
        case WM_SETFONT:
            p->hFont = (HFONT)wParam;
            if ( LOWORD(lParam) )
            {
                J3DGui_Invalidate(p);
            }
            return 0;
        case WM_GETFONT:
            return (LRESULT)p->hFont;
        case WM_ERASEBKGND:
        {
            RECT client;
            GetClientRect(hwnd, &client);
            J3DGui_FillRect32((HDC)wParam, &client, J3DGui_SysColor(COLOR_3DFACE));
            return 1;
        }
        case WM_NCPAINT:
            J3DGuiDraw_NonClient(p);
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        case WM_SETCURSOR:
        case WM_MOUSEACTIVATE:
            return 0;
        default:
            return 0;
    }
}

/* ---- Window state -------------------------------------------------------------------------------------------------- */

BOOL WINAPI IsWindow(HWND hwnd)
{
    return J3DGui_FromHwnd(hwnd) != NULL;
}

BOOL WINAPI SetWindowTextA(HWND hwnd, LPCSTR pText)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !p )
    {
        return FALSE;
    }
    return (BOOL)J3DGui_Send(p, WM_SETTEXT, 0, (LPARAM)(pText ? pText : ""));
}

int WINAPI GetWindowTextA(HWND hwnd, LPSTR pText, int max)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !pText || max <= 0 )
    {
        return 0;
    }
    if ( !p )
    {
        pText[0] = 0;
        return 0;
    }
    return (int)J3DGui_Send(p, WM_GETTEXT, (WPARAM)max, (LPARAM)pText);
}

int WINAPI GetWindowTextLengthA(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    return p ? (int)J3DGui_Send(p, WM_GETTEXTLENGTH, 0, 0) : 0;
}

int WINAPI GetClassNameA(HWND hwnd, LPSTR pName, int max)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !pName || max <= 0 )
    {
        return 0;
    }
    strncpy(pName, p ? p->aClassName : "", (size_t)max - 1);
    pName[max - 1] = 0;
    return (int)strlen(pName);
}

BOOL WINAPI EnableWindow(HWND hwnd, BOOL bEnable)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    BOOL bWasDisabled;
    if ( !p )
    {
        return FALSE;
    }
    bWasDisabled = (p->style & WS_DISABLED) != 0;
    if ( bEnable )
    {
        p->style &= ~(DWORD)WS_DISABLED;
    }
    else
    {
        p->style |= WS_DISABLED;
        if ( gui_pFocus == p )
        {
            gui_pFocus = NULL;
        }
    }
    if ( bWasDisabled != !bEnable )
    {
        J3DGui_Send(p, WM_ENABLE, (WPARAM)bEnable, 0);
        J3DGui_Invalidate(p);
    }
    return bWasDisabled;
}

BOOL WINAPI IsWindowEnabled(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    return p && !(p->style & WS_DISABLED);
}

BOOL WINAPI ShowWindow(HWND hwnd, int cmd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    BOOL bWasVisible;
    if ( !p )
    {
        return FALSE;
    }
    bWasVisible = (p->style & WS_VISIBLE) != 0;
    if ( cmd == SW_HIDE )
    {
        p->style &= ~(DWORD)WS_VISIBLE;
        if ( gui_pFocus && (gui_pFocus == p || IsChild(hwnd, J3DGui_ToHwnd(gui_pFocus))) )
        {
            gui_pFocus = NULL;
        }
    }
    else
    {
        p->style |= WS_VISIBLE;
    }
    if ( bWasVisible != ((p->style & WS_VISIBLE) != 0) )
    {
        J3DGui_Send(p, WM_SHOWWINDOW, cmd != SW_HIDE, 0);
        if ( p->pParent )
        {
            J3DGui_Invalidate(p->pParent);
        }
        J3DGui_Invalidate(p);
    }
    return bWasVisible;
}

BOOL WINAPI IsWindowVisible(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    return p ? J3DGui_IsReallyVisible(p) : FALSE;
}

BOOL WINAPI IsIconic(HWND hwnd)
{
    (void)hwnd;
    return FALSE;
}

BOOL WINAPI IsZoomed(HWND hwnd)
{
    (void)hwnd;
    return FALSE;
}

BOOL WINAPI IsChild(HWND hParent, HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    for ( p = p ? p->pParent : NULL; p; p = p->pParent )
    {
        if ( J3DGui_ToHwnd(p) == hParent )
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- Position ------------------------------------------------------------------------------------------------------ */

static void gui_Move(J3DGuiWindow* p, int x, int y, int cx, int cy, int bMove, int bSize)
{
    RECT old = p->rect;
    if ( !bMove )
    {
        x = p->rect.left;
        y = p->rect.top;
    }
    if ( !bSize )
    {
        cx = p->rect.right - p->rect.left;
        cy = p->rect.bottom - p->rect.top;
    }
    else if ( p->cls == GUI_CLASS_COMBOBOX )
    {
        cy = J3DGui_ComboAdjustHeight(p, cy);
    }
    SetRect(&p->rect, x, y, x + GUI_MAX(cx, 0), y + GUI_MAX(cy, 0));
    if ( EqualRect(&old, &p->rect) )
    {
        return;
    }
    gui_SizeSurface(p);
    if ( p->pParent )
    {
        J3DGui_Invalidate(p->pParent); // what was under the old rectangle is uncovered
    }
    gui_InvalidateTree(p);
    J3DGuiHost_RequestPresent();
    if ( old.left != p->rect.left || old.top != p->rect.top )
    {
        J3DGui_Send(p, WM_MOVE, 0, MAKELPARAM(p->rect.left, p->rect.top));
    }
    if ( old.right - old.left != p->rect.right - p->rect.left || old.bottom - old.top != p->rect.bottom - p->rect.top )
    {
        RECT client;
        GetClientRect(J3DGui_ToHwnd(p), &client);
        J3DGui_Send(p, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
    }
}

BOOL WINAPI MoveWindow(HWND hwnd, int x, int y, int cx, int cy, BOOL bRepaint)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    (void)bRepaint;
    if ( !p )
    {
        return FALSE;
    }
    gui_Move(p, x, y, cx, cy, 1, 1);
    return TRUE;
}

BOOL WINAPI SetWindowPos(HWND hwnd, HWND hAfter, int x, int y, int cx, int cy, UINT flags)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !p )
    {
        return FALSE;
    }
    gui_Move(p, x, y, cx, cy, !(flags & SWP_NOMOVE), !(flags & SWP_NOSIZE));
    if ( flags & SWP_SHOWWINDOW )
    {
        ShowWindow(hwnd, SW_SHOW);
    }
    if ( flags & SWP_HIDEWINDOW )
    {
        ShowWindow(hwnd, SW_HIDE);
    }
    if ( !(flags & SWP_NOZORDER) && !p->pParent && hAfter == HWND_TOP )
    {
        // to the top of the top-level windows: the end of the registry's order
        int i;
        for ( i = 0; i < gui_numWindows; ++i )
        {
            if ( gui_apWindows[i] == p )
            {
                memmove(&gui_apWindows[i], &gui_apWindows[i + 1], (size_t)(gui_numWindows - i - 1) * sizeof(gui_apWindows[0]));
                gui_apWindows[gui_numWindows - 1] = p;
                break;
            }
        }
    }
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT pRect)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    POINT origin;
    if ( !pRect )
    {
        return FALSE;
    }
    if ( !p )
    {
        gui_ScreenRect(pRect); // the game's window or the desktop: the whole back buffer
        return TRUE;
    }
    J3DGui_WindowOrigin(p, &origin);
    SetRect(pRect, origin.x, origin.y, origin.x + p->rect.right - p->rect.left, origin.y + p->rect.bottom - p->rect.top);
    return TRUE;
}

BOOL WINAPI GetClientRect(HWND hwnd, LPRECT pRect)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    RECT client;
    if ( !pRect )
    {
        return FALSE;
    }
    if ( !p )
    {
        gui_ScreenRect(pRect);
        return TRUE;
    }
    J3DGui_GetClientInWindow(p, &client);
    SetRect(pRect, 0, 0, client.right - client.left, client.bottom - client.top);
    return TRUE;
}

BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT pPoint)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    POINT origin;
    if ( !pPoint )
    {
        return FALSE;
    }
    if ( p )
    {
        J3DGui_ClientOrigin(p, &origin);
        pPoint->x += origin.x;
        pPoint->y += origin.y;
    }
    return TRUE;
}

BOOL WINAPI ScreenToClient(HWND hwnd, LPPOINT pPoint)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    POINT origin;
    if ( !pPoint )
    {
        return FALSE;
    }
    if ( p )
    {
        J3DGui_ClientOrigin(p, &origin);
        pPoint->x -= origin.x;
        pPoint->y -= origin.y;
    }
    return TRUE;
}

int WINAPI MapWindowPoints(HWND hFrom, HWND hTo, LPPOINT pPoints, UINT count)
{
    POINT from = { 0, 0 };
    POINT to = { 0, 0 };
    J3DGuiWindow* pFrom = J3DGui_FromHwnd(hFrom);
    J3DGuiWindow* pTo = J3DGui_FromHwnd(hTo);
    UINT i;
    if ( pFrom )
    {
        J3DGui_ClientOrigin(pFrom, &from);
    }
    if ( pTo )
    {
        J3DGui_ClientOrigin(pTo, &to);
    }
    for ( i = 0; i < count; ++i )
    {
        pPoints[i].x += from.x - to.x;
        pPoints[i].y += from.y - to.y;
    }
    return MAKELONG(from.x - to.x, from.y - to.y);
}

BOOL WINAPI AdjustWindowRectEx(LPRECT pRect, DWORD style, BOOL bMenu, DWORD exStyle)
{
    J3DGuiWindow probe;
    int frame;
    int caption;
    (void)bMenu;
    memset(&probe, 0, sizeof(probe));
    probe.style = style;
    probe.exStyle = exStyle;
    gui_NonClient(&probe, &frame, &caption);
    pRect->left -= frame;
    pRect->top -= frame + caption;
    pRect->right += frame;
    pRect->bottom += frame;
    return TRUE;
}

BOOL WINAPI AdjustWindowRect(LPRECT pRect, DWORD style, BOOL bMenu)
{
    return AdjustWindowRectEx(pRect, style, bMenu, 0);
}

/* ---- Relations ----------------------------------------------------------------------------------------------------- */

HWND WINAPI GetParent(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !p )
    {
        return NULL;
    }
    return p->pParent ? J3DGui_ToHwnd(p->pParent) : p->hOwner; // a popup's GetParent is its owner
}

HWND WINAPI SetParent(HWND hwnd, HWND hParent)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    HWND hOld;
    (void)hParent;
    if ( !p )
    {
        return NULL;
    }
    hOld = GetParent(hwnd);
    return hOld; // re-parenting is not needed by the game's dialogs
}

HWND WINAPI GetDesktopWindow(void)
{
    return GUI_DESKTOP;
}

HWND WINAPI GetDlgItem(HWND hDlg, int id)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hDlg);
    J3DGuiWindow* pChild;
    if ( !p )
    {
        return NULL;
    }
    for ( pChild = p->pFirstChild; pChild; pChild = pChild->pNext )
    {
        if ( pChild->id == id )
        {
            return J3DGui_ToHwnd(pChild);
        }
    }
    return NULL;
}

int WINAPI GetDlgCtrlID(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    return p ? p->id : 0;
}

HWND WINAPI GetWindow(HWND hwnd, UINT cmd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    J3DGuiWindow* q;
    if ( !p )
    {
        return NULL;
    }
    switch ( cmd )
    {
        case GW_CHILD:
            return J3DGui_ToHwnd(p->pFirstChild);
        case GW_HWNDNEXT:
            return J3DGui_ToHwnd(p->pNext);
        case GW_HWNDPREV:
            if ( !p->pParent )
            {
                return NULL;
            }
            for ( q = p->pParent->pFirstChild; q && q->pNext != p; q = q->pNext )
            {
            }
            return J3DGui_ToHwnd(q);
        case GW_HWNDFIRST:
            return p->pParent ? J3DGui_ToHwnd(p->pParent->pFirstChild) : hwnd;
        case GW_HWNDLAST:
            for ( q = p; q->pNext; q = q->pNext )
            {
            }
            return J3DGui_ToHwnd(q);
        case GW_OWNER:
            return p->pParent ? NULL : p->hOwner;
        default:
            return NULL;
    }
}

static BOOL gui_EnumChildren(J3DGuiWindow* p, WNDENUMPROC pfn, LPARAM lParam)
{
    J3DGuiWindow* pChild = p->pFirstChild;
    while ( pChild )
    {
        J3DGuiWindow* pNext = pChild->pNext;
        if ( !pfn(J3DGui_ToHwnd(pChild), lParam) )
        {
            return FALSE;
        }
        if ( J3DGui_FromHwnd(J3DGui_ToHwnd(pChild)) && !gui_EnumChildren(pChild, pfn, lParam) ) // descendants too, as Win32
        {
            return FALSE;
        }
        pChild = pNext;
    }
    return TRUE;
}

BOOL WINAPI EnumChildWindows(HWND hParent, WNDENUMPROC pfn, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hParent);
    if ( !p || !pfn )
    {
        return FALSE;
    }
    gui_EnumChildren(p, pfn, lParam);
    return TRUE;
}

/* ---- Window data --------------------------------------------------------------------------------------------------- */

LONG_PTR WINAPI GetWindowLongPtrA(HWND hwnd, int index)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !p )
    {
        return 0;
    }
    switch ( index )
    {
        case GWL_WNDPROC:    return (LONG_PTR)p->pfnWndProc;
        case GWL_HINSTANCE:  return (LONG_PTR)p->hInstance;
        case GWL_HWNDPARENT: return (LONG_PTR)GetParent(hwnd);
        case GWL_ID:         return p->id;
        case GWL_STYLE:      return (LONG_PTR)p->style;
        case GWL_EXSTYLE:    return (LONG_PTR)p->exStyle;
        case GWL_USERDATA:   return p->userData;
        default:
            break;
    }
    if ( p->cls == GUI_CLASS_DIALOG )
    {
        switch ( index )
        {
            case DWL_MSGRESULT: return p->dlgMsgResult;
            case DWL_DLGPROC:   return (LONG_PTR)p->pfnDlgProc;
            case DWL_USER:      return p->dlgUser;
            default:            break;
        }
    }
    return 0;
}

LONG_PTR WINAPI SetWindowLongPtrA(HWND hwnd, int index, LONG_PTR value)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    LONG_PTR old;
    if ( !p )
    {
        (void)value;
        return 0;
    }
    old = GetWindowLongPtrA(hwnd, index);
    switch ( index )
    {
        case GWL_WNDPROC:   p->pfnWndProc = (WNDPROC)value; return old;
        case GWL_HINSTANCE: p->hInstance = (HINSTANCE)value; return old;
        case GWL_ID:        p->id = (int)value; return old;
        case GWL_STYLE:     p->style = (DWORD)value; J3DGui_Invalidate(p); return old;
        case GWL_EXSTYLE:   p->exStyle = (DWORD)value; J3DGui_Invalidate(p); return old;
        case GWL_USERDATA:  p->userData = value; return old;
        default:
            break;
    }
    if ( p->cls == GUI_CLASS_DIALOG )
    {
        switch ( index )
        {
            case DWL_MSGRESULT: p->dlgMsgResult = value; return old;
            case DWL_DLGPROC:   p->pfnDlgProc = (DLGPROC)value; return old;
            case DWL_USER:      p->dlgUser = value; return old;
            default:            break;
        }
    }
    return 0;
}

LONG WINAPI GetWindowLongA(HWND hwnd, int index)
{
    return (LONG)GetWindowLongPtrA(hwnd, index); // Port: narrowing cast, as the 32-bit API
}

LONG WINAPI SetWindowLongA(HWND hwnd, int index, LONG value)
{
    return (LONG)SetWindowLongPtrA(hwnd, index, value);
}

/* ---- Painting requests --------------------------------------------------------------------------------------------- */

BOOL WINAPI InvalidateRect(HWND hwnd, const RECT* pRect, BOOL bErase)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    (void)pRect;
    (void)bErase;
    if ( !p )
    {
        return gui_IsGameWindow(hwnd) || !hwnd;
    }
    J3DGui_Invalidate(p);
    return TRUE;
}

BOOL WINAPI ValidateRect(HWND hwnd, const RECT* pRect)
{
    (void)hwnd;
    (void)pRect;
    return TRUE;
}

BOOL WINAPI UpdateWindow(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( p )
    {
        J3DGui_Paint(J3DGui_TopLevel(p));
    }
    return TRUE;
}

BOOL WINAPI RedrawWindow(HWND hwnd, const RECT* pRect, HRGN hRgn, UINT flags)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    (void)pRect;
    (void)hRgn;
    if ( p )
    {
        if ( flags & RDW_ALLCHILDREN )
        {
            gui_InvalidateTree(p);
        }
        J3DGui_Invalidate(p);
        if ( flags & RDW_UPDATENOW )
        {
            J3DGui_Paint(J3DGui_TopLevel(p));
        }
    }
    return TRUE;
}

/* ---- Focus, activation, capture ------------------------------------------------------------------------------------ */

J3DGuiWindow* J3DGui_GetFocusWindow(void)
{
    return gui_pFocus;
}

void J3DGui_SetFocusWindow(J3DGuiWindow* p)
{
    J3DGuiWindow* pOld = gui_pFocus;
    if ( p == pOld )
    {
        return;
    }
    gui_pFocus = p;
    if ( pOld )
    {
        J3DGui_Send(pOld, WM_KILLFOCUS, (WPARAM)J3DGui_ToHwnd(p), 0);
        J3DGui_Invalidate(pOld);
    }
    if ( p && gui_pFocus == p )
    {
        J3DGuiWindow* pTop = J3DGui_TopLevel(p);
        if ( pTop != p )
        {
            pTop->pLastFocus = p;
        }
        J3DGui_Send(p, WM_SETFOCUS, (WPARAM)J3DGui_ToHwnd(pOld), 0);
        J3DGui_Invalidate(p);
    }
}

HWND WINAPI SetFocus(HWND hwnd)
{
    HWND hOld = J3DGui_ToHwnd(gui_pFocus);
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( p && (!J3DGui_IsReallyEnabled(p)) )
    {
        return NULL;
    }
    J3DGui_SetFocusWindow(p);
    return hOld;
}

HWND WINAPI GetFocus(void)
{
    return J3DGui_ToHwnd(gui_pFocus);
}

HWND WINAPI GetActiveWindow(void)
{
    J3DGuiWindow* p = J3DGui_ActiveDialog();
    return p ? J3DGui_ToHwnd(p) : J3DGuiHost_GameWindow();
}

HWND WINAPI SetActiveWindow(HWND hwnd)
{
    HWND hOld = GetActiveWindow();
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( p )
    {
        gui_pActive = J3DGui_TopLevel(p);
    }
    return hOld;
}

HWND WINAPI GetForegroundWindow(void)
{
    return GetActiveWindow();
}

BOOL WINAPI SetForegroundWindow(HWND hwnd)
{
    SetActiveWindow(hwnd);
    return TRUE;
}

J3DGuiWindow* J3DGui_GetCaptureWindow(void)
{
    return gui_pCapture;
}

HWND WINAPI SetCapture(HWND hwnd)
{
    HWND hOld = J3DGui_ToHwnd(gui_pCapture);
    gui_pCapture = J3DGui_FromHwnd(hwnd);
    return hOld;
}

BOOL WINAPI ReleaseCapture(void)
{
    J3DGuiWindow* pOld = gui_pCapture;
    gui_pCapture = NULL;
    if ( pOld )
    {
        J3DGui_Send(pOld, WM_CAPTURECHANGED, 0, 0);
    }
    return TRUE;
}

HWND WINAPI GetCapture(void)
{
    return J3DGui_ToHwnd(gui_pCapture);
}

/* ---- System metrics ------------------------------------------------------------------------------------------------ */

int WINAPI GetSystemMetrics(int index)
{
    int w;
    int h;
    J3DGuiHost_GetScreenSize(&w, &h);
    switch ( index )
    {
        case SM_CXSCREEN:
        case SM_CXFULLSCREEN:
        case SM_CXMAXIMIZED:
            return w;
        case SM_CYSCREEN:
        case SM_CYMAXIMIZED:
            return h;
        case SM_CYFULLSCREEN:
            return h - 18;
        case SM_CYCAPTION:
            return 18;
        case SM_CXBORDER:
        case SM_CYBORDER:
            return 1;
        case SM_CXDLGFRAME:
        case SM_CYDLGFRAME:
            return 3;
        case SM_CXFRAME:
        case SM_CYFRAME:
            return 4;
        case SM_CXEDGE:
        case SM_CYEDGE:
            return 2;
        case SM_CXVSCROLL:
        case SM_CYHSCROLL:
        case SM_CYVSCROLL:
        case SM_CXHSCROLL:
            return 16;
        case SM_CXICON:
        case SM_CYICON:
            return 32;
        case SM_CXSMICON:
        case SM_CYSMICON:
            return 16;
        case SM_CXCURSOR:
        case SM_CYCURSOR:
            return 32;
        case SM_CMOUSEBUTTONS:
            return 3;
        case SM_MOUSEPRESENT:
            return 1;
        default:
            return 0;
    }
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT uiParam, PVOID pvParam, UINT winIni)
{
    (void)uiParam;
    (void)winIni;
    if ( action == SPI_GETWORKAREA && pvParam )
    {
        gui_ScreenRect((RECT*)pvParam);
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI MessageBeep(UINT type)
{
    (void)type;
    return TRUE;
}

/* ---- Rectangles ------------------------------------------------------------------------------------------------------ */

BOOL WINAPI SetRect(LPRECT pRect, int left, int top, int right, int bottom)
{
    if ( !pRect )
    {
        return FALSE;
    }
    pRect->left = left;
    pRect->top = top;
    pRect->right = right;
    pRect->bottom = bottom;
    return TRUE;
}

BOOL WINAPI SetRectEmpty(LPRECT pRect)
{
    return SetRect(pRect, 0, 0, 0, 0);
}

BOOL WINAPI CopyRect(LPRECT pDst, const RECT* pSrc)
{
    if ( !pDst || !pSrc )
    {
        return FALSE;
    }
    *pDst = *pSrc;
    return TRUE;
}

BOOL WINAPI IsRectEmpty(const RECT* pRect)
{
    return !pRect || pRect->right <= pRect->left || pRect->bottom <= pRect->top;
}

BOOL WINAPI EqualRect(const RECT* pA, const RECT* pB)
{
    return pA && pB && pA->left == pB->left && pA->top == pB->top && pA->right == pB->right && pA->bottom == pB->bottom;
}

BOOL WINAPI OffsetRect(LPRECT pRect, int dx, int dy)
{
    if ( !pRect )
    {
        return FALSE;
    }
    pRect->left += dx;
    pRect->right += dx;
    pRect->top += dy;
    pRect->bottom += dy;
    return TRUE;
}

BOOL WINAPI InflateRect(LPRECT pRect, int dx, int dy)
{
    if ( !pRect )
    {
        return FALSE;
    }
    pRect->left -= dx;
    pRect->right += dx;
    pRect->top -= dy;
    pRect->bottom += dy;
    return TRUE;
}

BOOL WINAPI IntersectRect(LPRECT pDst, const RECT* pA, const RECT* pB)
{
    RECT r;
    if ( !pDst || !pA || !pB )
    {
        return FALSE;
    }
    r.left = GUI_MAX(pA->left, pB->left);
    r.top = GUI_MAX(pA->top, pB->top);
    r.right = GUI_MIN(pA->right, pB->right);
    r.bottom = GUI_MIN(pA->bottom, pB->bottom);
    if ( IsRectEmpty(&r) )
    {
        SetRectEmpty(pDst);
        return FALSE;
    }
    *pDst = r;
    return TRUE;
}

BOOL WINAPI UnionRect(LPRECT pDst, const RECT* pA, const RECT* pB)
{
    if ( !pDst || !pA || !pB )
    {
        return FALSE;
    }
    if ( IsRectEmpty(pA) )
    {
        *pDst = *pB;
    }
    else if ( IsRectEmpty(pB) )
    {
        *pDst = *pA;
    }
    else
    {
        RECT r;
        r.left = GUI_MIN(pA->left, pB->left);
        r.top = GUI_MIN(pA->top, pB->top);
        r.right = GUI_MAX(pA->right, pB->right);
        r.bottom = GUI_MAX(pA->bottom, pB->bottom);
        *pDst = r;
    }
    return !IsRectEmpty(pDst);
}
