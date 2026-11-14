/*
 * j3dgui controls 1 (PORT-PLAN.md P6.6): Button (push, default, check box, radio button, group box, owner-drawn),
 * Static (text, icon, frames) and the single-line Edit, with Windows 98's behaviour and look.
 */
#include "gui_internal.h"
#include "j3dgui.h"

#include <stdlib.h>
#include <string.h>

/* ---- Button ------------------------------------------------------------------------------------------------------------ */

typedef struct GuiButton
{
    int check;      // BST_UNCHECKED, BST_CHECKED, BST_INDETERMINATE
    int bPushed;    // drawn pushed (mouse held inside, or Space held)
    int bTracking;  // the mouse went down on it
    int bSpace;
    HANDLE hImage;  // BM_SETIMAGE
} GuiButton;

static GuiButton* gui_ButtonData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        p->pData = calloc(1, sizeof(GuiButton));
    }
    return p->pData;
}

static int gui_ButtonType(const J3DGuiWindow* p)
{
    return (int)(p->style & 0xF);
}

static int gui_IsCheckType(int type)
{
    return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE;
}

static int gui_IsRadioType(int type)
{
    return type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
}

static void gui_ButtonClick(J3DGuiWindow* p)
{
    GuiButton* b = gui_ButtonData(p);
    int type = gui_ButtonType(p);
    HWND hwnd = J3DGui_ToHwnd(p);

    if ( type == BS_AUTOCHECKBOX )
    {
        b->check = !b->check;
        J3DGui_Invalidate(p);
    }
    else if ( type == BS_AUTO3STATE )
    {
        b->check = (b->check + 1) % 3;
        J3DGui_Invalidate(p);
    }
    else if ( type == BS_AUTORADIOBUTTON && p->pParent )
    {
        // check this one and clear the other automatic radio buttons of its group
        J3DGuiWindow* q;
        J3DGuiWindow* pStart = p->pParent->pFirstChild;
        for ( q = p->pParent->pFirstChild; q && q != p; q = q->pNext )
        {
            if ( q->style & WS_GROUP )
            {
                pStart = q;
            }
        }
        for ( q = pStart; q; q = q->pNext )
        {
            if ( q != pStart && (q->style & WS_GROUP) )
            {
                break;
            }
            if ( q->cls == GUI_CLASS_BUTTON && gui_ButtonType(q) == BS_AUTORADIOBUTTON && q != p )
            {
                if ( gui_ButtonData(q)->check )
                {
                    gui_ButtonData(q)->check = 0;
                    J3DGui_Invalidate(q);
                }
            }
        }
        b->check = 1;
        J3DGui_Invalidate(p);
    }
    J3DGui_NotifyParent(p, BN_CLICKED);
    (void)hwnd;
}

static void gui_ButtonPaint(J3DGuiWindow* p)
{
    GuiButton* b = gui_ButtonData(p);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT client;
    int type = gui_ButtonType(p);
    int bEnabled = J3DGui_IsReallyEnabled(p);
    int bFocus = J3DGui_GetFocusWindow() == p;
    HFONT hFont = J3DGui_GetFont(p);
    int textH = J3DGui_FontHeight(hFont);
    SIZE size;
    uint32_t textColor = J3DGui_SysColor(COLOR_BTNTEXT);

    GetClientRect(J3DGui_ToHwnd(p), &client);
    J3DGui_TextExtent(hFont, p->pText, (int)strlen(p->pText), &size);

    if ( type == BS_OWNERDRAW )
    {
        DRAWITEMSTRUCT dis;
        memset(&dis, 0, sizeof(dis));
        dis.CtlType = ODT_BUTTON;
        dis.CtlID = (UINT)p->id;
        dis.itemAction = ODA_DRAWENTIRE;
        dis.itemState = (b->bPushed ? ODS_SELECTED : 0) | (bFocus ? ODS_FOCUS : 0) | (bEnabled ? 0 : ODS_DISABLED);
        dis.hwndItem = J3DGui_ToHwnd(p);
        dis.hDC = hdc;
        dis.rcItem = client;
        if ( p->pParent )
        {
            J3DGui_Send(p->pParent, WM_DRAWITEM, (WPARAM)p->id, (LPARAM)&dis);
        }
        EndPaint(J3DGui_ToHwnd(p), &ps);
        return;
    }

    if ( type == BS_GROUPBOX )
    {
        RECT frame = client;
        frame.top += textH / 2;
        J3DGui_DrawEdge3D(hdc, &frame, GUI_EDGE_ETCHED);
        if ( p->pText[0] )
        {
            RECT gap;
            SetRect(&gap, 6, 0, 6 + size.cx + 4, textH);
            J3DGui_FillRect32(hdc, &gap, J3DGui_SysColor(COLOR_3DFACE));
            if ( bEnabled )
            {
                J3DGui_DrawText32(hdc, hFont, p->pText, -1, 8, 0, textColor, 1);
            }
            else
            {
                J3DGui_DrawEtchedText(hdc, hFont, p->pText, 8, 0, 1);
            }
        }
        EndPaint(J3DGui_ToHwnd(p), &ps);
        return;
    }

    J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(COLOR_3DFACE));
    if ( gui_IsCheckType(type) || gui_IsRadioType(type) )
    {
        int box = gui_IsRadioType(type) ? 12 : 13;
        RECT boxRect;
        int textX = box + 4;
        int textY = (client.bottom - textH) / 2;
        SetRect(&boxRect, 0, (client.bottom - box) / 2, box, (client.bottom - box) / 2 + box);
        if ( gui_IsRadioType(type) )
        {
            J3DGui_DrawRadio(hdc, &boxRect, b->check, bEnabled, b->bPushed);
        }
        else
        {
            J3DGui_DrawCheck(hdc, &boxRect, b->check, bEnabled, b->bPushed);
        }
        if ( bEnabled )
        {
            J3DGui_DrawText32(hdc, hFont, p->pText, -1, textX, textY, textColor, 1);
        }
        else
        {
            J3DGui_DrawEtchedText(hdc, hFont, p->pText, textX, textY, 1);
        }
        if ( bFocus && p->pText[0] )
        {
            RECT f;
            SetRect(&f, textX - 1, textY - 1, textX + size.cx + 1, textY + textH + 1);
            J3DGui_DrawFocusRect32(hdc, &f);
        }
    }
    else
    {
        // a push button
        RECT r = client;
        int bDefault = type == BS_DEFPUSHBUTTON || bFocus;
        int x;
        int y;
        if ( b->bPushed )
        {
            J3DGui_DrawEdge3D(hdc, &r, GUI_EDGE_PUSHED);
        }
        else
        {
            J3DGui_DrawEdge3D(hdc, &r, bDefault ? GUI_EDGE_DEFAULT : GUI_EDGE_RAISED);
        }
        x = (client.right - size.cx) / 2 + (b->bPushed ? 1 : 0);
        y = (client.bottom - textH) / 2 + (b->bPushed ? 1 : 0);
        if ( b->hImage )
        {
            J3DGui_DrawIconAt(hdc, (HICON)b->hImage, (client.right - 32) / 2, (client.bottom - 32) / 2, 0, 0);
        }
        else if ( bEnabled )
        {
            J3DGui_DrawText32(hdc, hFont, p->pText, -1, x, y, textColor, 1);
        }
        else
        {
            J3DGui_DrawEtchedText(hdc, hFont, p->pText, x, y, 1);
        }
        if ( bFocus )
        {
            RECT f = client;
            InflateRect(&f, -4, -4);
            J3DGui_DrawFocusRect32(hdc, &f);
        }
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

static LRESULT CALLBACK gui_ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiButton* b;
    int type;
    if ( !p )
    {
        return 0;
    }
    b = gui_ButtonData(p);
    type = gui_ButtonType(p);
    switch ( msg )
    {
        case WM_NCDESTROY:
            free(p->pData);
            p->pData = NULL;
            return 0;
        case WM_PAINT:
            gui_ButtonPaint(p);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCPAINT:
            return 0;
        case WM_GETDLGCODE:
            if ( type == BS_GROUPBOX )
            {
                return DLGC_STATIC;
            }
            if ( gui_IsRadioType(type) )
            {
                return DLGC_BUTTON | DLGC_RADIOBUTTON;
            }
            if ( type == BS_DEFPUSHBUTTON )
            {
                return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
            }
            if ( type == BS_PUSHBUTTON )
            {
                return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
            }
            return DLGC_BUTTON;
        case BM_GETCHECK:
            return b->check;
        case BM_SETCHECK:
            if ( b->check != (int)wParam )
            {
                b->check = (int)wParam;
                J3DGui_Invalidate(p);
            }
            return 0;
        case BM_GETSTATE:
            return b->check | (b->bPushed ? BST_PUSHED : 0) | (J3DGui_GetFocusWindow() == p ? BST_FOCUS : 0);
        case BM_SETSTATE:
            b->bPushed = wParam != 0;
            J3DGui_Invalidate(p);
            return 0;
        case BM_SETSTYLE:
            p->style = (p->style & ~(DWORD)0xFFFF) | (DWORD)(wParam & 0xFFFF);
            J3DGui_Invalidate(p);
            return 0;
        case BM_CLICK:
            if ( J3DGui_IsReallyEnabled(p) )
            {
                gui_ButtonClick(p);
            }
            return 0;
        case BM_SETIMAGE:
        {
            HANDLE hOld = b->hImage;
            b->hImage = (HANDLE)lParam;
            J3DGui_Invalidate(p);
            return (LRESULT)hOld;
        }
        case BM_GETIMAGE:
            return (LRESULT)b->hImage;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if ( type == BS_GROUPBOX )
            {
                return 0;
            }
            SetFocus(hwnd);
            SetCapture(hwnd);
            b->bTracking = 1;
            b->bPushed = 1;
            J3DGui_Invalidate(p);
            return 0;
        case WM_MOUSEMOVE:
            if ( b->bTracking )
            {
                RECT client;
                POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
                int bInside;
                GetClientRect(hwnd, &client);
                bInside = PtInRect(&client, pt);
                if ( bInside != b->bPushed )
                {
                    b->bPushed = bInside;
                    J3DGui_Invalidate(p);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if ( b->bTracking )
            {
                int bClick = b->bPushed;
                b->bTracking = 0;
                b->bPushed = 0;
                ReleaseCapture();
                J3DGui_Invalidate(p);
                if ( bClick )
                {
                    gui_ButtonClick(p);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if ( wParam == VK_SPACE && !b->bSpace )
            {
                b->bSpace = 1;
                b->bPushed = 1;
                J3DGui_Invalidate(p);
            }
            return 0;
        case WM_KEYUP:
            if ( wParam == VK_SPACE && b->bSpace )
            {
                b->bSpace = 0;
                b->bPushed = 0;
                J3DGui_Invalidate(p);
                gui_ButtonClick(p);
            }
            return 0;
        case WM_KILLFOCUS:
            if ( b->bSpace || b->bTracking )
            {
                b->bSpace = b->bTracking = b->bPushed = 0;
                J3DGui_Invalidate(p);
            }
            return 0;
        case WM_SETFOCUS:
            J3DGui_Invalidate(p);
            return 0;
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

BOOL WINAPI CheckDlgButton(HWND hDlg, int id, UINT check)
{
    SendDlgItemMessageA(hDlg, id, BM_SETCHECK, check, 0);
    return TRUE;
}

UINT WINAPI IsDlgButtonChecked(HWND hDlg, int id)
{
    return (UINT)SendDlgItemMessageA(hDlg, id, BM_GETCHECK, 0, 0);
}

BOOL WINAPI CheckRadioButton(HWND hDlg, int first, int last, int check)
{
    int id;
    for ( id = first; id <= last; ++id )
    {
        if ( GetDlgItem(hDlg, id) )
        {
            SendDlgItemMessageA(hDlg, id, BM_SETCHECK, id == check ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
    return TRUE;
}

/* ---- Static ------------------------------------------------------------------------------------------------------------ */

typedef struct GuiStatic
{
    HICON hIcon;
    HBITMAP hBitmap;
} GuiStatic;

static GuiStatic* gui_StaticData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        p->pData = calloc(1, sizeof(GuiStatic));
    }
    return p->pData;
}

static void gui_StaticPaint(J3DGuiWindow* p)
{
    GuiStatic* s = gui_StaticData(p);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT client;
    DWORD type = p->style & 0x1F;
    HFONT hFont = J3DGui_GetFont(p);
    HBRUSH hBrush = NULL;

    GetClientRect(J3DGui_ToHwnd(p), &client);
    if ( p->pParent )
    {
        hBrush = (HBRUSH)J3DGui_Send(p->pParent, WM_CTLCOLORSTATIC, (WPARAM)hdc, (LPARAM)J3DGui_ToHwnd(p));
    }
    switch ( type )
    {
        case SS_ICON:
            if ( hBrush )
            {
                FillRect(hdc, &client, hBrush);
            }
            if ( s->hIcon )
            {
                J3DGui_DrawIconAt(hdc, s->hIcon, 0, 0, (p->style & SS_REALSIZEIMAGE) ? 0 : client.right, (p->style & SS_REALSIZEIMAGE) ? 0 : client.bottom);
            }
            break;
        case SS_BITMAP:
            if ( s->hBitmap )
            {
                HDC hMem = CreateCompatibleDC(NULL);
                BITMAP bm;
                GetObjectA(s->hBitmap, sizeof(bm), &bm);
                SelectObject(hMem, s->hBitmap);
                BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, hMem, 0, 0, SRCCOPY);
                DeleteDC(hMem);
            }
            break;
        case SS_BLACKFRAME:
        case SS_GRAYFRAME:
        case 9: // SS_WHITEFRAME
        {
            HBRUSH h = CreateSolidBrush(GetSysColor(type == SS_BLACKFRAME ? COLOR_3DDKSHADOW : type == SS_GRAYFRAME ? COLOR_3DSHADOW : COLOR_3DHILIGHT));
            FrameRect(hdc, &client, h);
            DeleteObject(h);
            break;
        }
        case 4: // SS_BLACKRECT
        case 5: // SS_GRAYRECT
        case 6: // SS_WHITERECT
            J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(type == 4 ? COLOR_3DDKSHADOW : type == 5 ? COLOR_3DSHADOW : COLOR_3DHILIGHT));
            break;
        case SS_OWNERDRAW:
        {
            DRAWITEMSTRUCT dis;
            memset(&dis, 0, sizeof(dis));
            dis.CtlType = ODT_STATIC;
            dis.CtlID = (UINT)p->id;
            dis.itemAction = ODA_DRAWENTIRE;
            dis.hwndItem = J3DGui_ToHwnd(p);
            dis.hDC = hdc;
            dis.rcItem = client;
            if ( p->pParent )
            {
                J3DGui_Send(p->pParent, WM_DRAWITEM, (WPARAM)p->id, (LPARAM)&dis);
            }
            break;
        }
        default:
        {
            // text: SS_LEFT (word-wrapped), SS_CENTER, SS_RIGHT, SS_LEFTNOWORDWRAP, SS_SIMPLE
            UINT format = DT_NOCLIP;
            RECT r = client;
            if ( hBrush )
            {
                FillRect(hdc, &client, hBrush);
            }
            else
            {
                J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(COLOR_3DFACE));
            }
            if ( p->style & SS_SUNKEN )
            {
                J3DGui_DrawEdge3D(hdc, &r, GUI_EDGE_STATIC_SUNKEN);
            }
            if ( type == SS_CENTER )
            {
                format |= DT_CENTER;
            }
            else if ( type == SS_RIGHT )
            {
                format |= DT_RIGHT;
            }
            if ( type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE || (p->style & SS_CENTERIMAGE) )
            {
                format |= DT_SINGLELINE;
            }
            else
            {
                format |= DT_WORDBREAK;
            }
            if ( p->style & SS_CENTERIMAGE )
            {
                format |= DT_VCENTER;
            }
            if ( p->style & SS_NOPREFIX )
            {
                format |= DT_NOPREFIX;
            }
            SelectObject(hdc, hFont);
            SetBkMode(hdc, TRANSPARENT);
            if ( !J3DGui_IsReallyEnabled(p) )
            {
                RECT shifted = r;
                OffsetRect(&shifted, 1, 1);
                SetTextColor(hdc, GetSysColor(COLOR_3DHILIGHT));
                DrawTextA(hdc, p->pText, -1, &shifted, format);
                SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            }
            else if ( !hBrush )
            {
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            }
            DrawTextA(hdc, p->pText, -1, &r, format);
            break;
        }
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

static LRESULT CALLBACK gui_StaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiStatic* s;
    if ( !p )
    {
        return 0;
    }
    s = gui_StaticData(p);
    switch ( msg )
    {
        case WM_CREATE:
            if ( (p->style & 0x1F) == SS_ICON && p->pText[0] == '#' )
            {
                s->hIcon = LoadIconA(p->hInstance, MAKEINTRESOURCEA(atoi(p->pText + 1)));
                J3DGui_SetText(p, "");
            }
            return 0;
        case WM_NCDESTROY:
            free(p->pData);
            p->pData = NULL;
            return 0;
        case WM_PAINT:
            gui_StaticPaint(p);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_STATIC;
        case STM_SETICON:
        {
            HICON hOld = s->hIcon;
            s->hIcon = (HICON)wParam;
            J3DGui_Invalidate(p);
            return (LRESULT)hOld;
        }
        case STM_GETICON:
            return (LRESULT)s->hIcon;
        case STM_SETIMAGE:
        {
            HANDLE hOld;
            if ( wParam == IMAGE_ICON )
            {
                hOld = s->hIcon;
                s->hIcon = (HICON)lParam;
            }
            else
            {
                hOld = s->hBitmap;
                s->hBitmap = (HBITMAP)lParam;
            }
            J3DGui_Invalidate(p);
            return (LRESULT)hOld;
        }
        case STM_GETIMAGE:
            return wParam == IMAGE_ICON ? (LRESULT)s->hIcon : (LRESULT)s->hBitmap;
        case WM_LBUTTONDOWN:
            if ( p->style & SS_NOTIFY )
            {
                J3DGui_NotifyParent(p, STN_CLICKED);
            }
            return 0;
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ---- Edit (single line) ------------------------------------------------------------------------------------------------ */

typedef struct GuiEdit
{
    int limit;
    int caret;
    int selStart;   // the anchor
    int scroll;     // the first visible character
    int bTracking;
} GuiEdit;

static GuiEdit* gui_EditData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        GuiEdit* e = calloc(1, sizeof(GuiEdit));
        if ( e )
        {
            e->limit = 30000;
        }
        p->pData = e;
    }
    return p->pData;
}

static void gui_EditSelRange(GuiEdit* e, int* pA, int* pB)
{
    *pA = GUI_MIN(e->selStart, e->caret);
    *pB = GUI_MAX(e->selStart, e->caret);
}

static void gui_EditReplaceSel(J3DGuiWindow* p, const char* pInsert, int bNotify)
{
    GuiEdit* e = gui_EditData(p);
    int a;
    int b;
    size_t len = strlen(p->pText);
    size_t ins = strlen(pInsert);
    char* pNew;
    gui_EditSelRange(e, &a, &b);
    if ( (int)(len - (size_t)(b - a) + ins) > e->limit )
    {
        ins = (size_t)GUI_MAX(0, e->limit - (int)(len - (size_t)(b - a)));
        if ( ins == 0 && a == b )
        {
            MessageBeep(MB_OK);
            return;
        }
    }
    pNew = malloc(len - (size_t)(b - a) + ins + 1);
    if ( !pNew )
    {
        return;
    }
    memcpy(pNew, p->pText, (size_t)a);
    memcpy(pNew + a, pInsert, ins);
    memcpy(pNew + a + ins, p->pText + b, len - (size_t)b + 1);
    free(p->pText);
    p->pText = pNew;
    e->caret = e->selStart = a + (int)ins;
    J3DGui_Invalidate(p);
    if ( bNotify )
    {
        J3DGui_NotifyParent(p, EN_UPDATE);
        J3DGui_NotifyParent(p, EN_CHANGE);
    }
}

static int gui_EditCharAtX(J3DGuiWindow* p, int x)
{
    GuiEdit* e = gui_EditData(p);
    HFONT hFont = J3DGui_GetFont(p);
    int len = (int)strlen(p->pText);
    int i;
    for ( i = e->scroll; i < len; ++i )
    {
        SIZE a;
        SIZE b;
        J3DGui_TextExtent(hFont, p->pText + e->scroll, i - e->scroll, &a);
        J3DGui_TextExtent(hFont, p->pText + e->scroll, i - e->scroll + 1, &b);
        if ( x < 1 + (a.cx + b.cx) / 2 )
        {
            return i;
        }
    }
    return len;
}

static void gui_EditScrollToCaret(J3DGuiWindow* p)
{
    GuiEdit* e = gui_EditData(p);
    HFONT hFont = J3DGui_GetFont(p);
    RECT client;
    SIZE size;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    if ( e->caret < e->scroll )
    {
        e->scroll = e->caret;
    }
    for ( ;; )
    {
        J3DGui_TextExtent(hFont, p->pText + e->scroll, e->caret - e->scroll, &size);
        if ( size.cx <= client.right - 3 || e->scroll >= e->caret )
        {
            break;
        }
        e->scroll++;
    }
}

static void gui_EditPaint(J3DGuiWindow* p)
{
    GuiEdit* e = gui_EditData(p);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT client;
    HFONT hFont = J3DGui_GetFont(p);
    int bEnabled = J3DGui_IsReallyEnabled(p) && !(p->style & ES_READONLY);
    int textH = J3DGui_FontHeight(hFont);
    int y;
    int a;
    int b;
    const char* pVisible = p->pText + GUI_MIN(e->scroll, (int)strlen(p->pText));
    int bFocus = J3DGui_GetFocusWindow() == p;

    GetClientRect(J3DGui_ToHwnd(p), &client);
    J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(bEnabled ? COLOR_WINDOW : COLOR_3DFACE));
    y = (client.bottom - textH) / 2;
    gui_EditSelRange(e, &a, &b);
    J3DGui_DrawText32(hdc, hFont, pVisible, -1, 1, y, J3DGui_SysColor(bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT), -1);
    if ( bFocus && a != b )
    {
        // the selection: highlight behind, then the text again in the highlight colour
        int sa = GUI_MAX(a, e->scroll) - e->scroll;
        int sb = GUI_MAX(b, e->scroll) - e->scroll;
        SIZE x0;
        SIZE x1;
        RECT sel;
        J3DGui_TextExtent(hFont, pVisible, sa, &x0);
        J3DGui_TextExtent(hFont, pVisible, sb, &x1);
        SetRect(&sel, 1 + x0.cx, y, 1 + x1.cx, y + textH);
        J3DGui_FillRect32(hdc, &sel, J3DGui_SysColor(COLOR_HIGHLIGHT));
        J3DGui_DrawText32(hdc, hFont, pVisible + sa, sb - sa, 1 + x0.cx, y, J3DGui_SysColor(COLOR_HIGHLIGHTTEXT), -1);
    }
    if ( bFocus )
    {
        SIZE cx;
        RECT caret;
        J3DGui_TextExtent(hFont, pVisible, GUI_MAX(0, e->caret - e->scroll), &cx);
        SetRect(&caret, 1 + cx.cx, y, 2 + cx.cx, y + textH);
        J3DGui_FillRect32(hdc, &caret, J3DGui_SysColor(COLOR_WINDOWTEXT));
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

static LRESULT CALLBACK gui_EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiEdit* e;
    int len;
    if ( !p )
    {
        return 0;
    }
    e = gui_EditData(p);
    len = (int)strlen(p->pText);
    switch ( msg )
    {
        case WM_NCDESTROY:
            free(p->pData);
            p->pData = NULL;
            return 0;
        case WM_PAINT:
            gui_EditPaint(p);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS;
        case WM_SETTEXT:
            J3DGui_SetText(p, (const char*)lParam);
            if ( (int)strlen(p->pText) > e->limit )
            {
                p->pText[e->limit] = 0;
            }
            e->caret = e->selStart = e->scroll = 0;
            J3DGui_Invalidate(p);
            return TRUE;
        case EM_LIMITTEXT:
            e->limit = wParam ? (int)wParam : 30000;
            return 0;
        case EM_SETSEL:
        {
            int a = (int)wParam;
            int b = (int)lParam;
            if ( a < 0 )
            {
                e->selStart = e->caret;
                return 0;
            }
            if ( b < 0 || b > len )
            {
                b = len;
            }
            e->selStart = GUI_MIN(a, len);
            e->caret = b;
            gui_EditScrollToCaret(p);
            J3DGui_Invalidate(p);
            return 0;
        }
        case EM_GETSEL:
        {
            int a;
            int b;
            gui_EditSelRange(e, &a, &b);
            if ( wParam )
            {
                *(DWORD*)wParam = (DWORD)a;
            }
            if ( lParam )
            {
                *(DWORD*)lParam = (DWORD)b;
            }
            return MAKELRESULT(a, b);
        }
        case EM_REPLACESEL:
            gui_EditReplaceSel(p, lParam ? (const char*)lParam : "", 1);
            gui_EditScrollToCaret(p);
            return 0;
        case WM_CHAR:
            if ( wParam == '\b' )
            {
                int a;
                int b;
                gui_EditSelRange(e, &a, &b);
                if ( a == b && a > 0 )
                {
                    e->selStart = a - 1;
                }
                gui_EditReplaceSel(p, "", 1);
            }
            else if ( wParam >= 32 && wParam < 256 && wParam != 127 )
            {
                char s[2] = { (char)wParam, 0 };
                if ( (p->style & ES_NUMBER) && (wParam < '0' || wParam > '9') )
                {
                    MessageBeep(MB_OK);
                    return 0;
                }
                gui_EditReplaceSel(p, s, 1);
            }
            gui_EditScrollToCaret(p);
            return 0;
        case WM_KEYDOWN:
        {
            int bShift = GetKeyState(VK_SHIFT) < 0;
            switch ( wParam )
            {
                case VK_LEFT:  e->caret = GUI_MAX(0, e->caret - 1); break;
                case VK_RIGHT: e->caret = GUI_MIN(len, e->caret + 1); break;
                case VK_HOME:  e->caret = 0; break;
                case VK_END:   e->caret = len; break;
                case VK_DELETE:
                {
                    int a;
                    int b;
                    gui_EditSelRange(e, &a, &b);
                    if ( a == b && b < len )
                    {
                        e->caret = b + 1;
                        e->selStart = a;
                    }
                    gui_EditReplaceSel(p, "", 1);
                    gui_EditScrollToCaret(p);
                    return 0;
                }
                default:
                    return 0;
            }
            if ( !bShift )
            {
                e->selStart = e->caret;
            }
            gui_EditScrollToCaret(p);
            J3DGui_Invalidate(p);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            SetCapture(hwnd);
            e->bTracking = 1;
            e->caret = gui_EditCharAtX(p, (short)LOWORD(lParam));
            if ( !(wParam & MK_SHIFT) )
            {
                e->selStart = e->caret;
            }
            J3DGui_Invalidate(p);
            return 0;
        case WM_MOUSEMOVE:
            if ( e->bTracking )
            {
                e->caret = gui_EditCharAtX(p, (short)LOWORD(lParam));
                gui_EditScrollToCaret(p);
                J3DGui_Invalidate(p);
            }
            return 0;
        case WM_LBUTTONUP:
            if ( e->bTracking )
            {
                e->bTracking = 0;
                ReleaseCapture();
            }
            return 0;
        case WM_SETFOCUS:
            J3DGui_NotifyParent(p, EN_SETFOCUS);
            J3DGui_Invalidate(p);
            return 0;
        case WM_KILLFOCUS:
            J3DGui_NotifyParent(p, EN_KILLFOCUS);
            J3DGui_Invalidate(p);
            return 0;
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ---- Class procedures -------------------------------------------------------------------------------------------------- */

LRESULT CALLBACK J3DGui_ListBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK J3DGui_ComboBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK J3DGui_ComboLBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK J3DGui_ScrollBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK J3DGui_TrackBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK J3DGui_ListViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK J3DGui_HeaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC J3DGui_ClassProc(J3DGuiClass cls)
{
    switch ( cls )
    {
        case GUI_CLASS_DIALOG:    return J3DGui_DialogProc;
        case GUI_CLASS_BUTTON:    return gui_ButtonProc;
        case GUI_CLASS_STATIC:    return gui_StaticProc;
        case GUI_CLASS_EDIT:      return gui_EditProc;
        case GUI_CLASS_LISTBOX:   return J3DGui_ListBoxProc;
        case GUI_CLASS_COMBOBOX:  return J3DGui_ComboBoxProc;
        case GUI_CLASS_COMBOLBOX: return J3DGui_ComboLBoxProc;
        case GUI_CLASS_SCROLLBAR: return J3DGui_ScrollBarProc;
        case GUI_CLASS_TRACKBAR:  return J3DGui_TrackBarProc;
        case GUI_CLASS_LISTVIEW:  return J3DGui_ListViewProc;
        case GUI_CLASS_HEADER:    return J3DGui_HeaderProc;
        default:                  return DefWindowProcA;
    }
}
