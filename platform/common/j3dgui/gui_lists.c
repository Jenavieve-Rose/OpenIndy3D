/*
 * j3dgui controls 2 (PORT-PLAN.md P6.7): ListBox and the drop-down list ComboBox (CBS_DROPDOWNLIST, the only kind
 * the game's templates use), with item data (LB_* and CB_*), sorting, keyboard and mouse.
 *
 * Both keep their items in a GuiList. A combo box's drop-down is a top-level "ComboLBox" window it owns, shown below
 * it (above when there is no room), which follows the mouse and closes on a click, Return or Escape, as in Windows 98.
 *
 * As in Windows, a combo box's window height is the height of its selection field; the height it is created or moved
 * with is the height of the drop-down (template COMBOBOX heights are the dropped size).
 */
#include "gui_internal.h"
#include "j3dgui.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct GuiItem
{
    char* pText;
    LPARAM data;
} GuiItem;

typedef struct GuiList
{
    GuiItem* aItems;
    int count;
    int capacity;
    int sel;            // -1: none
    int top;            // first visible item
    int bSorted;
    int itemHeight;
    int horizontalExtent;
    // the vertical scroll bar's drag
    int bThumbDrag;
    int thumbGrab;
    int pushedPart;     // 0 none, 1 up arrow, 2 down arrow
} GuiList;

typedef struct GuiCombo
{
    GuiList list;
    int droppedHeight;
    int droppedWidth;
    J3DGuiWindow* pDrop;    // the drop-down while it is open
    int selOnOpen;
    int bButtonDown;
} GuiCombo;

static void gui_ListInit(GuiList* l, int bSorted)
{
    memset(l, 0, sizeof(*l));
    l->sel = -1;
    l->bSorted = bSorted;
    l->itemHeight = 13;
}

static void gui_ListClear(GuiList* l)
{
    int i;
    for ( i = 0; i < l->count; ++i )
    {
        free(l->aItems[i].pText);
    }
    free(l->aItems);
    l->aItems = NULL;
    l->count = l->capacity = 0;
    l->sel = -1;
    l->top = 0;
}

static int gui_ListInsert(GuiList* l, int index, const char* pText)
{
    if ( l->count == l->capacity )
    {
        int capacity = l->capacity ? l->capacity * 2 : 16;
        GuiItem* a = realloc(l->aItems, (size_t)capacity * sizeof(GuiItem));
        if ( !a )
        {
            return -1; // LB_ERR
        }
        l->aItems = a;
        l->capacity = capacity;
    }
    if ( index < 0 || index > l->count )
    {
        index = l->count;
        if ( l->bSorted )
        {
            for ( index = 0; index < l->count && strcasecmp(l->aItems[index].pText, pText ? pText : "") <= 0; ++index )
            {
            }
        }
    }
    memmove(&l->aItems[index + 1], &l->aItems[index], (size_t)(l->count - index) * sizeof(GuiItem));
    l->aItems[index].pText = strdup(pText ? pText : "");
    l->aItems[index].data = 0;
    l->count++;
    if ( l->sel >= index )
    {
        l->sel++;
    }
    return index;
}

static int gui_ListDelete(GuiList* l, int index)
{
    if ( index < 0 || index >= l->count )
    {
        return -1;
    }
    free(l->aItems[index].pText);
    memmove(&l->aItems[index], &l->aItems[index + 1], (size_t)(l->count - index - 1) * sizeof(GuiItem));
    l->count--;
    if ( l->sel == index )
    {
        l->sel = -1;
    }
    else if ( l->sel > index )
    {
        l->sel--;
    }
    if ( l->top >= l->count )
    {
        l->top = GUI_MAX(0, l->count - 1);
    }
    return l->count;
}

static int gui_ListFind(const GuiList* l, int after, const char* pText, int bExact)
{
    size_t len = pText ? strlen(pText) : 0;
    int i;
    for ( i = 0; i < l->count; ++i )
    {
        int k = (after + 1 + i) % l->count;
        if ( bExact ? strcasecmp(l->aItems[k].pText, pText) == 0 : strncasecmp(l->aItems[k].pText, pText, len) == 0 )
        {
            return k;
        }
    }
    return -1; // LB_ERR, CB_ERR
}

static int gui_ListVisibleRows(const GuiList* l, int height)
{
    return GUI_MAX(1, height / GUI_MAX(1, l->itemHeight));
}

static void gui_ListEnsureVisible(GuiList* l, int height)
{
    int rows = gui_ListVisibleRows(l, height);
    if ( l->sel < 0 )
    {
        return;
    }
    if ( l->sel < l->top )
    {
        l->top = l->sel;
    }
    else if ( l->sel >= l->top + rows )
    {
        l->top = l->sel - rows + 1;
    }
    l->top = GUI_MAX(0, GUI_MIN(l->top, GUI_MAX(0, l->count - rows)));
}

static int gui_ListNeedsScroll(const GuiList* l, int height)
{
    return l->count > gui_ListVisibleRows(l, height);
}

// The vertical scroll bar inside a list's client area (Windows draws it in the non-client area; the effect is the same).
static void gui_ListScrollRects(const RECT* pClient, RECT* pBar, RECT* pUp, RECT* pDown, RECT* pTrack)
{
    SetRect(pBar, pClient->right - 16, pClient->top, pClient->right, pClient->bottom);
    SetRect(pUp, pBar->left, pBar->top, pBar->right, pBar->top + 16);
    SetRect(pDown, pBar->left, pBar->bottom - 16, pBar->right, pBar->bottom);
    SetRect(pTrack, pBar->left, pUp->bottom, pBar->right, pDown->top);
}

static void gui_ListThumb(const GuiList* l, int height, const RECT* pTrack, RECT* pThumb)
{
    int rows = gui_ListVisibleRows(l, height);
    int range = GUI_MAX(1, l->count - rows);
    int trackH = pTrack->bottom - pTrack->top;
    int thumbH = GUI_MAX(8, trackH * rows / GUI_MAX(1, l->count));
    int y;
    thumbH = GUI_MIN(thumbH, trackH);
    y = pTrack->top + (trackH - thumbH) * l->top / range;
    SetRect(pThumb, pTrack->left, y, pTrack->right, y + thumbH);
}

void J3DGui_DrawScrollTrack(HDC hdc, const RECT* pTrack)
{
    // Windows 98's track: a checkerboard of the face colour and white
    int x;
    int y;
    uint32_t face = J3DGui_SysColor(COLOR_3DFACE);
    uint32_t hi = J3DGui_SysColor(COLOR_3DHILIGHT);
    for ( y = pTrack->top; y < pTrack->bottom; ++y )
    {
        for ( x = pTrack->left; x < pTrack->right; ++x )
        {
            RECT px;
            SetRect(&px, x, y, x + 1, y + 1);
            J3DGui_FillRect32(hdc, &px, ((x + y) & 1) ? hi : face);
        }
    }
}

static void gui_ListPaint(J3DGuiWindow* p, GuiList* l, int bFocus, int bHotTrack)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT client;
    HFONT hFont = J3DGui_GetFont(p);
    int rows;
    int i;
    int bScroll;
    (void)bHotTrack;

    GetClientRect(J3DGui_ToHwnd(p), &client);
    J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(J3DGui_IsReallyEnabled(p) ? COLOR_WINDOW : COLOR_3DFACE));
    bScroll = gui_ListNeedsScroll(l, client.bottom) && ((p->style & WS_VSCROLL) || p->cls == GUI_CLASS_COMBOLBOX);
    rows = gui_ListVisibleRows(l, client.bottom);
    for ( i = 0; i < rows + 1 && l->top + i < l->count; ++i )
    {
        int index = l->top + i;
        RECT item;
        int bSelected = index == l->sel;
        SetRect(&item, 0, i * l->itemHeight, client.right - (bScroll ? 16 : 0), (i + 1) * l->itemHeight);
        if ( bSelected )
        {
            J3DGui_FillRect32(hdc, &item, J3DGui_SysColor(COLOR_HIGHLIGHT));
        }
        J3DGui_DrawText32(hdc, hFont, l->aItems[index].pText, -1, item.left + 2, item.top + (l->itemHeight - J3DGui_FontHeight(hFont)) / 2,
                          J3DGui_SysColor(bSelected ? COLOR_HIGHLIGHTTEXT : J3DGui_IsReallyEnabled(p) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT), -1);
        if ( bSelected && bFocus )
        {
            J3DGui_DrawFocusRect32(hdc, &item);
        }
    }
    if ( bScroll )
    {
        RECT bar;
        RECT up;
        RECT down;
        RECT track;
        RECT thumb;
        gui_ListScrollRects(&client, &bar, &up, &down, &track);
        J3DGui_DrawScrollButton(hdc, &up, 0, l->pushedPart == 1, l->top > 0);
        J3DGui_DrawScrollButton(hdc, &down, 1, l->pushedPart == 2, l->top < l->count - rows);
        J3DGui_DrawScrollTrack(hdc, &track);
        gui_ListThumb(l, client.bottom, &track, &thumb);
        J3DGui_DrawEdge3D(hdc, &thumb, GUI_EDGE_RAISED);
        J3DGui_FillRect32(hdc, &thumb, J3DGui_SysColor(COLOR_3DFACE));
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

// A mouse press on a list. Returns the item under it, or -1 (the scroll bar handled it, or no item).
static int gui_ListMouse(J3DGuiWindow* p, GuiList* l, UINT msg, LPARAM lParam)
{
    RECT client;
    POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
    int bScroll;
    int rows;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    bScroll = gui_ListNeedsScroll(l, client.bottom) && ((p->style & WS_VSCROLL) || p->cls == GUI_CLASS_COMBOLBOX);
    rows = gui_ListVisibleRows(l, client.bottom);
    if ( bScroll )
    {
        RECT bar;
        RECT up;
        RECT down;
        RECT track;
        RECT thumb;
        gui_ListScrollRects(&client, &bar, &up, &down, &track);
        gui_ListThumb(l, client.bottom, &track, &thumb);
        if ( l->bThumbDrag )
        {
            if ( msg == WM_MOUSEMOVE )
            {
                int trackH = track.bottom - track.top - (thumb.bottom - thumb.top);
                int y = pt.y - l->thumbGrab - track.top;
                l->top = trackH > 0 ? (y * GUI_MAX(1, l->count - rows) + trackH / 2) / trackH : 0;
                l->top = GUI_MAX(0, GUI_MIN(l->top, l->count - rows));
                J3DGui_Invalidate(p);
            }
            else if ( msg == WM_LBUTTONUP )
            {
                l->bThumbDrag = 0;
                ReleaseCapture();
            }
            return -2;
        }
        if ( PtInRect(&bar, pt) )
        {
            if ( msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK )
            {
                if ( PtInRect(&up, pt) )
                {
                    l->top = GUI_MAX(0, l->top - 1);
                    l->pushedPart = 1;
                }
                else if ( PtInRect(&down, pt) )
                {
                    l->top = GUI_MIN(l->count - rows, l->top + 1);
                    l->pushedPart = 2;
                }
                else if ( PtInRect(&thumb, pt) )
                {
                    l->bThumbDrag = 1;
                    l->thumbGrab = pt.y - thumb.top;
                    SetCapture(J3DGui_ToHwnd(p));
                }
                else if ( pt.y < thumb.top )
                {
                    l->top = GUI_MAX(0, l->top - rows);
                }
                else
                {
                    l->top = GUI_MIN(l->count - rows, l->top + rows);
                }
                J3DGui_Invalidate(p);
            }
            else if ( msg == WM_LBUTTONUP )
            {
                l->pushedPart = 0;
                J3DGui_Invalidate(p);
            }
            return -2;
        }
        if ( msg == WM_LBUTTONUP && l->pushedPart )
        {
            l->pushedPart = 0;
            J3DGui_Invalidate(p);
        }
    }
    if ( pt.x < 0 || pt.x >= client.right || pt.y < 0 || pt.y >= client.bottom )
    {
        return -1;
    }
    {
        int index = l->top + pt.y / GUI_MAX(1, l->itemHeight);
        return index < l->count ? index : -1;
    }
}

// The keys that move a selection. Returns the new selection, or -2 when the key is not one of them.
static int gui_ListKey(GuiList* l, WPARAM vk, int height)
{
    int rows = gui_ListVisibleRows(l, height);
    int sel = l->sel;
    switch ( vk )
    {
        case VK_UP:    sel = sel < 0 ? 0 : GUI_MAX(0, sel - 1); break;
        case VK_DOWN:  sel = sel < 0 ? 0 : GUI_MIN(l->count - 1, sel + 1); break;
        case VK_PRIOR: sel = GUI_MAX(0, (sel < 0 ? 0 : sel) - (rows - 1)); break;
        case VK_NEXT:  sel = GUI_MIN(l->count - 1, (sel < 0 ? 0 : sel) + (rows - 1)); break;
        case VK_HOME:  sel = 0; break;
        case VK_END:   sel = l->count - 1; break;
        default:       return -2;
    }
    return l->count ? sel : -1;
}

// The LB_* and CB_* item messages, which mirror each other. Returns 1 when msg was one of them.
static int gui_ListItemMessage(GuiList* l, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* pResult, int bCombo)
{
    const UINT ADD = bCombo ? CB_ADDSTRING : LB_ADDSTRING;
    const UINT INSERT = bCombo ? CB_INSERTSTRING : LB_INSERTSTRING;
    const UINT DEL = bCombo ? CB_DELETESTRING : LB_DELETESTRING;
    const UINT RESET = bCombo ? CB_RESETCONTENT : LB_RESETCONTENT;
    const UINT COUNT = bCombo ? CB_GETCOUNT : LB_GETCOUNT;
    const UINT GETTEXT = bCombo ? CB_GETLBTEXT : LB_GETTEXT;
    const UINT GETTEXTLEN = bCombo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN;
    const UINT GETDATA = bCombo ? CB_GETITEMDATA : LB_GETITEMDATA;
    const UINT SETDATA = bCombo ? CB_SETITEMDATA : LB_SETITEMDATA;
    const UINT FIND = bCombo ? CB_FINDSTRING : LB_FINDSTRING;
    const UINT FINDEXACT = bCombo ? CB_FINDSTRINGEXACT : LB_FINDSTRINGEXACT;
    const UINT GETCURSEL = bCombo ? CB_GETCURSEL : LB_GETCURSEL;
    int index = (int)wParam;

    if ( msg == ADD )
    {
        *pResult = gui_ListInsert(l, -1, (const char*)lParam);
    }
    else if ( msg == INSERT )
    {
        *pResult = gui_ListInsert(l, index < 0 ? l->count : index, (const char*)lParam);
    }
    else if ( msg == DEL )
    {
        *pResult = gui_ListDelete(l, index);
    }
    else if ( msg == RESET )
    {
        gui_ListClear(l);
        *pResult = 0;
    }
    else if ( msg == COUNT )
    {
        *pResult = l->count;
    }
    else if ( msg == GETTEXT )
    {
        if ( index < 0 || index >= l->count || !lParam )
        {
            *pResult = -1;
        }
        else
        {
            strcpy((char*)lParam, l->aItems[index].pText);
            *pResult = (LRESULT)strlen(l->aItems[index].pText);
        }
    }
    else if ( msg == GETTEXTLEN )
    {
        *pResult = index >= 0 && index < l->count ? (LRESULT)strlen(l->aItems[index].pText) : -1;
    }
    else if ( msg == GETDATA )
    {
        *pResult = index >= 0 && index < l->count ? l->aItems[index].data : -1;
    }
    else if ( msg == SETDATA )
    {
        if ( index >= 0 && index < l->count )
        {
            l->aItems[index].data = lParam;
            *pResult = 0;
        }
        else
        {
            *pResult = -1;
        }
    }
    else if ( msg == FIND || msg == FINDEXACT )
    {
        *pResult = l->count ? gui_ListFind(l, index < 0 ? -1 : index, (const char*)lParam, msg == FINDEXACT) : -1;
    }
    else if ( msg == GETCURSEL )
    {
        *pResult = l->sel;
    }
    else
    {
        return 0;
    }
    return 1;
}

/* ---- ListBox ----------------------------------------------------------------------------------------------------------- */

static GuiList* gui_ListBoxData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        GuiList* l = malloc(sizeof(GuiList));
        if ( l )
        {
            gui_ListInit(l, (p->style & LBS_SORT) != 0);
        }
        p->pData = l;
    }
    return p->pData;
}

static void gui_ListBoxSelect(J3DGuiWindow* p, GuiList* l, int sel, int bNotify)
{
    RECT client;
    if ( sel == l->sel )
    {
        return;
    }
    l->sel = sel;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    gui_ListEnsureVisible(l, client.bottom);
    J3DGui_Invalidate(p);
    if ( bNotify && (p->style & LBS_NOTIFY) )
    {
        J3DGui_NotifyParent(p, LBN_SELCHANGE);
    }
}

LRESULT CALLBACK J3DGui_ListBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiList* l;
    LRESULT result;
    RECT client;
    if ( !p )
    {
        return 0;
    }
    l = gui_ListBoxData(p);
    if ( gui_ListItemMessage(l, msg, wParam, lParam, &result, 0) )
    {
        if ( msg != LB_GETCOUNT && msg != LB_GETTEXT && msg != LB_GETTEXTLEN && msg != LB_GETITEMDATA && msg != LB_FINDSTRING
             && msg != LB_FINDSTRINGEXACT && msg != LB_GETCURSEL )
        {
            J3DGui_Invalidate(p);
        }
        return result;
    }
    GetClientRect(hwnd, &client);
    switch ( msg )
    {
        case WM_NCDESTROY:
            gui_ListClear(l);
            free(l);
            p->pData = NULL;
            return 0;
        case WM_SETFONT:
            p->hFont = (HFONT)wParam;
            l->itemHeight = J3DGui_FontHeight(J3DGui_GetFont(p));
            J3DGui_Invalidate(p);
            return 0;
        case WM_PAINT:
            gui_ListPaint(p, l, J3DGui_GetFocusWindow() == p, 0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case LB_SETCURSEL:
            gui_ListBoxSelect(p, l, (int)wParam < l->count ? (int)wParam : -1, 0);
            return l->sel;
        case LB_SELECTSTRING:
        {
            int index = l->count ? gui_ListFind(l, (int)wParam, (const char*)lParam, 0) : -1;
            if ( index >= 0 )
            {
                gui_ListBoxSelect(p, l, index, 0);
            }
            return index;
        }
        case LB_GETTOPINDEX:
            return l->top;
        case LB_SETTOPINDEX:
            l->top = GUI_MAX(0, GUI_MIN((int)wParam, l->count - 1));
            J3DGui_Invalidate(p);
            return 0;
        case LB_GETITEMHEIGHT:
            return l->itemHeight;
        case LB_SETITEMHEIGHT:
            l->itemHeight = GUI_MAX(1, (int)LOWORD(lParam));
            J3DGui_Invalidate(p);
            return 0;
        case LB_GETITEMRECT:
            if ( lParam )
            {
                SetRect((RECT*)lParam, 0, ((int)wParam - l->top) * l->itemHeight, client.right, ((int)wParam - l->top + 1) * l->itemHeight);
            }
            return (int)wParam < l->count ? 1 : -1;
        case LB_SETHORIZONTALEXTENT:
            l->horizontalExtent = (int)wParam;
            return 0;
        case LB_GETHORIZONTALEXTENT:
            return l->horizontalExtent;
        case LB_GETSEL:
            return (int)wParam == l->sel;
        case LB_SETSEL:
            gui_ListBoxSelect(p, l, wParam ? (int)lParam : -1, 0);
            return 0;
        case LB_GETSELCOUNT:
            return l->sel >= 0 ? 1 : 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            int index;
            SetFocus(hwnd);
            index = gui_ListMouse(p, l, msg, lParam);
            if ( index >= 0 )
            {
                gui_ListBoxSelect(p, l, index, 1);
                if ( msg == WM_LBUTTONDBLCLK && (p->style & LBS_NOTIFY) )
                {
                    J3DGui_NotifyParent(p, LBN_DBLCLK);
                }
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if ( l->bThumbDrag )
            {
                gui_ListMouse(p, l, msg, lParam);
            }
            else if ( (wParam & MK_LBUTTON) && GetCapture() == hwnd )
            {
                int index = gui_ListMouse(p, l, msg, lParam);
                if ( index >= 0 )
                {
                    gui_ListBoxSelect(p, l, index, 1);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            gui_ListMouse(p, l, msg, lParam);
            if ( GetCapture() == hwnd )
            {
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEWHEEL:
            l->top = GUI_MAX(0, GUI_MIN(l->top - (short)HIWORD(wParam) / WHEEL_DELTA * 3, l->count - gui_ListVisibleRows(l, client.bottom)));
            J3DGui_Invalidate(p);
            return 0;
        case WM_KEYDOWN:
        {
            int sel = gui_ListKey(l, wParam, client.bottom);
            if ( sel != -2 )
            {
                gui_ListBoxSelect(p, l, sel, 1);
            }
            return 0;
        }
        case WM_CHAR:
            if ( wParam > ' ' && l->count )
            {
                char s[2] = { (char)wParam, 0 };
                int index = gui_ListFind(l, l->sel, s, 0);
                if ( index >= 0 )
                {
                    gui_ListBoxSelect(p, l, index, 1);
                }
            }
            return 0;
        case WM_SETFOCUS:
            J3DGui_Invalidate(p);
            if ( p->style & LBS_NOTIFY )
            {
                J3DGui_NotifyParent(p, LBN_SETFOCUS);
            }
            return 0;
        case WM_KILLFOCUS:
            J3DGui_Invalidate(p);
            if ( p->style & LBS_NOTIFY )
            {
                J3DGui_NotifyParent(p, LBN_KILLFOCUS);
            }
            return 0;
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ---- ComboBox ---------------------------------------------------------------------------------------------------------- */

static GuiCombo* gui_ComboData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        GuiCombo* c = calloc(1, sizeof(GuiCombo));
        if ( c )
        {
            gui_ListInit(&c->list, (p->style & CBS_SORT) != 0);
        }
        p->pData = c;
    }
    return p->pData;
}

static int gui_ComboFieldHeight(J3DGuiWindow* p)
{
    return J3DGui_FontHeight(J3DGui_GetFont(p)) + 8; // 21 for MS Sans Serif 8, as Windows 98
}

// For gui_window.c's MoveWindow and SetWindowPos: a combo box's height is its field's; the asked height is the drop-down's.
int J3DGui_ComboAdjustHeight(J3DGuiWindow* p, int cy)
{
    GuiCombo* c = gui_ComboData(p);
    if ( c && cy > 0 )
    {
        c->droppedHeight = cy;
    }
    return gui_ComboFieldHeight(p);
}

static void gui_ComboClose(J3DGuiWindow* p, int bAccept)
{
    GuiCombo* c = gui_ComboData(p);
    J3DGuiWindow* pDrop = c->pDrop;
    if ( !pDrop )
    {
        return;
    }
    c->pDrop = NULL;
    if ( GetCapture() == J3DGui_ToHwnd(pDrop) )
    {
        ReleaseCapture();
    }
    DestroyWindow(J3DGui_ToHwnd(pDrop));
    J3DGui_Invalidate(p);
    if ( !bAccept )
    {
        c->list.sel = c->selOnOpen;
    }
    J3DGui_NotifyParent(p, CBN_CLOSEUP);
    if ( bAccept )
    {
        if ( c->list.sel != c->selOnOpen )
        {
            J3DGui_NotifyParent(p, CBN_SELCHANGE);
        }
        J3DGui_NotifyParent(p, CBN_SELENDOK);
    }
    else
    {
        J3DGui_NotifyParent(p, CBN_SELENDCANCEL);
    }
}

static void gui_ComboOpen(J3DGuiWindow* p)
{
    GuiCombo* c = gui_ComboData(p);
    RECT r;
    int screenW;
    int screenH;
    int rows;
    int listH;
    if ( c->pDrop || !c->list.count )
    {
        if ( !c->list.count )
        {
            MessageBeep(MB_OK);
        }
        return;
    }
    J3DGui_NotifyParent(p, CBN_DROPDOWN);
    GetWindowRect(J3DGui_ToHwnd(p), &r);
    J3DGuiHost_GetScreenSize(&screenW, &screenH);
    rows = GUI_MAX(1, (c->droppedHeight - gui_ComboFieldHeight(p)) / GUI_MAX(1, c->list.itemHeight));
    rows = GUI_MIN(rows, c->list.count);
    listH = rows * c->list.itemHeight + 2;
    if ( r.bottom + listH <= screenH || r.top - listH < 0 )
    {
        SetRect(&r, r.left, r.bottom, GUI_MAX(r.right, r.left + c->droppedWidth), r.bottom + listH);
    }
    else
    {
        SetRect(&r, r.left, r.top - listH, GUI_MAX(r.right, r.left + c->droppedWidth), r.top);
    }
    c->selOnOpen = c->list.sel;
    c->pDrop = J3DGui_CreateWindow(GUI_CLASS_COMBOLBOX, "ComboLBox", "", WS_POPUP | WS_BORDER | WS_VISIBLE, 0, &r, NULL, J3DGui_ToHwnd(p), 0,
                                   p->hInstance, p);
    if ( c->pDrop )
    {
        c->pDrop->hFont = p->hFont;
        if ( c->list.sel >= 0 )
        {
            gui_ListEnsureVisible(&c->list, listH - 2);
        }
        SetWindowPos(J3DGui_ToHwnd(c->pDrop), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetCapture(J3DGui_ToHwnd(c->pDrop));
    }
    J3DGui_Invalidate(p);
}

static void gui_ComboSelect(J3DGuiWindow* p, int sel, int bNotify)
{
    GuiCombo* c = gui_ComboData(p);
    if ( sel == c->list.sel )
    {
        return;
    }
    c->list.sel = sel;
    J3DGui_Invalidate(p);
    if ( bNotify )
    {
        J3DGui_NotifyParent(p, CBN_SELCHANGE);
    }
}

static void gui_ComboPaint(J3DGuiWindow* p)
{
    GuiCombo* c = gui_ComboData(p);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT r;
    RECT field;
    RECT button;
    HFONT hFont = J3DGui_GetFont(p);
    int bEnabled = J3DGui_IsReallyEnabled(p);
    int bFocus = J3DGui_GetFocusWindow() == p && !c->pDrop;

    SetRect(&r, 0, 0, p->rect.right - p->rect.left, p->rect.bottom - p->rect.top);
    J3DGui_DrawEdge3D(hdc, &r, GUI_EDGE_SUNKEN);
    J3DGui_FillRect32(hdc, &r, J3DGui_SysColor(bEnabled ? COLOR_WINDOW : COLOR_3DFACE));
    SetRect(&button, r.right - 16, r.top, r.right, r.bottom);
    J3DGui_DrawScrollButton(hdc, &button, 1, c->bButtonDown, bEnabled);
    SetRect(&field, r.left + 1, r.top + 1, button.left - 1, r.bottom - 1);
    if ( bFocus )
    {
        J3DGui_FillRect32(hdc, &field, J3DGui_SysColor(COLOR_HIGHLIGHT));
        J3DGui_DrawFocusRect32(hdc, &field);
    }
    if ( c->list.sel >= 0 && c->list.sel < c->list.count )
    {
        J3DGui_DrawText32(hdc, hFont, c->list.aItems[c->list.sel].pText, -1, field.left + 2, field.top + (field.bottom - field.top - J3DGui_FontHeight(hFont)) / 2,
                          J3DGui_SysColor(bFocus ? COLOR_HIGHLIGHTTEXT : bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT), -1);
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

LRESULT CALLBACK J3DGui_ComboBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiCombo* c;
    LRESULT result;
    if ( !p )
    {
        return 0;
    }
    c = gui_ComboData(p);
    if ( gui_ListItemMessage(&c->list, msg, wParam, lParam, &result, 1) )
    {
        if ( msg == CB_RESETCONTENT || msg == CB_DELETESTRING )
        {
            J3DGui_Invalidate(p);
        }
        return result;
    }
    switch ( msg )
    {
        case WM_CREATE:
        {
            int cy = p->rect.bottom - p->rect.top;
            c->droppedHeight = cy;
            p->rect.bottom = p->rect.top + gui_ComboFieldHeight(p);
            return 0;
        }
        case WM_NCDESTROY:
            gui_ListClear(&c->list);
            free(c);
            p->pData = NULL;
            return 0;
        case WM_DESTROY:
            if ( c->pDrop )
            {
                DestroyWindow(J3DGui_ToHwnd(c->pDrop));
                c->pDrop = NULL;
            }
            return 0;
        case WM_SETFONT:
            p->hFont = (HFONT)wParam;
            c->list.itemHeight = J3DGui_FontHeight(J3DGui_GetFont(p));
            p->rect.bottom = p->rect.top + gui_ComboFieldHeight(p);
            J3DGui_Invalidate(p);
            if ( p->pParent )
            {
                J3DGui_Invalidate(p->pParent);
            }
            return 0;
        case WM_PAINT:
            gui_ComboPaint(p);
            return 0;
        case WM_NCPAINT:
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS | (c->pDrop ? DLGC_WANTMESSAGE : 0);
        case WM_GETTEXT:
            if ( wParam && lParam )
            {
                const char* s = c->list.sel >= 0 && c->list.sel < c->list.count ? c->list.aItems[c->list.sel].pText : "";
                strncpy((char*)lParam, s, wParam - 1);
                ((char*)lParam)[wParam - 1] = 0;
                return (LRESULT)strlen((char*)lParam);
            }
            return 0;
        case WM_GETTEXTLENGTH:
            return c->list.sel >= 0 && c->list.sel < c->list.count ? (LRESULT)strlen(c->list.aItems[c->list.sel].pText) : 0;
        case CB_SETCURSEL:
            gui_ComboSelect(p, (int)wParam >= 0 && (int)wParam < c->list.count ? (int)wParam : -1, 0);
            return c->list.sel;
        case CB_SELECTSTRING:
        {
            int index = c->list.count ? gui_ListFind(&c->list, (int)wParam, (const char*)lParam, 0) : -1;
            if ( index >= 0 )
            {
                gui_ComboSelect(p, index, 0);
            }
            return index;
        }
        case CB_SHOWDROPDOWN:
            if ( wParam )
            {
                gui_ComboOpen(p);
            }
            else
            {
                gui_ComboClose(p, 1);
            }
            return TRUE;
        case CB_GETDROPPEDSTATE:
            return c->pDrop != NULL;
        case CB_GETDROPPEDCONTROLRECT:
            if ( lParam )
            {
                GetWindowRect(hwnd, (RECT*)lParam);
                ((RECT*)lParam)->bottom = ((RECT*)lParam)->top + c->droppedHeight;
            }
            return TRUE;
        case CB_SETDROPPEDWIDTH:
            c->droppedWidth = (int)wParam;
            return (LRESULT)wParam;
        case CB_GETITEMHEIGHT:
            return c->list.itemHeight;
        case CB_SETITEMHEIGHT:
            return 0;
        case CB_LIMITTEXT:
            return TRUE;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetFocus(hwnd);
            if ( c->pDrop )
            {
                gui_ComboClose(p, 0);
            }
            else
            {
                c->bButtonDown = 1;
                gui_ComboOpen(p);
            }
            return 0;
        case WM_LBUTTONUP:
            if ( c->bButtonDown )
            {
                c->bButtonDown = 0;
                J3DGui_Invalidate(p);
            }
            return 0;
        case WM_KEYDOWN:
            if ( wParam == VK_F4 || (wParam == VK_DOWN && GetKeyState(VK_MENU) < 0) )
            {
                gui_ComboOpen(p);
                return 0;
            }
            {
                int sel = gui_ListKey(&c->list, wParam, c->list.itemHeight * 8);
                if ( sel != -2 && sel >= 0 )
                {
                    gui_ComboSelect(p, sel, 1);
                }
            }
            return 0;
        case WM_SYSKEYDOWN:
            if ( wParam == VK_DOWN || wParam == VK_UP )
            {
                if ( c->pDrop )
                {
                    gui_ComboClose(p, 1);
                }
                else
                {
                    gui_ComboOpen(p);
                }
                return 0;
            }
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        case WM_CHAR:
            if ( wParam > ' ' && c->list.count )
            {
                char s[2] = { (char)wParam, 0 };
                int index = gui_ListFind(&c->list, c->list.sel, s, 0);
                if ( index >= 0 )
                {
                    gui_ComboSelect(p, index, 1);
                }
            }
            return 0;
        case WM_MOUSEWHEEL:
            if ( !c->pDrop && c->list.count )
            {
                int sel = (c->list.sel < 0 ? 0 : c->list.sel) - (short)HIWORD(wParam) / WHEEL_DELTA;
                gui_ComboSelect(p, GUI_MAX(0, GUI_MIN(c->list.count - 1, sel)), 1);
            }
            return 0;
        case WM_SETFOCUS:
            J3DGui_Invalidate(p);
            J3DGui_NotifyParent(p, CBN_SETFOCUS);
            return 0;
        case WM_KILLFOCUS:
            if ( c->pDrop )
            {
                gui_ComboClose(p, 0);
            }
            J3DGui_Invalidate(p);
            J3DGui_NotifyParent(p, CBN_KILLFOCUS);
            return 0;
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ---- The drop-down ------------------------------------------------------------------------------------------------------ */

static J3DGuiWindow* gui_DropOwner(J3DGuiWindow* pDrop)
{
    return J3DGui_FromHwnd(pDrop->hOwner);
}

LRESULT CALLBACK J3DGui_ComboLBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    J3DGuiWindow* pCombo;
    GuiCombo* c;
    RECT client;
    if ( !p )
    {
        return 0;
    }
    pCombo = gui_DropOwner(p);
    if ( !pCombo || pCombo->cls != GUI_CLASS_COMBOBOX )
    {
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    c = gui_ComboData(pCombo);
    GetClientRect(hwnd, &client);
    switch ( msg )
    {
        case WM_PAINT:
            gui_ListPaint(p, &c->list, 0, 1);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
        {
            int index = gui_ListMouse(p, &c->list, msg, lParam);
            if ( index >= 0 && index != c->list.sel )
            {
                c->list.sel = index; // the hover selects, as Windows 98's drop-down
                J3DGui_Invalidate(p);
                J3DGui_Invalidate(pCombo);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            if ( !PtInRect(&client, pt) )
            {
                gui_ComboClose(pCombo, 0);
                return 0;
            }
            gui_ListMouse(p, &c->list, msg, lParam);
            return 0;
        }
        case WM_LBUTTONUP:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int index;
            if ( c->list.bThumbDrag || c->list.pushedPart )
            {
                gui_ListMouse(p, &c->list, msg, lParam);
                SetCapture(hwnd);
                return 0;
            }
            if ( !PtInRect(&client, pt) )
            {
                return 0; // the release of the press that opened it, over the combo box
            }
            index = gui_ListMouse(p, &c->list, msg, lParam);
            if ( index >= 0 )
            {
                c->list.sel = index;
                gui_ComboClose(pCombo, 1);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            c->list.top = GUI_MAX(0, GUI_MIN(c->list.top - (short)HIWORD(wParam) / WHEEL_DELTA * 3, c->list.count - gui_ListVisibleRows(&c->list, client.bottom)));
            J3DGui_Invalidate(p);
            return 0;
        case WM_KEYDOWN:
            if ( wParam == VK_RETURN || wParam == VK_F4 )
            {
                gui_ComboClose(pCombo, 1);
            }
            else if ( wParam == VK_ESCAPE )
            {
                gui_ComboClose(pCombo, 0);
            }
            else
            {
                int sel = gui_ListKey(&c->list, wParam, client.bottom);
                if ( sel >= 0 )
                {
                    c->list.sel = sel;
                    gui_ListEnsureVisible(&c->list, client.bottom);
                    J3DGui_Invalidate(p);
                    J3DGui_Invalidate(pCombo);
                }
            }
            return 0;
        case WM_SYSKEYDOWN:
            if ( wParam == VK_UP || wParam == VK_DOWN )
            {
                gui_ComboClose(pCombo, 1);
            }
            return 0;
        case WM_CANCELMODE:
            gui_ComboClose(pCombo, 0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}
