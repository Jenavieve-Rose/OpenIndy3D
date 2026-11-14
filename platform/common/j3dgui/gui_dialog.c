/*
 * j3dgui dialogs (PORT-PLAN.md P6.4): dialogs from the templates, the default dialog procedure and its keyboard
 * interface, the modal loop, input routing, and composing the dialogs over the game's frame.
 *
 * Templates are in dialog units. The base units are those of the template's font, MS Sans Serif 8 (6 x 13 pixels:
 * a dialog unit is 1.5 pixels across and 1.625 down), as Windows computed them. The game then rescales its own dialogs
 * to the screen (jonesConfig_InitDialog), as it did on Windows.
 *
 * The modal loop is DialogBoxParam's: it disables the owner, pumps the host's events (keyboard and mouse to the
 * dialogs, the rest to the game's window), repaints what changed, composes and presents, until EndDialog. A run that
 * cannot answer a dialog (J3DGuiHost_IsInteractive is false and no test hook is installed) dismisses it as IDCANCEL
 * and says so on stderr.
 */
#include "gui_internal.h"
#include "j3dgui.h"

#include <std/Win95/stdDisplay.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUI_BASEUNIT_X 6
#define GUI_BASEUNIT_Y 13

typedef struct GuiPosted
{
    HWND hwnd;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
} GuiPosted;

static int gui_modalDepth = 0;
static int gui_bPresentRequested = 0;
static uint32_t* gui_pBackground = NULL;   // the frame under the dialogs, captured when the first one opened
static int gui_bgWidth = 0;
static int gui_bgHeight = 0;
static int gui_bRecapture = 0;
static GuiPosted gui_aPosted[64];
static int gui_numPosted = 0;
static int gui_bQuitting = 0;
static J3DGuiModalHook gui_pfModalHook = NULL;

// dragging a dialog by its caption
static J3DGuiWindow* gui_pDragging = NULL;
static POINT gui_dragStart;
static RECT gui_dragRect;

// double clicks
static J3DGuiWindow* gui_pLastClick = NULL;
static DWORD gui_lastClickTime = 0;
static POINT gui_lastClickPos;

void J3DGuiHost_RequestPresent(void)
{
    gui_bPresentRequested = 1;
}

int J3DGui_IsModalActive(void)
{
    return gui_modalDepth > 0;
}

int J3DGui_ModalDepth(void)
{
    return gui_modalDepth;
}

void J3DGui_SetModalHook(J3DGuiModalHook pfHook)
{
    gui_pfModalHook = pfHook;
}

void J3DGui_RecaptureBackground(void)
{
    gui_bRecapture = 1;
}

/* ---- Painting and composing ------------------------------------------------------------------------------------------ */

static void gui_PaintTree(J3DGuiWindow* p, int bForce)
{
    J3DGuiWindow* pChild;
    int bPaint = bForce || p->bDirty;

    if ( !(p->style & WS_VISIBLE) )
    {
        p->bDirty = 0;
        return;
    }
    if ( bPaint )
    {
        HDC hdc;
        p->bDirty = 0;
        J3DGui_Send(p, WM_NCPAINT, 1, 0);
        hdc = J3DGui_GetWindowDC(p, 0);
        J3DGui_Send(p, WM_ERASEBKGND, (WPARAM)hdc, 0);
        J3DGui_ReleaseWindowDC(hdc);
        J3DGui_Send(p, WM_PAINT, 0, 0);
    }
    for ( pChild = p->pFirstChild; pChild; pChild = pChild->pNext )
    {
        gui_PaintTree(pChild, bPaint);
    }
}

void J3DGui_Paint(J3DGuiWindow* pTop)
{
    if ( pTop && pTop->pSurface )
    {
        gui_PaintTree(pTop, 0);
    }
}

static void gui_CaptureBackground(void)
{
    void* pSurface;
    uint32_t w;
    uint32_t h;
    int32_t pitch;
    uint32_t y;

    free(gui_pBackground);
    gui_pBackground = NULL;
    gui_bgWidth = gui_bgHeight = 0;
    if ( stdDisplay_LockBackBuffer(&pSurface, &w, &h, &pitch) != 0 )
    {
        return;
    }
    gui_pBackground = malloc((size_t)w * h * 4);
    if ( gui_pBackground )
    {
        for ( y = 0; y < h; ++y )
        {
            memcpy(gui_pBackground + (size_t)y * w, (uint8_t*)pSurface + (size_t)y * (size_t)pitch, (size_t)w * 4);
        }
        gui_bgWidth = (int)w;
        gui_bgHeight = (int)h;
    }
    stdDisplay_UnlockBackBuffer();
}

// The back buffer: the captured frame, then every visible top-level window's surface, bottom to top.
static void gui_Compose(void)
{
    void* pSurface;
    uint32_t w;
    uint32_t h;
    int32_t pitch;
    J3DGuiWindow* ap[64];
    int n;
    int i;
    uint32_t y;

    if ( gui_bRecapture || !gui_pBackground )
    {
        gui_bRecapture = 0;
        gui_CaptureBackground();
    }
    n = J3DGui_GetTopLevels(ap, 64);
    for ( i = 0; i < n; ++i )
    {
        J3DGui_Paint(ap[i]);
    }
    if ( stdDisplay_LockBackBuffer(&pSurface, &w, &h, &pitch) != 0 )
    {
        return;
    }
    for ( y = 0; y < h; ++y )
    {
        uint32_t* pRow = (uint32_t*)((uint8_t*)pSurface + (size_t)y * (size_t)pitch);
        if ( gui_pBackground && (int)w == gui_bgWidth && (int)y < gui_bgHeight )
        {
            memcpy(pRow, gui_pBackground + (size_t)y * w, (size_t)w * 4);
        }
        else
        {
            memset(pRow, 0, (size_t)w * 4);
        }
    }
    for ( i = 0; i < n; ++i )
    {
        J3DGuiSurface* s = ap[i]->pSurface;
        int x0 = ap[i]->rect.left;
        int y0 = ap[i]->rect.top;
        int sy;
        if ( !s )
        {
            continue;
        }
        for ( sy = 0; sy < s->height; ++sy )
        {
            int dy = y0 + sy;
            int sx0 = 0;
            int sx1 = s->width;
            if ( dy < 0 || dy >= (int)h )
            {
                continue;
            }
            if ( x0 < 0 )
            {
                sx0 = -x0;
            }
            if ( x0 + sx1 > (int)w )
            {
                sx1 = (int)w - x0;
            }
            if ( sx1 > sx0 )
            {
                memcpy((uint8_t*)pSurface + (size_t)dy * (size_t)pitch + (size_t)(x0 + sx0) * 4, s->pPixels + (size_t)sy * (size_t)s->pitch + sx0,
                       (size_t)(sx1 - sx0) * 4);
            }
        }
    }
    J3DGui_ComposeOverlay((uint32_t*)pSurface, (int)w, (int)h, pitch / 4);
    stdDisplay_UnlockBackBuffer();
}

static void gui_PresentIfNeeded(int bForce)
{
    if ( gui_bPresentRequested || bForce )
    {
        gui_bPresentRequested = 0;
        gui_Compose();
        J3DGuiHost_Present();
    }
}

// Puts the captured frame back, so the game's next present does not show a closed dialog for a frame.
static void gui_RestoreBackground(void)
{
    void* pSurface;
    uint32_t w;
    uint32_t h;
    int32_t pitch;
    uint32_t y;
    if ( !gui_pBackground || stdDisplay_LockBackBuffer(&pSurface, &w, &h, &pitch) != 0 )
    {
        return;
    }
    if ( (int)w == gui_bgWidth )
    {
        for ( y = 0; y < h && (int)y < gui_bgHeight; ++y )
        {
            memcpy((uint8_t*)pSurface + (size_t)y * (size_t)pitch, gui_pBackground + (size_t)y * w, (size_t)w * 4);
        }
    }
    stdDisplay_UnlockBackBuffer();
}

/* ---- Posted messages ------------------------------------------------------------------------------------------------- */

BOOL J3DGui_PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if ( gui_numPosted >= (int)(sizeof(gui_aPosted) / sizeof(gui_aPosted[0])) )
    {
        return FALSE;
    }
    gui_aPosted[gui_numPosted].hwnd = hwnd;
    gui_aPosted[gui_numPosted].msg = msg;
    gui_aPosted[gui_numPosted].wParam = wParam;
    gui_aPosted[gui_numPosted].lParam = lParam;
    gui_numPosted++;
    return TRUE;
}

static void gui_DeliverPosted(void)
{
    while ( gui_numPosted > 0 )
    {
        GuiPosted m = gui_aPosted[0];
        memmove(&gui_aPosted[0], &gui_aPosted[1], (size_t)(gui_numPosted - 1) * sizeof(gui_aPosted[0]));
        gui_numPosted--;
        if ( J3DGui_FromHwnd(m.hwnd) )
        {
            SendMessageA(m.hwnd, m.msg, m.wParam, m.lParam);
        }
    }
}

/* ---- Keyboard interface ---------------------------------------------------------------------------------------------- */

// The control's own flags, as Windows tests them: the dialog itself is still hidden when its first focus is chosen.
static int gui_IsTabStop(J3DGuiWindow* p)
{
    return (p->style & WS_TABSTOP) && (p->style & WS_VISIBLE) && !(p->style & WS_DISABLED);
}

static J3DGuiWindow* gui_DefaultButton(J3DGuiWindow* pDlg)
{
    J3DGuiWindow* p;
    for ( p = pDlg->pFirstChild; p; p = p->pNext )
    {
        if ( p->cls == GUI_CLASS_BUTTON && (p->style & 0xF) == BS_DEFPUSHBUTTON && J3DGui_IsReallyVisible(p) && J3DGui_IsReallyEnabled(p) )
        {
            return p;
        }
    }
    return NULL;
}

HWND WINAPI GetNextDlgTabItem(HWND hDlg, HWND hCtl, BOOL bPrevious)
{
    J3DGuiWindow* pDlg = J3DGui_FromHwnd(hDlg);
    J3DGuiWindow* pCur = J3DGui_FromHwnd(hCtl);
    J3DGuiWindow* ap[256];
    int n = 0;
    int i;
    int at = -1;
    J3DGuiWindow* p;
    if ( !pDlg )
    {
        return NULL;
    }
    for ( p = pDlg->pFirstChild; p && n < 256; p = p->pNext )
    {
        if ( p == pCur )
        {
            at = n;
        }
        if ( gui_IsTabStop(p) || p == pCur )
        {
            ap[n++] = p;
        }
    }
    if ( n == 0 )
    {
        return NULL;
    }
    for ( i = 1; i <= n; ++i )
    {
        int k = at < 0 ? (bPrevious ? n - i : i - 1) : ((at + (bPrevious ? -i : i)) % n + n) % n;
        if ( gui_IsTabStop(ap[k]) )
        {
            return J3DGui_ToHwnd(ap[k]);
        }
    }
    return NULL;
}

// The previous or next control of the same group (radio buttons and the like), for the arrow keys.
static J3DGuiWindow* gui_NextInGroup(J3DGuiWindow* pDlg, J3DGuiWindow* pCur, int bPrevious)
{
    J3DGuiWindow* ap[256];
    int n = 0;
    int at = -1;
    int start;
    int end;
    int i;
    J3DGuiWindow* p;
    for ( p = pDlg->pFirstChild; p && n < 256; p = p->pNext )
    {
        if ( p == pCur )
        {
            at = n;
        }
        ap[n++] = p;
    }
    if ( at < 0 )
    {
        return NULL;
    }
    // a group runs from a WS_GROUP control to the one before the next WS_GROUP control
    for ( start = at; start > 0 && !(ap[start]->style & WS_GROUP); --start )
    {
    }
    for ( end = at + 1; end < n && !(ap[end]->style & WS_GROUP); ++end )
    {
    }
    for ( i = 1; i < end - start; ++i )
    {
        int k = start + ((at - start + (bPrevious ? -i : i)) % (end - start) + (end - start)) % (end - start);
        if ( J3DGui_IsReallyVisible(ap[k]) && J3DGui_IsReallyEnabled(ap[k]) )
        {
            return ap[k];
        }
    }
    return NULL;
}

static void gui_FocusControl(J3DGuiWindow* p)
{
    if ( !p )
    {
        return;
    }
    J3DGui_SetFocusWindow(p);
    if ( p->cls == GUI_CLASS_EDIT )
    {
        J3DGui_Send(p, EM_SETSEL, 0, -1);
    }
}

// Alt+letter or a plain letter: the control whose text has that mnemonic.
static J3DGuiWindow* gui_FindMnemonic(J3DGuiWindow* pDlg, char ch)
{
    J3DGuiWindow* p;
    char lower = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
    for ( p = pDlg->pFirstChild; p; p = p->pNext )
    {
        const char* s;
        if ( !J3DGui_IsReallyVisible(p) || !J3DGui_IsReallyEnabled(p) )
        {
            continue;
        }
        for ( s = p->pText; s && *s; ++s )
        {
            if ( s[0] == '&' && s[1] && s[1] != '&' )
            {
                char c = s[1];
                if ( c >= 'A' && c <= 'Z' )
                {
                    c = (char)(c + 32);
                }
                if ( c == lower )
                {
                    if ( p->cls == GUI_CLASS_STATIC )
                    {
                        // a label's mnemonic moves to the control after it
                        return p->pNext;
                    }
                    return p;
                }
                break;
            }
            if ( s[0] == '&' && s[1] == '&' )
            {
                ++s;
            }
        }
    }
    return NULL;
}

static void gui_ClickButton(J3DGuiWindow* p)
{
    if ( p && p->cls == GUI_CLASS_BUTTON )
    {
        J3DGui_Send(p, BM_CLICK, 0, 0);
    }
}

// IsDialogMessage's keyboard part. Returns 1 when the dialog handled the key.
static int gui_DialogKey(J3DGuiWindow* pDlg, UINT msg, WPARAM vk, LPARAM lParam)
{
    J3DGuiWindow* pFocus = J3DGui_GetFocusWindow();
    LRESULT code = 0;
    (void)lParam;
    if ( pFocus && J3DGui_TopLevel(pFocus) != pDlg )
    {
        pFocus = NULL;
    }
    if ( pFocus )
    {
        MSG m;
        memset(&m, 0, sizeof(m));
        m.hwnd = J3DGui_ToHwnd(pFocus);
        m.message = msg;
        m.wParam = vk;
        code = J3DGui_Send(pFocus, WM_GETDLGCODE, vk, (LPARAM)&m);
    }
    if ( code & DLGC_WANTALLKEYS )
    {
        return 0;
    }
    if ( msg == WM_SYSKEYDOWN || (msg == WM_KEYDOWN && GetKeyState(VK_MENU) < 0) )
    {
        if ( (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') )
        {
            J3DGuiWindow* pTarget = gui_FindMnemonic(pDlg, (char)vk);
            if ( pTarget )
            {
                gui_FocusControl(pTarget);
                gui_ClickButton(pTarget);
            }
            return 1;
        }
        return 0;
    }
    if ( msg != WM_KEYDOWN )
    {
        return 0;
    }
    switch ( vk )
    {
        case VK_TAB:
            if ( code & DLGC_WANTTAB )
            {
                return 0;
            }
            gui_FocusControl(J3DGui_FromHwnd(GetNextDlgTabItem(J3DGui_ToHwnd(pDlg), J3DGui_ToHwnd(pFocus), GetKeyState(VK_SHIFT) < 0)));
            return 1;
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
            if ( !pFocus || (code & DLGC_WANTARROWS) )
            {
                return 0;
            }
            {
                J3DGuiWindow* pNext = gui_NextInGroup(pDlg, pFocus, vk == VK_LEFT || vk == VK_UP);
                if ( pNext )
                {
                    gui_FocusControl(pNext);
                    if ( pNext->cls == GUI_CLASS_BUTTON && ((pNext->style & 0xF) == BS_AUTORADIOBUTTON) )
                    {
                        gui_ClickButton(pNext);
                    }
                }
            }
            return 1;
        case VK_RETURN:
        {
            J3DGuiWindow* pButton;
            if ( pFocus && pFocus->cls == GUI_CLASS_BUTTON && ((pFocus->style & 0xF) == BS_PUSHBUTTON || (pFocus->style & 0xF) == BS_DEFPUSHBUTTON) )
            {
                gui_ClickButton(pFocus);
                return 1;
            }
            pButton = gui_DefaultButton(pDlg);
            if ( pButton )
            {
                gui_ClickButton(pButton);
            }
            else
            {
                J3DGui_Send(pDlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)GetDlgItem(J3DGui_ToHwnd(pDlg), IDOK));
            }
            return 1;
        }
        case VK_ESCAPE:
            J3DGui_Send(pDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)GetDlgItem(J3DGui_ToHwnd(pDlg), IDCANCEL));
            return 1;
        case VK_SPACE:
            return 0; // the focused button handles it
        default:
            if ( !(code & (DLGC_WANTCHARS | DLGC_HASSETSEL)) && !pFocus )
            {
                return 0;
            }
            return 0;
    }
}

BOOL WINAPI IsDialogMessageA(HWND hDlg, LPMSG pMsg)
{
    J3DGuiWindow* pDlg = J3DGui_FromHwnd(hDlg);
    if ( !pDlg || !pMsg )
    {
        return FALSE;
    }
    if ( pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN )
    {
        return gui_DialogKey(pDlg, pMsg->message, pMsg->wParam, pMsg->lParam);
    }
    return FALSE;
}

/* ---- The default dialog procedure ------------------------------------------------------------------------------------ */

LRESULT CALLBACK J3DGui_DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    INT_PTR handled = 0;

    if ( !p )
    {
        return 0;
    }
    if ( p->pfnDlgProc && msg != WM_NCCREATE && msg != WM_CREATE )
    {
        p->dlgMsgResult = 0;
        handled = p->pfnDlgProc(hwnd, msg, wParam, lParam);
        if ( !J3DGui_FromHwnd(hwnd) )
        {
            return 0; // the procedure destroyed the dialog
        }
        if ( handled )
        {
            switch ( msg )
            {
                // these return the procedure's result directly
                case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
                case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC: case WM_COMPAREITEM:
                case WM_VKEYTOITEM: case WM_CHARTOITEM: case WM_QUERYDRAGICON: case WM_INITDIALOG:
                    return handled;
                default:
                    return p->dlgMsgResult;
            }
        }
    }
    switch ( msg )
    {
        case WM_INITDIALOG:
            return TRUE;
        case WM_CLOSE:
            // Close (the caption's button, Alt+F4): IDCANCEL, if the dialog has such a button or none at all
            J3DGui_Send(p, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)GetDlgItem(hwnd, IDCANCEL));
            return 0;
        case WM_COMMAND:
            return 0;
        case WM_SYSCOMMAND:
            if ( (wParam & 0xFFF0) == SC_CLOSE )
            {
                J3DGui_Send(p, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_ERASEBKGND:
        {
            RECT client;
            HBRUSH hBrush = (HBRUSH)J3DGui_Send(p, WM_CTLCOLORDLG, wParam, (LPARAM)hwnd);
            GetClientRect(hwnd, &client);
            if ( hBrush )
            {
                FillRect((HDC)wParam, &client, hBrush);
            }
            else
            {
                J3DGui_FillRect32((HDC)wParam, &client, J3DGui_SysColor(COLOR_3DFACE));
            }
            return 1;
        }
        case DM_GETDEFID:
        {
            J3DGuiWindow* pDef = gui_DefaultButton(p);
            return pDef ? MAKELRESULT(pDef->id, DC_HASDEFID) : 0;
        }
        case WM_NEXTDLGCTL:
            if ( lParam )
            {
                gui_FocusControl(J3DGui_FromHwnd((HWND)wParam));
            }
            else
            {
                gui_FocusControl(J3DGui_FromHwnd(GetNextDlgTabItem(hwnd, GetFocus(), wParam != 0)));
            }
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ---- Dialogs from templates ------------------------------------------------------------------------------------------ */

static const J3DGuiTemplate* gui_FindTemplate(LPCSTR pName)
{
    int id;
    int i;
    if ( (uintptr_t)pName < 0x10000 )
    {
        id = (int)(uintptr_t)pName;
    }
    else if ( pName[0] == '#' )
    {
        id = atoi(pName + 1);
    }
    else
    {
        id = atoi(pName);
    }
    for ( i = 0; i < J3DGui_numTemplates; ++i )
    {
        if ( J3DGui_aTemplates[i].id == id )
        {
            return &J3DGui_aTemplates[i];
        }
    }
    return NULL;
}

BOOL WINAPI MapDialogRect(HWND hDlg, LPRECT pRect)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hDlg);
    int bx = p && p->baseUnitX ? p->baseUnitX : GUI_BASEUNIT_X;
    int by = p && p->baseUnitY ? p->baseUnitY : GUI_BASEUNIT_Y;
    if ( !pRect )
    {
        return FALSE;
    }
    pRect->left = MulDiv(pRect->left, bx, 4);
    pRect->right = MulDiv(pRect->right, bx, 4);
    pRect->top = MulDiv(pRect->top, by, 8);
    pRect->bottom = MulDiv(pRect->bottom, by, 8);
    return TRUE;
}

LONG WINAPI GetDialogBaseUnits(void)
{
    return MAKELONG(GUI_BASEUNIT_X, GUI_BASEUNIT_Y);
}

static HFONT gui_TemplateFont(const J3DGuiTemplate* t)
{
    // MS Sans Serif 8 is the stock dialog font; any other is created (and owned by the dialog)
    if ( t->fontSize == 8 && strcmp(t->pFontName, "MS Sans Serif") == 0 )
    {
        return J3DGui_DefaultFont();
    }
    return CreateFontA(-MulDiv(t->fontSize, 96, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, t->pFontName);
}

static J3DGuiWindow* gui_CreateDialog(const J3DGuiTemplate* t, HWND hParent, DLGPROC pfnDlgProc, LPARAM param, HINSTANCE hInstance)
{
    J3DGuiWindow* pParentDlg = J3DGui_FromHwnd(hParent);
    DWORD style = t->style;
    J3DGuiWindow* pDlg;
    J3DGuiWindow* pFirst = NULL;
    HFONT hFont = gui_TemplateFont(t);
    RECT rect;
    RECT client;
    int screenW;
    int screenH;
    int i;
    J3DGuiWindow* pChild;
    INT_PTR initResult;

    // A child-style template (the file dialogs' 154 and 159) is shown as a dialog of its own.
    if ( style & WS_CHILD )
    {
        style = (style & ~(DWORD)(WS_CHILD | DS_CONTROL)) | WS_POPUP | DS_MODALFRAME;
    }

    SetRect(&client, 0, 0, t->cx, t->cy);
    client.right = MulDiv(t->cx, GUI_BASEUNIT_X, 4);
    client.bottom = MulDiv(t->cy, GUI_BASEUNIT_Y, 8);
    rect = client;
    AdjustWindowRectEx(&rect, style & ~(DWORD)WS_VISIBLE, FALSE, 0);
    OffsetRect(&rect, -rect.left, -rect.top);
    J3DGuiHost_GetScreenSize(&screenW, &screenH);
    // Centred on the screen. The templates' own positions are for a 640x480 desktop, and retail's code moves its dialogs
    // after WM_INITDIALOG in any case (jonesConfig_SetDialogTitleAndPosition).
    OffsetRect(&rect, (screenW - (rect.right - rect.left)) / 2, (screenH - (rect.bottom - rect.top)) / 2);

    pDlg = J3DGui_CreateWindow(GUI_CLASS_DIALOG, "#32770", t->pCaption, style & ~(DWORD)WS_VISIBLE, 0, &rect, NULL,
                               pParentDlg ? J3DGui_ToHwnd(J3DGui_TopLevel(pParentDlg)) : hParent, 0, hInstance, NULL);
    if ( !pDlg )
    {
        return NULL;
    }
    pDlg->pfnDlgProc = pfnDlgProc;
    pDlg->baseUnitX = GUI_BASEUNIT_X;
    pDlg->baseUnitY = GUI_BASEUNIT_Y;
    pDlg->hFont = hFont;

    for ( i = 0; i < t->numItems; ++i )
    {
        const J3DGuiTemplateItem* it = &t->pItems[i];
        J3DGuiClass cls = J3DGui_ClassFromName(it->pClass);
        RECT r;
        char aIconText[16];
        const char* pText = it->pText;
        J3DGuiWindow* pCtl;
        SetRect(&r, it->x, it->y, it->x + it->cx, it->y + it->cy);
        MapDialogRect(J3DGui_ToHwnd(pDlg), &r);
        if ( it->iconId )
        {
            snprintf(aIconText, sizeof(aIconText), "#%d", it->iconId);
            pText = aIconText;
        }
        pCtl = J3DGui_CreateWindow(cls, it->pClass, pText, it->style, it->exStyle, &r, pDlg, NULL, it->id, hInstance, NULL);
        if ( pCtl )
        {
            J3DGui_Send(pCtl, WM_SETFONT, (WPARAM)hFont, 0);
            if ( !pFirst && gui_IsTabStop(pCtl) )
            {
                pFirst = pCtl;
            }
        }
    }
    for ( i = 0; i < J3DGui_numTemplateInits; ++i )
    {
        const J3DGuiTemplateInit* in = &J3DGui_aTemplateInits[i];
        if ( in->dialogId == t->id )
        {
            SendDlgItemMessageA(J3DGui_ToHwnd(pDlg), in->controlId, in->msg, 0, (LPARAM)in->pData);
        }
    }

    initResult = J3DGui_Send(pDlg, WM_INITDIALOG, (WPARAM)J3DGui_ToHwnd(pFirst), param);
    if ( !J3DGui_FromHwnd(J3DGui_ToHwnd(pDlg)) )
    {
        return NULL;
    }
    if ( initResult && !J3DGui_GetFocusWindow() )
    {
        gui_FocusControl(pFirst ? J3DGui_FromHwnd(GetNextDlgTabItem(J3DGui_ToHwnd(pDlg), NULL, FALSE)) : NULL);
    }
    else if ( initResult )
    {
        J3DGuiWindow* pFocus = J3DGui_GetFocusWindow();
        if ( !pFocus || J3DGui_TopLevel(pFocus) != pDlg )
        {
            gui_FocusControl(J3DGui_FromHwnd(GetNextDlgTabItem(J3DGui_ToHwnd(pDlg), NULL, FALSE)));
        }
    }
    for ( pChild = pDlg->pFirstChild; pChild; pChild = pChild->pNext )
    {
        J3DGui_Invalidate(pChild);
    }
    return pDlg;
}

HWND WINAPI CreateDialogParamA(HINSTANCE hInstance, LPCSTR pTemplate, HWND hParent, DLGPROC pfnDlgProc, LPARAM param)
{
    const J3DGuiTemplate* t = gui_FindTemplate(pTemplate);
    J3DGuiWindow* pDlg;
    if ( !t )
    {
        return NULL;
    }
    pDlg = gui_CreateDialog(t, hParent, pfnDlgProc, param, hInstance);
    if ( !pDlg )
    {
        return NULL;
    }
    if ( t->style & WS_VISIBLE )
    {
        ShowWindow(J3DGui_ToHwnd(pDlg), SW_SHOW);
    }
    return J3DGui_ToHwnd(pDlg);
}

BOOL WINAPI EndDialog(HWND hDlg, INT_PTR result)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hDlg);
    if ( !p )
    {
        return FALSE;
    }
    p->bEnded = 1;
    p->result = result;
    ShowWindow(hDlg, SW_HIDE);
    return TRUE;
}

/* ---- The modal loop ---------------------------------------------------------------------------------------------------- */

void J3DGui_RunModal(J3DGuiWindow* pDlg)
{
    HWND hDlg = J3DGui_ToHwnd(pDlg);
    int iteration = 0;
    int bInteractive = J3DGuiHost_IsInteractive() || gui_pfModalHook;

    if ( gui_modalDepth++ == 0 )
    {
        gui_CaptureBackground();
    }
    J3DGuiHost_RequestPresent();
    while ( J3DGui_FromHwnd(hDlg) && !pDlg->bEnded )
    {
        if ( !bInteractive )
        {
            fprintf(stderr, "j3dgui: dialog \"%s\" dismissed (IDCANCEL): nothing can answer it in this run\n", pDlg->pText);
            J3DGui_Send(pDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            if ( J3DGui_FromHwnd(hDlg) && !pDlg->bEnded )
            {
                EndDialog(hDlg, IDCANCEL);
            }
            break;
        }
        gui_DeliverPosted();
        if ( gui_pfModalHook )
        {
            gui_pfModalHook(hDlg, iteration);
            if ( !J3DGui_FromHwnd(hDlg) || pDlg->bEnded )
            {
                break;
            }
        }
        gui_PresentIfNeeded(iteration == 0);
        if ( J3DGuiHost_Pump(gui_pfModalHook == NULL) )
        {
            gui_bQuitting = 1;
            if ( J3DGui_FromHwnd(hDlg) && !pDlg->bEnded )
            {
                EndDialog(hDlg, IDCANCEL);
            }
        }
        if ( gui_bQuitting && J3DGui_FromHwnd(hDlg) && !pDlg->bEnded )
        {
            EndDialog(hDlg, IDCANCEL);
        }
        iteration++;
    }
    if ( --gui_modalDepth == 0 )
    {
        gui_RestoreBackground();
        free(gui_pBackground);
        gui_pBackground = NULL;
    }
    else
    {
        J3DGuiHost_RequestPresent();
        gui_PresentIfNeeded(1);
    }
}

INT_PTR WINAPI DialogBoxParamA(HINSTANCE hInstance, LPCSTR pTemplate, HWND hParent, DLGPROC pfnDlgProc, LPARAM param)
{
    const J3DGuiTemplate* t = gui_FindTemplate(pTemplate);
    J3DGuiWindow* pDlg;
    J3DGuiWindow* pOwner = J3DGui_TopLevel(J3DGui_FromHwnd(hParent));
    INT_PTR result;
    J3DGuiWindow* pPrevFocus = J3DGui_GetFocusWindow();
    int bOwnerWasEnabled = 0;

    if ( !t )
    {
        fprintf(stderr, "j3dgui: DialogBoxParam: no template %u\n", (unsigned)(uintptr_t)pTemplate);
        return -1;
    }
    if ( gui_bQuitting )
    {
        return IDCANCEL;
    }
    pDlg = gui_CreateDialog(t, hParent, pfnDlgProc, param, hInstance);
    if ( !pDlg )
    {
        return -1;
    }
    if ( pOwner )
    {
        bOwnerWasEnabled = !EnableWindow(J3DGui_ToHwnd(pOwner), FALSE);
    }
    pDlg->bModal = 1;
    ShowWindow(J3DGui_ToHwnd(pDlg), SW_SHOW);
    SetWindowPos(J3DGui_ToHwnd(pDlg), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    J3DGui_RunModal(pDlg);

    result = J3DGui_FromHwnd(J3DGui_ToHwnd(pDlg)) ? pDlg->result : IDCANCEL;
    if ( pOwner && J3DGui_FromHwnd(J3DGui_ToHwnd(pOwner)) )
    {
        if ( bOwnerWasEnabled )
        {
            EnableWindow(J3DGui_ToHwnd(pOwner), TRUE);
        }
        J3DGui_Invalidate(pOwner);
    }
    if ( J3DGui_FromHwnd(J3DGui_ToHwnd(pDlg)) )
    {
        DestroyWindow(J3DGui_ToHwnd(pDlg));
    }
    if ( pPrevFocus && J3DGui_FromHwnd(J3DGui_ToHwnd(pPrevFocus)) )
    {
        J3DGui_SetFocusWindow(pPrevFocus);
    }
    return result;
}

/* ---- Input routing ----------------------------------------------------------------------------------------------------- */

// The windows that may take input while a modal dialog is up: the topmost dialog, and popups it owns (a list).
static int gui_AcceptsInput(J3DGuiWindow* p)
{
    J3DGuiWindow* pTop = J3DGui_TopLevel(p);
    J3DGuiWindow* pActive = J3DGui_ActiveDialog();
    if ( !pTop )
    {
        return 0;
    }
    if ( pTop == pActive )
    {
        return 1;
    }
    // a combo box's list is a top-level window owned by the combo box
    return pTop->cls == GUI_CLASS_COMBOLBOX;
}

void J3DGui_Input(UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* pDlg = J3DGui_ActiveDialog();
    if ( msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP )
    {
        J3DGui_TrackKey(msg, wParam);
    }
    if ( msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL )
    {
        J3DGui_SetCursorPosition((short)LOWORD(lParam), (short)HIWORD(lParam));
    }
    J3DGuiHost_RequestPresent();
    if ( !pDlg )
    {
        return;
    }

    if ( msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR )
    {
        J3DGuiWindow* pFocus = J3DGui_GetFocusWindow();
        J3DGuiWindow* pList = NULL;
        J3DGuiWindow* ap[64];
        int n = J3DGui_GetTopLevels(ap, 64);
        // an open drop-down list takes the keys
        if ( n > 0 && ap[n - 1]->cls == GUI_CLASS_COMBOLBOX )
        {
            pList = ap[n - 1];
            J3DGui_Send(pList, msg, wParam, lParam);
            return;
        }
        if ( pFocus && J3DGui_TopLevel(pFocus) != pDlg )
        {
            pFocus = NULL;
        }
        if ( msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN )
        {
            if ( msg == WM_SYSKEYDOWN && wParam == VK_F4 )
            {
                J3DGui_Send(pDlg, WM_SYSCOMMAND, SC_CLOSE, 0);
                return;
            }
            if ( gui_DialogKey(pDlg, msg, wParam, lParam) )
            {
                return;
            }
        }
        J3DGui_Send(pFocus ? pFocus : pDlg, msg, wParam, lParam);
        return;
    }

    if ( msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST )
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        J3DGuiWindow* pTarget = J3DGui_GetCaptureWindow();
        POINT origin;
        WPARAM keys = wParam;

        if ( gui_pDragging )
        {
            if ( msg == WM_MOUSEMOVE )
            {
                RECT r = gui_dragRect;
                OffsetRect(&r, pt.x - gui_dragStart.x, pt.y - gui_dragStart.y);
                MoveWindow(J3DGui_ToHwnd(gui_pDragging), r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
            }
            else if ( msg == WM_LBUTTONUP )
            {
                gui_pDragging = NULL;
            }
            return;
        }
        if ( msg == WM_MOUSEWHEEL )
        {
            // the wheel goes to the window under the pointer, its position in screen coordinates
            J3DGuiWindow* pUnder = J3DGui_WindowFromPoint(pt);
            if ( pUnder && gui_AcceptsInput(pUnder) )
            {
                J3DGui_Send(pUnder, msg, wParam, lParam);
            }
            return;
        }
        if ( !pTarget )
        {
            pTarget = J3DGui_WindowFromPoint(pt);
            if ( (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) )
            {
                // a click outside an open drop-down list closes it
                J3DGuiWindow* ap[64];
                int n = J3DGui_GetTopLevels(ap, 64);
                if ( n > 0 && ap[n - 1]->cls == GUI_CLASS_COMBOLBOX && J3DGui_TopLevel(pTarget) != ap[n - 1] )
                {
                    J3DGui_Send(ap[n - 1], WM_CANCELMODE, 0, 0);
                    if ( pTarget && J3DGui_TopLevel(pTarget) != pDlg )
                    {
                        return;
                    }
                }
            }
            if ( !pTarget || !gui_AcceptsInput(pTarget) )
            {
                if ( msg == WM_LBUTTONDOWN )
                {
                    MessageBeep(MB_OK);
                }
                return;
            }
            if ( !J3DGui_IsReallyEnabled(pTarget) )
            {
                return; // a disabled control swallows the click, as in Windows
            }
        }

        // the caption and its close button
        if ( !pTarget->pParent && pTarget->cls != GUI_CLASS_COMBOLBOX && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) )
        {
            POINT w;
            RECT client;
            J3DGui_WindowOrigin(pTarget, &w);
            J3DGui_GetClientInWindow(pTarget, &client);
            OffsetRect(&client, w.x, w.y);
            if ( !PtInRect(&client, pt) )
            {
                RECT close;
                J3DGuiDraw_CloseButtonRect(pTarget, &close);
                OffsetRect(&close, w.x, w.y);
                if ( (pTarget->style & WS_SYSMENU) && PtInRect(&close, pt) )
                {
                    if ( msg == WM_LBUTTONUP )
                    {
                        J3DGui_Send(pTarget, WM_SYSCOMMAND, SC_CLOSE, 0);
                    }
                }
                else if ( msg == WM_LBUTTONDOWN && (pTarget->style & WS_CAPTION) == WS_CAPTION && pt.y < client.top )
                {
                    gui_pDragging = pTarget;
                    gui_dragStart = pt;
                    gui_dragRect = pTarget->rect;
                }
                return;
            }
        }

        if ( msg == WM_LBUTTONDOWN )
        {
            DWORD now = GetTickCount();
            if ( pTarget == gui_pLastClick && now - gui_lastClickTime < 500 && abs(pt.x - gui_lastClickPos.x) <= 4 && abs(pt.y - gui_lastClickPos.y) <= 4
                 && (pTarget->cls == GUI_CLASS_LISTBOX || pTarget->cls == GUI_CLASS_LISTVIEW || pTarget->cls == GUI_CLASS_COMBOLBOX) )
            {
                msg = WM_LBUTTONDBLCLK;
                gui_pLastClick = NULL;
            }
            else
            {
                gui_pLastClick = pTarget;
                gui_lastClickTime = now;
                gui_lastClickPos = pt;
            }
        }
        J3DGui_ClientOrigin(pTarget, &origin);
        J3DGui_Send(pTarget, msg, keys, MAKELPARAM(pt.x - origin.x, pt.y - origin.y));
    }
}

/* ---- Message boxes ------------------------------------------------------------------------------------------------------ */

typedef struct GuiMsgBox
{
    const char* pText;
    UINT type;
} GuiMsgBox;

static BOOL CALLBACK gui_MessageBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch ( msg )
    {
        case WM_INITDIALOG:
            return TRUE;
        case WM_COMMAND:
            if ( HIWORD(wParam) == BN_CLICKED )
            {
                int id = LOWORD(wParam);
                if ( id == IDCANCEL && !GetDlgItem(hwnd, IDCANCEL) )
                {
                    // Escape or Close with no Cancel button: OK if there is only OK, else nothing
                    if ( GetDlgItem(hwnd, IDOK) && !GetDlgItem(hwnd, IDNO) )
                    {
                        id = IDOK;
                    }
                    else
                    {
                        return TRUE;
                    }
                }
                EndDialog(hwnd, id);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

int WINAPI MessageBoxA(HWND hwnd, LPCSTR pText, LPCSTR pCaption, UINT type)
{
    static const struct { UINT type; int ids[3]; const char* names[3]; } aButtons[] = {
        { MB_OK, { IDOK }, { "OK" } },
        { MB_OKCANCEL, { IDOK, IDCANCEL }, { "OK", "Cancel" } },
        { MB_ABORTRETRYIGNORE, { IDABORT, IDRETRY, IDIGNORE }, { "&Abort", "&Retry", "&Ignore" } },
        { MB_YESNOCANCEL, { IDYES, IDNO, IDCANCEL }, { "&Yes", "&No", "Cancel" } },
        { MB_YESNO, { IDYES, IDNO }, { "&Yes", "&No" } },
        { MB_RETRYCANCEL, { IDRETRY, IDCANCEL }, { "&Retry", "Cancel" } },
    };
    UINT kind = type & 0xF;
    int numButtons = 0;
    const int* pIds = NULL;
    const char* const* pNames = NULL;
    HFONT hFont = J3DGui_DefaultFont();
    RECT textRect;
    RECT client;
    RECT rect;
    int screenW;
    int screenH;
    int buttonW = 75;
    int buttonH = 23;
    int i;
    J3DGuiWindow* pBox;
    J3DGuiWindow* pOwner;
    INT_PTR result;
    HDC hdcMeasure;
    size_t k;

    for ( k = 0; k < sizeof(aButtons) / sizeof(aButtons[0]); ++k )
    {
        if ( aButtons[k].type == kind )
        {
            pIds = aButtons[k].ids;
            pNames = aButtons[k].names;
            numButtons = aButtons[k].ids[2] ? 3 : aButtons[k].ids[1] ? 2 : 1;
        }
    }
    if ( !pIds )
    {
        pIds = aButtons[0].ids;
        pNames = aButtons[0].names;
        numButtons = 1;
    }
    if ( !J3DGuiHost_IsInteractive() && !gui_pfModalHook )
    {
        fprintf(stderr, "[MessageBox] %s: %s\n", pCaption ? pCaption : "", pText ? pText : "");
        return pIds[0] == IDYES ? IDYES : pIds[0];
    }

    // lay out: text wrapped at most 2/3 of the screen wide, buttons centred under it
    J3DGuiHost_GetScreenSize(&screenW, &screenH);
    hdcMeasure = CreateCompatibleDC(NULL);
    SelectObject(hdcMeasure, hFont);
    SetRect(&textRect, 0, 0, GUI_MAX(screenW * 2 / 3, 200), 0);
    DrawTextA(hdcMeasure, pText ? pText : "", -1, &textRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    DeleteDC(hdcMeasure);
    SetRect(&client, 0, 0, GUI_MAX(textRect.right + 24, numButtons * (buttonW + 6) + 24), textRect.bottom + 18 + buttonH + 24);
    rect = client;
    AdjustWindowRectEx(&rect, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, FALSE, 0);
    OffsetRect(&rect, -rect.left + (screenW - (rect.right - rect.left)) / 2, -rect.top + (screenH - (rect.bottom - rect.top)) / 2);

    pOwner = J3DGui_TopLevel(J3DGui_FromHwnd(hwnd));
    pBox = J3DGui_CreateWindow(GUI_CLASS_DIALOG, "#32770", pCaption ? pCaption : "Error", WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 0,
                               &rect, NULL, pOwner ? J3DGui_ToHwnd(pOwner) : hwnd, 0, NULL, NULL);
    if ( !pBox )
    {
        return pIds[0];
    }
    pBox->pfnDlgProc = gui_MessageBoxProc;
    pBox->hFont = hFont;
    {
        RECT r = textRect;
        OffsetRect(&r, 12, 12);
        J3DGui_CreateWindow(GUI_CLASS_STATIC, "Static", pText ? pText : "", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, &r, pBox, NULL, 0xFFFF, NULL, NULL);
    }
    for ( i = 0; i < numButtons; ++i )
    {
        RECT r;
        int total = numButtons * buttonW + (numButtons - 1) * 6;
        int x = (client.right - total) / 2 + i * (buttonW + 6);
        int defIndex = (type & MB_DEFMASK) == MB_DEFBUTTON2 ? 1 : (type & MB_DEFMASK) == MB_DEFBUTTON3 ? 2 : 0;
        J3DGuiWindow* pButton;
        SetRect(&r, x, client.bottom - 12 - buttonH, x + buttonW, client.bottom - 12);
        pButton = J3DGui_CreateWindow(GUI_CLASS_BUTTON, "Button", pNames[i],
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == 0 ? WS_GROUP : 0) | (i == defIndex ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0, &r, pBox,
                                      NULL, pIds[i], NULL, NULL);
        if ( pButton && i == defIndex )
        {
            J3DGui_SetFocusWindow(pButton);
        }
    }
    if ( pOwner )
    {
        EnableWindow(J3DGui_ToHwnd(pOwner), FALSE);
    }
    pBox->bModal = 1;
    ShowWindow(J3DGui_ToHwnd(pBox), SW_SHOW);
    SetWindowPos(J3DGui_ToHwnd(pBox), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    J3DGui_RunModal(pBox);
    result = J3DGui_FromHwnd(J3DGui_ToHwnd(pBox)) ? pBox->result : IDCANCEL;
    if ( pOwner && J3DGui_FromHwnd(J3DGui_ToHwnd(pOwner)) )
    {
        EnableWindow(J3DGui_ToHwnd(pOwner), TRUE);
    }
    if ( J3DGui_FromHwnd(J3DGui_ToHwnd(pBox)) )
    {
        DestroyWindow(J3DGui_ToHwnd(pBox));
    }
    return (int)result;
}
