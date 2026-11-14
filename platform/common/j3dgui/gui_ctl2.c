/*
 * j3dgui controls 3 (PORT-PLAN.md P6.8): the scroll bar control, the trackbar ("msctls_trackbar32") and the list view
 * ("SysListView32", report mode: the control options' key lists and the trading post's three lists), with the
 * notifications the game's dialogs read (WM_HSCROLL; WM_NOTIFY with NM_CLICK, NM_DBLCLK, LVN_ITEMCHANGED,
 * LVN_BEGINDRAG and LVN_BEGINRDRAG).
 */
#include "gui_internal.h"
#include "j3dgui.h"

#include <stdlib.h>
#include <string.h>

void J3DGui_DrawScrollTrack(HDC hdc, const RECT* pTrack);

/* ---- Scroll bar (SBS_HORZ, SBS_VERT) ------------------------------------------------------------------------------------ */

typedef struct GuiScroll
{
    int min;
    int max;
    int page;
    int pos;
    int trackPos;
    int bDragging;
    int grab;
    int pushed;     // 0 none, 1 the line-up/left button, 2 the line-down/right button
} GuiScroll;

static GuiScroll* gui_ScrollData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        GuiScroll* s = calloc(1, sizeof(GuiScroll));
        if ( s )
        {
            s->max = 100;
        }
        p->pData = s;
    }
    return p->pData;
}

static int gui_ScrollVertical(const J3DGuiWindow* p)
{
    return (p->style & SBS_VERT) != 0;
}

static void gui_ScrollClamp(GuiScroll* s)
{
    int top = s->max - GUI_MAX(s->page - 1, 0);
    s->pos = GUI_MAX(s->min, GUI_MIN(s->pos, GUI_MAX(s->min, top)));
}

// The two buttons, the track between them and the thumb, along the bar's length.
static void gui_ScrollParts(J3DGuiWindow* p, GuiScroll* s, RECT* pA, RECT* pB, RECT* pTrack, RECT* pThumb, int pos)
{
    RECT client;
    int len;
    int thick;
    int trackLen;
    int thumbLen;
    int range;
    int at;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    if ( gui_ScrollVertical(p) )
    {
        len = client.bottom;
        thick = client.right;
    }
    else
    {
        len = client.right;
        thick = client.bottom;
    }
    {
        int button = GUI_MIN(thick, len / 2);
        trackLen = len - 2 * button;
        range = s->max - s->min + 1;
        thumbLen = s->page > 0 && range > 0 ? GUI_MAX(8, trackLen * s->page / range) : GUI_MIN(thick, trackLen);
        thumbLen = GUI_MIN(thumbLen, trackLen);
        {
            int span = range - GUI_MAX(s->page, 1);
            at = span > 0 ? (trackLen - thumbLen) * (pos - s->min) / span : 0;
        }
        if ( gui_ScrollVertical(p) )
        {
            SetRect(pA, 0, 0, thick, button);
            SetRect(pB, 0, len - button, thick, len);
            SetRect(pTrack, 0, button, thick, len - button);
            SetRect(pThumb, 0, button + at, thick, button + at + thumbLen);
        }
        else
        {
            SetRect(pA, 0, 0, button, thick);
            SetRect(pB, len - button, 0, len, thick);
            SetRect(pTrack, button, 0, len - button, thick);
            SetRect(pThumb, button + at, 0, button + at + thumbLen, thick);
        }
    }
}

static void gui_ScrollNotify(J3DGuiWindow* p, int code, int pos)
{
    if ( p->pParent )
    {
        J3DGui_Send(p->pParent, gui_ScrollVertical(p) ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, pos), (LPARAM)J3DGui_ToHwnd(p));
    }
}

LRESULT CALLBACK J3DGui_ScrollBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiScroll* s;
    RECT a;
    RECT b;
    RECT track;
    RECT thumb;
    if ( !p )
    {
        return 0;
    }
    s = gui_ScrollData(p);
    switch ( msg )
    {
        case WM_NCDESTROY:
            free(p->pData);
            p->pData = NULL;
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            int bEnabled = J3DGui_IsReallyEnabled(p) && s->max > s->min;
            int vertical = gui_ScrollVertical(p);
            gui_ScrollParts(p, s, &a, &b, &track, &thumb, s->bDragging ? s->trackPos : s->pos);
            J3DGui_DrawScrollButton(hdc, &a, vertical ? 0 : 2, s->pushed == 1, bEnabled);
            J3DGui_DrawScrollButton(hdc, &b, vertical ? 1 : 3, s->pushed == 2, bEnabled);
            J3DGui_DrawScrollTrack(hdc, &track);
            if ( bEnabled )
            {
                J3DGui_DrawEdge3D(hdc, &thumb, GUI_EDGE_RAISED);
                J3DGui_FillRect32(hdc, &thumb, J3DGui_SysColor(COLOR_3DFACE));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case SBM_SETPOS:
        {
            int old = s->pos;
            s->pos = (int)wParam;
            gui_ScrollClamp(s);
            if ( lParam )
            {
                J3DGui_Invalidate(p);
            }
            return old;
        }
        case SBM_GETPOS:
            return s->pos;
        case SBM_SETRANGE:
        case SBM_SETRANGEREDRAW:
            s->min = (int)wParam;
            s->max = (int)lParam;
            gui_ScrollClamp(s);
            J3DGui_Invalidate(p);
            return s->pos;
        case SBM_GETRANGE:
            if ( wParam )
            {
                *(int*)wParam = s->min;
            }
            if ( lParam )
            {
                *(int*)lParam = s->max;
            }
            return 0;
        case SBM_SETSCROLLINFO:
        {
            const SCROLLINFO* pInfo = (const SCROLLINFO*)lParam;
            if ( pInfo )
            {
                if ( pInfo->fMask & SIF_RANGE )
                {
                    s->min = pInfo->nMin;
                    s->max = pInfo->nMax;
                }
                if ( pInfo->fMask & SIF_PAGE )
                {
                    s->page = (int)pInfo->nPage;
                }
                if ( pInfo->fMask & SIF_POS )
                {
                    s->pos = pInfo->nPos;
                }
                gui_ScrollClamp(s);
                J3DGui_Invalidate(p);
            }
            return s->pos;
        }
        case SBM_GETSCROLLINFO:
        {
            SCROLLINFO* pInfo = (SCROLLINFO*)lParam;
            if ( !pInfo )
            {
                return FALSE;
            }
            if ( pInfo->fMask & SIF_RANGE )
            {
                pInfo->nMin = s->min;
                pInfo->nMax = s->max;
            }
            if ( pInfo->fMask & SIF_PAGE )
            {
                pInfo->nPage = (UINT)s->page;
            }
            if ( pInfo->fMask & SIF_POS )
            {
                pInfo->nPos = s->pos;
            }
            if ( pInfo->fMask & SIF_TRACKPOS )
            {
                pInfo->nTrackPos = s->bDragging ? s->trackPos : s->pos;
            }
            return TRUE;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int vertical = gui_ScrollVertical(p);
            if ( s->max <= s->min || !J3DGui_IsReallyEnabled(p) )
            {
                return 0;
            }
            gui_ScrollParts(p, s, &a, &b, &track, &thumb, s->pos);
            SetCapture(hwnd);
            if ( PtInRect(&a, pt) )
            {
                s->pushed = 1;
                gui_ScrollNotify(p, vertical ? SB_LINEUP : SB_LINELEFT, 0);
            }
            else if ( PtInRect(&b, pt) )
            {
                s->pushed = 2;
                gui_ScrollNotify(p, vertical ? SB_LINEDOWN : SB_LINERIGHT, 0);
            }
            else if ( PtInRect(&thumb, pt) )
            {
                s->bDragging = 1;
                s->trackPos = s->pos;
                s->grab = vertical ? pt.y - thumb.top : pt.x - thumb.left;
            }
            else if ( (vertical ? pt.y < thumb.top : pt.x < thumb.left) )
            {
                gui_ScrollNotify(p, vertical ? SB_PAGEUP : SB_PAGELEFT, 0);
            }
            else
            {
                gui_ScrollNotify(p, vertical ? SB_PAGEDOWN : SB_PAGERIGHT, 0);
            }
            J3DGui_Invalidate(p);
            return 0;
        }
        case WM_MOUSEMOVE:
            if ( s->bDragging )
            {
                POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
                int vertical = gui_ScrollVertical(p);
                int trackLen;
                int thumbLen;
                int at;
                int span;
                gui_ScrollParts(p, s, &a, &b, &track, &thumb, s->trackPos);
                trackLen = vertical ? track.bottom - track.top : track.right - track.left;
                thumbLen = vertical ? thumb.bottom - thumb.top : thumb.right - thumb.left;
                at = (vertical ? pt.y - track.top : pt.x - track.left) - s->grab;
                span = s->max - s->min + 1 - GUI_MAX(s->page, 1);
                s->trackPos = trackLen > thumbLen ? s->min + (at * span + (trackLen - thumbLen) / 2) / (trackLen - thumbLen) : s->min;
                s->trackPos = GUI_MAX(s->min, GUI_MIN(s->trackPos, s->min + GUI_MAX(span, 0)));
                gui_ScrollNotify(p, SB_THUMBTRACK, s->trackPos);
                J3DGui_Invalidate(p);
            }
            return 0;
        case WM_LBUTTONUP:
            if ( GetCapture() == hwnd )
            {
                ReleaseCapture();
            }
            if ( s->bDragging )
            {
                s->bDragging = 0;
                gui_ScrollNotify(p, SB_THUMBPOSITION, s->trackPos);
            }
            s->pushed = 0;
            gui_ScrollNotify(p, SB_ENDSCROLL, 0);
            J3DGui_Invalidate(p);
            return 0;
        case WM_KEYDOWN:
        {
            int vertical = gui_ScrollVertical(p);
            int code = -1;
            switch ( wParam )
            {
                case VK_LEFT:  code = vertical ? -1 : SB_LINELEFT; break;
                case VK_RIGHT: code = vertical ? -1 : SB_LINERIGHT; break;
                case VK_UP:    code = vertical ? SB_LINEUP : -1; break;
                case VK_DOWN:  code = vertical ? SB_LINEDOWN : -1; break;
                case VK_PRIOR: code = SB_PAGEUP; break;
                case VK_NEXT:  code = SB_PAGEDOWN; break;
                case VK_HOME:  code = SB_TOP; break;
                case VK_END:   code = SB_BOTTOM; break;
                default: break;
            }
            if ( code >= 0 )
            {
                gui_ScrollNotify(p, code, 0);
                gui_ScrollNotify(p, SB_ENDSCROLL, 0);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

// The scroll functions, for a scroll bar control (SB_CTL). The game's dialogs use no window scroll bars of their own.
int WINAPI SetScrollPos(HWND hwnd, int bar, int pos, BOOL bRedraw)
{
    return bar == SB_CTL ? (int)SendMessageA(hwnd, SBM_SETPOS, (WPARAM)pos, bRedraw) : 0;
}

int WINAPI GetScrollPos(HWND hwnd, int bar)
{
    return bar == SB_CTL ? (int)SendMessageA(hwnd, SBM_GETPOS, 0, 0) : 0;
}

BOOL WINAPI SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL bRedraw)
{
    if ( bar != SB_CTL )
    {
        return FALSE;
    }
    SendMessageA(hwnd, bRedraw ? SBM_SETRANGEREDRAW : SBM_SETRANGE, (WPARAM)min, max);
    return TRUE;
}

BOOL WINAPI GetScrollRange(HWND hwnd, int bar, LPINT pMin, LPINT pMax)
{
    if ( bar != SB_CTL )
    {
        if ( pMin )
        {
            *pMin = 0;
        }
        if ( pMax )
        {
            *pMax = 0;
        }
        return FALSE;
    }
    SendMessageA(hwnd, SBM_GETRANGE, (WPARAM)pMin, (LPARAM)pMax);
    return TRUE;
}

int WINAPI SetScrollInfo(HWND hwnd, int bar, const SCROLLINFO* pInfo, BOOL bRedraw)
{
    (void)bRedraw;
    return bar == SB_CTL ? (int)SendMessageA(hwnd, SBM_SETSCROLLINFO, bRedraw, (LPARAM)pInfo) : 0;
}

BOOL WINAPI GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO pInfo)
{
    return bar == SB_CTL ? (BOOL)SendMessageA(hwnd, SBM_GETSCROLLINFO, 0, (LPARAM)pInfo) : FALSE;
}

/* ---- Trackbar ----------------------------------------------------------------------------------------------------------- */

typedef struct GuiTrack
{
    int min;
    int max;
    int pos;
    int page;
    int line;
    int ticFreq;
    int bDragging;
    int grab;
} GuiTrack;

static GuiTrack* gui_TrackData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        GuiTrack* t = calloc(1, sizeof(GuiTrack));
        if ( t )
        {
            t->max = 100;
            t->page = 20;
            t->line = 1;
            t->ticFreq = 1;
        }
        p->pData = t;
    }
    return p->pData;
}

// Windows 98's horizontal trackbar with ticks below: an 11x21 thumb pointing down, on a 4-pixel sunken channel.
#define GUI_THUMB_W 11
#define GUI_THUMB_H 21

static void gui_TrackGeometry(J3DGuiWindow* p, GuiTrack* t, RECT* pChannel, RECT* pThumb, int pos)
{
    RECT client;
    int left;
    int right;
    int x;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    left = 8 + GUI_THUMB_W / 2;
    right = client.right - 8 - GUI_THUMB_W / 2;
    SetRect(pChannel, 8, 8, client.right - 8, 12);
    x = t->max > t->min ? left + (right - left) * (pos - t->min) / (t->max - t->min) : left;
    SetRect(pThumb, x - GUI_THUMB_W / 2, 0, x - GUI_THUMB_W / 2 + GUI_THUMB_W, GUI_MIN(GUI_THUMB_H, client.bottom));
    (void)pChannel;
}

static int gui_TrackPosFromX(J3DGuiWindow* p, GuiTrack* t, int x)
{
    RECT client;
    int left;
    int right;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    left = 8 + GUI_THUMB_W / 2;
    right = client.right - 8 - GUI_THUMB_W / 2;
    if ( right <= left )
    {
        return t->min;
    }
    {
        int pos = t->min + ((x - left) * (t->max - t->min) + (right - left) / 2) / (right - left);
        return GUI_MAX(t->min, GUI_MIN(t->max, pos));
    }
}

static void gui_TrackNotify(J3DGuiWindow* p, int code, int pos)
{
    if ( p->pParent )
    {
        J3DGui_Send(p->pParent, WM_HSCROLL, MAKEWPARAM(code, pos), (LPARAM)J3DGui_ToHwnd(p));
    }
}

static void gui_TrackPaint(J3DGuiWindow* p, GuiTrack* t)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT client;
    RECT channel;
    RECT thumb;
    int bFocus = J3DGui_GetFocusWindow() == p;
    int bEnabled = J3DGui_IsReallyEnabled(p);
    int v;

    GetClientRect(J3DGui_ToHwnd(p), &client);
    J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(COLOR_3DFACE));
    gui_TrackGeometry(p, t, &channel, &thumb, t->pos);
    {
        RECT c = channel;
        J3DGui_DrawEdge3D(hdc, &c, GUI_EDGE_SUNKEN);
        J3DGui_FillRect32(hdc, &c, J3DGui_SysColor(COLOR_WINDOW));
    }
    // ticks below the channel, with TBS_AUTOTICKS one every ticFreq; always the two ends
    if ( t->max > t->min )
    {
        int left = 8 + GUI_THUMB_W / 2;
        int right = client.right - 8 - GUI_THUMB_W / 2;
        int step = (p->style & TBS_AUTOTICKS) ? GUI_MAX(1, t->ticFreq) : t->max - t->min;
        for ( v = t->min; v <= t->max; v += step )
        {
            RECT tick;
            int x = left + (right - left) * (v - t->min) / (t->max - t->min);
            int bEnd = v == t->min || v == t->max;
            SetRect(&tick, x, GUI_THUMB_H + 1, x + 1, GUI_THUMB_H + (bEnd ? 5 : 4));
            J3DGui_FillRect32(hdc, &tick, 0);
            if ( v + step > t->max && v != t->max )
            {
                v = t->max - step; // the last tick at the maximum
            }
        }
    }
    // the thumb: a pentagon pointing down
    {
        int h = thumb.bottom - thumb.top;
        int body = h - GUI_THUMB_W / 2 - 1;
        int y;
        uint32_t face = J3DGui_SysColor(bEnabled ? COLOR_3DFACE : COLOR_3DFACE);
        for ( y = 0; y < h; ++y )
        {
            int inset = y < body ? 0 : y - body + 1;
            RECT row;
            if ( thumb.left + inset >= thumb.right - inset )
            {
                break;
            }
            SetRect(&row, thumb.left + inset, thumb.top + y, thumb.right - inset, thumb.top + y + 1);
            J3DGui_FillRect32(hdc, &row, face);
            // the outline: highlight left and top, shadow and dark shadow right and on the point
            {
                RECT px;
                SetRect(&px, thumb.left + inset, thumb.top + y, thumb.left + inset + 1, thumb.top + y + 1);
                J3DGui_FillRect32(hdc, &px, J3DGui_SysColor(COLOR_3DHILIGHT));
                SetRect(&px, thumb.right - inset - 1, thumb.top + y, thumb.right - inset, thumb.top + y + 1);
                J3DGui_FillRect32(hdc, &px, J3DGui_SysColor(COLOR_3DDKSHADOW));
                if ( thumb.right - inset - 2 > thumb.left + inset )
                {
                    SetRect(&px, thumb.right - inset - 2, thumb.top + y, thumb.right - inset - 1, thumb.top + y + 1);
                    J3DGui_FillRect32(hdc, &px, J3DGui_SysColor(COLOR_3DSHADOW));
                }
            }
        }
        {
            RECT top;
            SetRect(&top, thumb.left, thumb.top, thumb.right - 1, thumb.top + 1);
            J3DGui_FillRect32(hdc, &top, J3DGui_SysColor(COLOR_3DHILIGHT));
        }
        if ( !bEnabled )
        {
            // a disabled thumb shows the checkerboard inside
            RECT inner;
            SetRect(&inner, thumb.left + 2, thumb.top + 2, thumb.right - 2, thumb.top + body);
            J3DGui_DrawScrollTrack(hdc, &inner);
        }
    }
    if ( bFocus )
    {
        RECT f = client;
        J3DGui_DrawFocusRect32(hdc, &f);
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

static void gui_TrackSetPos(J3DGuiWindow* p, GuiTrack* t, int pos)
{
    pos = GUI_MAX(t->min, GUI_MIN(t->max, pos));
    if ( pos != t->pos )
    {
        t->pos = pos;
        J3DGui_Invalidate(p);
    }
}

LRESULT CALLBACK J3DGui_TrackBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiTrack* t;
    if ( !p )
    {
        return 0;
    }
    t = gui_TrackData(p);
    switch ( msg )
    {
        case WM_NCDESTROY:
            free(p->pData);
            p->pData = NULL;
            return 0;
        case WM_PAINT:
            gui_TrackPaint(p, t);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case TBM_GETPOS:
            return t->pos;
        case TBM_SETPOS:
            gui_TrackSetPos(p, t, (int)lParam);
            return 0;
        case TBM_GETRANGEMIN:
            return t->min;
        case TBM_GETRANGEMAX:
            return t->max;
        case TBM_SETRANGE:
            t->min = (short)LOWORD(lParam);
            t->max = (short)HIWORD(lParam);
            gui_TrackSetPos(p, t, t->pos);
            J3DGui_Invalidate(p);
            return 0;
        case TBM_SETRANGEMIN:
            t->min = (int)lParam;
            gui_TrackSetPos(p, t, t->pos);
            J3DGui_Invalidate(p);
            return 0;
        case TBM_SETRANGEMAX:
            t->max = (int)lParam;
            gui_TrackSetPos(p, t, t->pos);
            J3DGui_Invalidate(p);
            return 0;
        case TBM_SETTICFREQ:
            t->ticFreq = GUI_MAX(1, (int)wParam);
            J3DGui_Invalidate(p);
            return 0;
        case TBM_SETPAGESIZE:
        {
            int old = t->page;
            t->page = (int)lParam;
            return old;
        }
        case TBM_GETPAGESIZE:
            return t->page;
        case TBM_SETLINESIZE:
        {
            int old = t->line;
            t->line = (int)lParam;
            return old;
        }
        case TBM_GETLINESIZE:
            return t->line;
        case TBM_GETTHUMBRECT:
            if ( lParam )
            {
                RECT channel;
                gui_TrackGeometry(p, t, &channel, (RECT*)lParam, t->pos);
            }
            return 0;
        case TBM_GETCHANNELRECT:
            if ( lParam )
            {
                RECT thumb;
                gui_TrackGeometry(p, t, (RECT*)lParam, &thumb, t->pos);
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            RECT channel;
            RECT thumb;
            int x = (short)LOWORD(lParam);
            POINT pt = { x, (short)HIWORD(lParam) };
            SetFocus(hwnd);
            gui_TrackGeometry(p, t, &channel, &thumb, t->pos);
            if ( PtInRect(&thumb, pt) )
            {
                t->bDragging = 1;
                t->grab = x - (thumb.left + GUI_THUMB_W / 2);
                SetCapture(hwnd);
            }
            else
            {
                // a click beside the thumb pages towards it
                int bLeft = x < thumb.left;
                gui_TrackSetPos(p, t, t->pos + (bLeft ? -t->page : t->page));
                gui_TrackNotify(p, bLeft ? TB_PAGEUP : TB_PAGEDOWN, 0);
                gui_TrackNotify(p, TB_ENDTRACK, 0);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if ( t->bDragging )
            {
                int pos = gui_TrackPosFromX(p, t, (short)LOWORD(lParam) - t->grab);
                if ( pos != t->pos )
                {
                    gui_TrackSetPos(p, t, pos);
                    gui_TrackNotify(p, TB_THUMBTRACK, t->pos);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if ( t->bDragging )
            {
                t->bDragging = 0;
                ReleaseCapture();
                gui_TrackNotify(p, TB_THUMBPOSITION, t->pos);
                gui_TrackNotify(p, TB_ENDTRACK, 0);
            }
            return 0;
        case WM_KEYDOWN:
        {
            int code = -1;
            int pos = t->pos;
            switch ( wParam )
            {
                case VK_LEFT:
                case VK_UP:    pos -= t->line; code = TB_LINEUP; break;
                case VK_RIGHT:
                case VK_DOWN:  pos += t->line; code = TB_LINEDOWN; break;
                case VK_PRIOR: pos -= t->page; code = TB_PAGEUP; break;
                case VK_NEXT:  pos += t->page; code = TB_PAGEDOWN; break;
                case VK_HOME:  pos = t->min; code = TB_TOP; break;
                case VK_END:   pos = t->max; code = TB_BOTTOM; break;
                default: break;
            }
            if ( code >= 0 )
            {
                gui_TrackSetPos(p, t, pos);
                gui_TrackNotify(p, code, 0);
                gui_TrackNotify(p, TB_ENDTRACK, 0);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ---- List view (report mode) ------------------------------------------------------------------------------------------- */

#define GUI_LV_MAXCOLUMNS 8

typedef struct GuiLVItem
{
    char* apText[GUI_LV_MAXCOLUMNS];
    int image;
    UINT state;
    LPARAM lParam;
} GuiLVItem;

typedef struct GuiLVColumn
{
    char* pText;
    int width;
    int fmt;
} GuiLVColumn;

typedef struct GuiListView
{
    GuiLVColumn aColumns[GUI_LV_MAXCOLUMNS];
    int numColumns;
    GuiLVItem* aItems;
    int count;
    int capacity;
    int top;
    int focus;
    HIMAGELIST hSmall;
    HIMAGELIST hNormal;
    DWORD exStyle;
    int bPressed;       // the mouse went down on an item: a drag may start
    int pressItem;
    POINT pressPoint;
    int pressButton;    // 1 left, 2 right
} GuiListView;

static GuiListView* gui_LVData(J3DGuiWindow* p)
{
    if ( !p->pData )
    {
        GuiListView* lv = calloc(1, sizeof(GuiListView));
        if ( lv )
        {
            lv->focus = -1;
        }
        p->pData = lv;
    }
    return p->pData;
}

static int gui_LVRowHeight(J3DGuiWindow* p)
{
    return GUI_MAX(16, J3DGui_FontHeight(J3DGui_GetFont(p)) + 2); // room for a 16x16 small icon
}

static int gui_LVHeaderHeight(J3DGuiWindow* p)
{
    if ( (p->style & LVS_NOCOLUMNHEADER) )
    {
        return 0;
    }
    return J3DGui_FontHeight(J3DGui_GetFont(p)) + 6;
}

static int gui_LVRows(J3DGuiWindow* p)
{
    RECT client;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    return GUI_MAX(1, (client.bottom - gui_LVHeaderHeight(p)) / gui_LVRowHeight(p));
}

static void gui_LVFreeItem(GuiLVItem* it)
{
    int i;
    for ( i = 0; i < GUI_LV_MAXCOLUMNS; ++i )
    {
        free(it->apText[i]);
    }
}

static void gui_LVNotify(J3DGuiWindow* p, UINT code, int item, int subItem, UINT newState, UINT oldState, UINT changed, POINT pt)
{
    NMLISTVIEW nm;
    GuiListView* lv = gui_LVData(p);
    if ( !p->pParent )
    {
        return;
    }
    memset(&nm, 0, sizeof(nm));
    nm.hdr.hwndFrom = J3DGui_ToHwnd(p);
    nm.hdr.idFrom = (UINT_PTR)p->id;
    nm.hdr.code = code;
    nm.iItem = item;
    nm.iSubItem = subItem;
    nm.uNewState = newState;
    nm.uOldState = oldState;
    nm.uChanged = changed;
    nm.ptAction = pt;
    nm.lParam = item >= 0 && item < lv->count ? lv->aItems[item].lParam : 0;
    J3DGui_Send(p->pParent, WM_NOTIFY, (WPARAM)p->id, (LPARAM)&nm);
}

static void gui_LVSetState(J3DGuiWindow* p, int item, UINT state, UINT mask, int bNotify)
{
    GuiListView* lv = gui_LVData(p);
    int i;
    POINT none = { 0, 0 };
    for ( i = 0; i < lv->count; ++i )
    {
        if ( item == -1 || i == item )
        {
            UINT old = lv->aItems[i].state;
            UINT now = (old & ~mask) | (state & mask);
            if ( (now & LVIS_SELECTED) && (p->style & LVS_SINGLESEL) )
            {
                int k;
                for ( k = 0; k < lv->count; ++k )
                {
                    if ( k != i && (lv->aItems[k].state & LVIS_SELECTED) )
                    {
                        UINT o = lv->aItems[k].state;
                        lv->aItems[k].state &= ~(UINT)LVIS_SELECTED;
                        if ( bNotify )
                        {
                            gui_LVNotify(p, LVN_ITEMCHANGED, k, 0, lv->aItems[k].state, o, LVIF_STATE, none);
                        }
                    }
                }
            }
            if ( now & LVIS_FOCUSED )
            {
                if ( lv->focus >= 0 && lv->focus < lv->count && lv->focus != i )
                {
                    lv->aItems[lv->focus].state &= ~(UINT)LVIS_FOCUSED;
                }
                lv->focus = i;
            }
            lv->aItems[i].state = now;
            if ( now != old && bNotify )
            {
                gui_LVNotify(p, LVN_ITEMCHANGED, i, 0, now, old, LVIF_STATE, none);
            }
        }
    }
    J3DGui_Invalidate(p);
}

static void gui_LVEnsureVisible(J3DGuiWindow* p, int item)
{
    GuiListView* lv = gui_LVData(p);
    int rows = gui_LVRows(p);
    if ( item < 0 )
    {
        return;
    }
    if ( item < lv->top )
    {
        lv->top = item;
    }
    else if ( item >= lv->top + rows )
    {
        lv->top = item - rows + 1;
    }
    lv->top = GUI_MAX(0, GUI_MIN(lv->top, GUI_MAX(0, lv->count - rows)));
    J3DGui_Invalidate(p);
}

static int gui_LVHitItem(J3DGuiWindow* p, POINT pt)
{
    GuiListView* lv = gui_LVData(p);
    RECT client;
    int row;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    if ( pt.x < 0 || pt.x >= client.right || pt.y < gui_LVHeaderHeight(p) || pt.y >= client.bottom )
    {
        return -1;
    }
    row = lv->top + (pt.y - gui_LVHeaderHeight(p)) / gui_LVRowHeight(p);
    return row < lv->count ? row : -1;
}

static void gui_LVItemRect(J3DGuiWindow* p, int item, int code, RECT* pRect)
{
    GuiListView* lv = gui_LVData(p);
    RECT client;
    int y = gui_LVHeaderHeight(p) + (item - lv->top) * gui_LVRowHeight(p);
    int width = 0;
    int i;
    GetClientRect(J3DGui_ToHwnd(p), &client);
    for ( i = 0; i < lv->numColumns; ++i )
    {
        width += lv->aColumns[i].width;
    }
    switch ( code )
    {
        case LVIR_ICON:
            SetRect(pRect, 2, y, 2 + 16, y + gui_LVRowHeight(p));
            break;
        case LVIR_LABEL:
            SetRect(pRect, lv->hSmall ? 20 : 2, y, lv->numColumns ? lv->aColumns[0].width : client.right, y + gui_LVRowHeight(p));
            break;
        case LVIR_SELECTBOUNDS:
            SetRect(pRect, lv->hSmall ? 2 : 0, y, (lv->exStyle & LVS_EX_FULLROWSELECT) ? width : (lv->numColumns ? lv->aColumns[0].width : client.right),
                    y + gui_LVRowHeight(p));
            break;
        default: // LVIR_BOUNDS
            SetRect(pRect, 0, y, width ? width : client.right, y + gui_LVRowHeight(p));
            break;
    }
}

static void gui_LVPaint(J3DGuiWindow* p)
{
    GuiListView* lv = gui_LVData(p);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(J3DGui_ToHwnd(p), &ps);
    RECT client;
    HFONT hFont = J3DGui_GetFont(p);
    int header = gui_LVHeaderHeight(p);
    int rowH = gui_LVRowHeight(p);
    int textH = J3DGui_FontHeight(hFont);
    int bFocus = J3DGui_GetFocusWindow() == p;
    int bEnabled = J3DGui_IsReallyEnabled(p);
    int i;
    int c;
    int rows;

    GetClientRect(J3DGui_ToHwnd(p), &client);
    J3DGui_FillRect32(hdc, &client, J3DGui_SysColor(COLOR_WINDOW));

    // the column header: raised buttons, the text left-aligned (or as the column's format), then face to the right
    if ( header )
    {
        int x = 0;
        for ( c = 0; c < lv->numColumns; ++c )
        {
            RECT b;
            SetRect(&b, x, 0, x + lv->aColumns[c].width, header);
            {
                RECT inner = b;
                J3DGui_DrawEdge3D(hdc, &inner, GUI_EDGE_RAISED);
                J3DGui_FillRect32(hdc, &inner, J3DGui_SysColor(COLOR_3DFACE));
            }
            if ( lv->aColumns[c].pText )
            {
                RECT t = b;
                InflateRect(&t, -6, 0);
                SelectObject(hdc, hFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
                DrawTextA(hdc, lv->aColumns[c].pText, -1, &t,
                          DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (lv->aColumns[c].fmt & LVCFMT_RIGHT ? DT_RIGHT : lv->aColumns[c].fmt & LVCFMT_CENTER ? DT_CENTER : DT_LEFT));
            }
            x += lv->aColumns[c].width;
        }
        if ( x < client.right )
        {
            RECT rest;
            SetRect(&rest, x, 0, client.right, header);
            J3DGui_DrawEdge3D(hdc, &rest, GUI_EDGE_RAISED);
            J3DGui_FillRect32(hdc, &rest, J3DGui_SysColor(COLOR_3DFACE));
        }
    }

    rows = (client.bottom - header + rowH - 1) / rowH;
    for ( i = 0; i < rows && lv->top + i < lv->count; ++i )
    {
        int index = lv->top + i;
        GuiLVItem* it = &lv->aItems[index];
        int y = header + i * rowH;
        int bSelected = (it->state & LVIS_SELECTED) && (bFocus || (p->style & LVS_SHOWSELALWAYS));
        int x = 0;
        RECT sel;
        gui_LVItemRect(p, index, LVIR_SELECTBOUNDS, &sel);
        if ( lv->hSmall && it->image >= 0 )
        {
            ImageList_Draw(lv->hSmall, it->image, hdc, 2, y + (rowH - 16) / 2, ILD_NORMAL);
        }
        for ( c = 0; c < GUI_MAX(lv->numColumns, 1); ++c )
        {
            int colW = lv->numColumns ? lv->aColumns[c].width : client.right;
            int textX = x + (c == 0 ? (lv->hSmall ? 20 : 2) : 6);
            const char* s = it->apText[c] ? it->apText[c] : "";
            uint32_t color = J3DGui_SysColor(bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
            RECT cell;
            SetRect(&cell, textX, y, x + colW - 2, y + rowH);
            if ( bSelected && (c == 0 || (lv->exStyle & LVS_EX_FULLROWSELECT)) )
            {
                RECT hl;
                SIZE size;
                J3DGui_TextExtent(hFont, s, (int)strlen(s), &size);
                if ( lv->exStyle & LVS_EX_FULLROWSELECT )
                {
                    SetRect(&hl, c == 0 ? sel.left + (lv->hSmall ? 18 : 0) : x, y, x + colW, y + rowH);
                }
                else
                {
                    SetRect(&hl, textX - 2, y, GUI_MIN(textX + size.cx + 2, x + colW), y + rowH);
                }
                J3DGui_FillRect32(hdc, &hl, J3DGui_SysColor(bFocus ? COLOR_HIGHLIGHT : COLOR_3DFACE));
                color = J3DGui_SysColor(bFocus ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
            }
            SelectObject(hdc, hFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
            DrawTextA(hdc, s, -1, &cell, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            x += colW;
        }
        if ( bFocus && index == lv->focus )
        {
            J3DGui_DrawFocusRect32(hdc, &sel);
        }
        (void)textH;
    }
    EndPaint(J3DGui_ToHwnd(p), &ps);
}

static int gui_LVGetItem(GuiListView* lv, LVITEMA* pItem)
{
    GuiLVItem* it;
    if ( !pItem || pItem->iItem < 0 || pItem->iItem >= lv->count )
    {
        return FALSE;
    }
    it = &lv->aItems[pItem->iItem];
    if ( pItem->mask & LVIF_TEXT && pItem->pszText && pItem->cchTextMax > 0 )
    {
        const char* s = pItem->iSubItem >= 0 && pItem->iSubItem < GUI_LV_MAXCOLUMNS && it->apText[pItem->iSubItem] ? it->apText[pItem->iSubItem] : "";
        strncpy(pItem->pszText, s, (size_t)pItem->cchTextMax - 1);
        pItem->pszText[pItem->cchTextMax - 1] = 0;
    }
    if ( pItem->mask & LVIF_IMAGE )
    {
        pItem->iImage = it->image;
    }
    if ( pItem->mask & LVIF_PARAM )
    {
        pItem->lParam = it->lParam;
    }
    if ( pItem->mask & LVIF_STATE )
    {
        pItem->state = it->state & pItem->stateMask;
    }
    return TRUE;
}

static int gui_LVSetItem(J3DGuiWindow* p, GuiListView* lv, const LVITEMA* pItem)
{
    GuiLVItem* it;
    if ( !pItem || pItem->iItem < 0 || pItem->iItem >= lv->count )
    {
        return FALSE;
    }
    it = &lv->aItems[pItem->iItem];
    if ( pItem->mask & LVIF_TEXT && pItem->iSubItem >= 0 && pItem->iSubItem < GUI_LV_MAXCOLUMNS )
    {
        free(it->apText[pItem->iSubItem]);
        it->apText[pItem->iSubItem] = strdup(pItem->pszText && pItem->pszText != LPSTR_TEXTCALLBACKA ? pItem->pszText : "");
    }
    if ( pItem->iSubItem == 0 )
    {
        if ( pItem->mask & LVIF_IMAGE )
        {
            it->image = pItem->iImage;
        }
        if ( pItem->mask & LVIF_PARAM )
        {
            it->lParam = pItem->lParam;
        }
        if ( pItem->mask & LVIF_STATE )
        {
            gui_LVSetState(p, pItem->iItem, pItem->state, pItem->stateMask, 0);
        }
    }
    J3DGui_Invalidate(p);
    return TRUE;
}

LRESULT CALLBACK J3DGui_ListViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    GuiListView* lv;
    int i;
    if ( !p )
    {
        return 0;
    }
    lv = gui_LVData(p);
    switch ( msg )
    {
        case WM_NCDESTROY:
            for ( i = 0; i < lv->count; ++i )
            {
                gui_LVFreeItem(&lv->aItems[i]);
            }
            for ( i = 0; i < lv->numColumns; ++i )
            {
                free(lv->aColumns[i].pText);
            }
            free(lv->aItems);
            free(lv);
            p->pData = NULL;
            return 0;
        case WM_PAINT:
            gui_LVPaint(p);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case LVM_SETEXTENDEDLISTVIEWSTYLE:
        {
            DWORD old = lv->exStyle;
            DWORD mask = wParam ? (DWORD)wParam : 0xFFFFFFFFu;
            lv->exStyle = (lv->exStyle & ~mask) | ((DWORD)lParam & mask);
            J3DGui_Invalidate(p);
            return (LRESULT)old;
        }
        case LVM_GETEXTENDEDLISTVIEWSTYLE:
            return (LRESULT)lv->exStyle;
        case LVM_SETIMAGELIST:
        {
            HIMAGELIST hOld;
            if ( wParam == LVSIL_SMALL )
            {
                hOld = lv->hSmall;
                lv->hSmall = (HIMAGELIST)lParam;
            }
            else
            {
                hOld = lv->hNormal;
                lv->hNormal = (HIMAGELIST)lParam;
            }
            J3DGui_Invalidate(p);
            return (LRESULT)hOld;
        }
        case LVM_GETIMAGELIST:
            return (LRESULT)(wParam == LVSIL_SMALL ? lv->hSmall : lv->hNormal);
        case LVM_INSERTCOLUMNA:
        {
            const LVCOLUMNA* pCol = (const LVCOLUMNA*)lParam;
            int at = GUI_MIN((int)wParam, lv->numColumns);
            if ( !pCol || lv->numColumns >= GUI_LV_MAXCOLUMNS )
            {
                return -1;
            }
            memmove(&lv->aColumns[at + 1], &lv->aColumns[at], (size_t)(lv->numColumns - at) * sizeof(GuiLVColumn));
            memset(&lv->aColumns[at], 0, sizeof(GuiLVColumn));
            lv->aColumns[at].pText = strdup((pCol->mask & LVCF_TEXT) && pCol->pszText ? pCol->pszText : "");
            lv->aColumns[at].width = (pCol->mask & LVCF_WIDTH) ? pCol->cx : 50;
            lv->aColumns[at].fmt = (pCol->mask & LVCF_FMT) ? pCol->fmt : LVCFMT_LEFT;
            lv->numColumns++;
            J3DGui_Invalidate(p);
            return at;
        }
        case LVM_SETCOLUMNWIDTH:
            if ( (int)wParam >= 0 && (int)wParam < lv->numColumns )
            {
                int cx = (int)(short)LOWORD(lParam);
                if ( cx == LVSCW_AUTOSIZE || cx == LVSCW_AUTOSIZE_USEHEADER )
                {
                    RECT client;
                    GetClientRect(hwnd, &client);
                    cx = client.right;
                }
                lv->aColumns[wParam].width = cx;
                J3DGui_Invalidate(p);
                return TRUE;
            }
            return FALSE;
        case LVM_GETCOLUMNWIDTH:
            return (int)wParam >= 0 && (int)wParam < lv->numColumns ? lv->aColumns[wParam].width : 0;
        case LVM_INSERTITEMA:
        {
            const LVITEMA* pItem = (const LVITEMA*)lParam;
            int at;
            if ( !pItem )
            {
                return -1;
            }
            if ( lv->count == lv->capacity )
            {
                int capacity = lv->capacity ? lv->capacity * 2 : 16;
                GuiLVItem* a = realloc(lv->aItems, (size_t)capacity * sizeof(GuiLVItem));
                if ( !a )
                {
                    return -1;
                }
                lv->aItems = a;
                lv->capacity = capacity;
            }
            at = pItem->iItem < 0 || pItem->iItem > lv->count ? lv->count : pItem->iItem;
            memmove(&lv->aItems[at + 1], &lv->aItems[at], (size_t)(lv->count - at) * sizeof(GuiLVItem));
            memset(&lv->aItems[at], 0, sizeof(GuiLVItem));
            lv->aItems[at].image = (pItem->mask & LVIF_IMAGE) ? pItem->iImage : -1;
            lv->aItems[at].lParam = (pItem->mask & LVIF_PARAM) ? pItem->lParam : 0;
            lv->aItems[at].apText[0] = strdup((pItem->mask & LVIF_TEXT) && pItem->pszText && pItem->pszText != LPSTR_TEXTCALLBACKA ? pItem->pszText : "");
            if ( pItem->mask & LVIF_STATE )
            {
                lv->aItems[at].state = pItem->state & pItem->stateMask;
            }
            lv->count++;
            if ( lv->focus >= at )
            {
                lv->focus++;
            }
            J3DGui_Invalidate(p);
            return at;
        }
        case LVM_DELETEITEM:
        {
            int at = (int)wParam;
            if ( at < 0 || at >= lv->count )
            {
                return FALSE;
            }
            gui_LVFreeItem(&lv->aItems[at]);
            memmove(&lv->aItems[at], &lv->aItems[at + 1], (size_t)(lv->count - at - 1) * sizeof(GuiLVItem));
            lv->count--;
            if ( lv->focus == at )
            {
                lv->focus = -1;
            }
            else if ( lv->focus > at )
            {
                lv->focus--;
            }
            lv->top = GUI_MAX(0, GUI_MIN(lv->top, lv->count - gui_LVRows(p)));
            J3DGui_Invalidate(p);
            return TRUE;
        }
        case LVM_DELETEALLITEMS:
            for ( i = 0; i < lv->count; ++i )
            {
                gui_LVFreeItem(&lv->aItems[i]);
            }
            lv->count = 0;
            lv->top = 0;
            lv->focus = -1;
            J3DGui_Invalidate(p);
            return TRUE;
        case LVM_GETITEMCOUNT:
            return lv->count;
        case LVM_GETITEMA:
            return gui_LVGetItem(lv, (LVITEMA*)lParam);
        case LVM_SETITEMA:
            return gui_LVSetItem(p, lv, (const LVITEMA*)lParam);
        case LVM_SETITEMTEXTA:
        {
            LVITEMA item = *(const LVITEMA*)lParam;
            item.iItem = (int)wParam;
            item.mask = LVIF_TEXT;
            return gui_LVSetItem(p, lv, &item);
        }
        case LVM_GETITEMTEXTA:
        {
            LVITEMA* pItem = (LVITEMA*)lParam;
            if ( !pItem )
            {
                return 0;
            }
            pItem->iItem = (int)wParam;
            pItem->mask = LVIF_TEXT;
            gui_LVGetItem(lv, pItem);
            return pItem->pszText ? (LRESULT)strlen(pItem->pszText) : 0;
        }
        case LVM_SETITEMSTATE:
        {
            const LVITEMA* pItem = (const LVITEMA*)lParam;
            if ( pItem )
            {
                gui_LVSetState(p, (int)wParam, pItem->state, pItem->stateMask, 1);
            }
            return TRUE;
        }
        case LVM_GETITEMSTATE:
            return (int)wParam >= 0 && (int)wParam < lv->count ? (LRESULT)(lv->aItems[wParam].state & (UINT)lParam) : 0;
        case LVM_GETNEXTITEM:
        {
            int start = (int)wParam;
            UINT flags = (UINT)LOWORD(lParam);
            for ( i = start + 1; i < lv->count; ++i )
            {
                if ( (!(flags & LVNI_SELECTED) || (lv->aItems[i].state & LVIS_SELECTED)) && (!(flags & LVNI_FOCUSED) || i == lv->focus) )
                {
                    return i;
                }
            }
            return -1;
        }
        case LVM_GETSELECTEDCOUNT:
        {
            int n = 0;
            for ( i = 0; i < lv->count; ++i )
            {
                n += (lv->aItems[i].state & LVIS_SELECTED) != 0;
            }
            return n;
        }
        case LVM_GETCOUNTPERPAGE:
            return gui_LVRows(p);
        case LVM_GETTOPINDEX:
            return lv->top;
        case LVM_ENSUREVISIBLE:
            gui_LVEnsureVisible(p, (int)wParam);
            return TRUE;
        case LVM_GETITEMRECT:
            if ( lParam && (int)wParam >= 0 && (int)wParam < lv->count )
            {
                gui_LVItemRect(p, (int)wParam, ((RECT*)lParam)->left, (RECT*)lParam);
                return TRUE;
            }
            return FALSE;
        case LVM_HITTEST:
        {
            LVHITTESTINFO* pHit = (LVHITTESTINFO*)lParam;
            if ( !pHit )
            {
                return -1;
            }
            pHit->iItem = gui_LVHitItem(p, pHit->pt);
            pHit->flags = pHit->iItem >= 0 ? LVHT_ONITEM : LVHT_NOWHERE;
            return pHit->iItem;
        }
        case LVM_REDRAWITEMS:
        case LVM_UPDATE:
            J3DGui_Invalidate(p);
            return TRUE;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            int item = gui_LVHitItem(p, pt);
            SetFocus(hwnd);
            if ( item >= 0 )
            {
                gui_LVSetState(p, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED, 1);
                gui_LVEnsureVisible(p, item);
                lv->bPressed = 1;
                lv->pressItem = item;
                lv->pressPoint = pt;
                lv->pressButton = msg == WM_RBUTTONDOWN ? 2 : 1;
                SetCapture(hwnd);
            }
            else
            {
                // a click on no item clears the selection, as Windows does
                gui_LVSetState(p, -1, 0, LVIS_SELECTED, 1);
            }
            if ( msg == WM_LBUTTONDBLCLK )
            {
                NMHDR hdr;
                lv->bPressed = 0;
                if ( GetCapture() == hwnd )
                {
                    ReleaseCapture();
                }
                hdr.hwndFrom = hwnd;
                hdr.idFrom = (UINT_PTR)p->id;
                hdr.code = (UINT)NM_DBLCLK;
                if ( p->pParent )
                {
                    J3DGui_Send(p->pParent, WM_NOTIFY, (WPARAM)p->id, (LPARAM)&hdr);
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if ( lv->bPressed )
            {
                POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
                if ( abs(pt.x - lv->pressPoint.x) > 4 || abs(pt.y - lv->pressPoint.y) > 4 )
                {
                    // a drag begins: the list view lets go and tells its parent, which takes over
                    lv->bPressed = 0;
                    if ( GetCapture() == hwnd )
                    {
                        ReleaseCapture();
                    }
                    gui_LVNotify(p, lv->pressButton == 2 ? LVN_BEGINRDRAG : LVN_BEGINDRAG, lv->pressItem, 0, 0, 0, 0, lv->pressPoint);
                }
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            if ( lv->bPressed )
            {
                NMHDR hdr;
                lv->bPressed = 0;
                if ( GetCapture() == hwnd )
                {
                    ReleaseCapture();
                }
                hdr.hwndFrom = hwnd;
                hdr.idFrom = (UINT_PTR)p->id;
                hdr.code = (UINT)(msg == WM_RBUTTONUP ? NM_RCLICK : NM_CLICK);
                if ( p->pParent )
                {
                    J3DGui_Send(p->pParent, WM_NOTIFY, (WPARAM)p->id, (LPARAM)&hdr);
                }
            }
            return 0;
        case WM_MOUSEWHEEL:
            lv->top = GUI_MAX(0, GUI_MIN(lv->top - (short)HIWORD(wParam) / WHEEL_DELTA * 3, lv->count - gui_LVRows(p)));
            J3DGui_Invalidate(p);
            return 0;
        case WM_KEYDOWN:
        {
            int item = lv->focus < 0 ? 0 : lv->focus;
            int rows = gui_LVRows(p);
            NMLVKEYDOWN kd;
            memset(&kd, 0, sizeof(kd));
            kd.hdr.hwndFrom = hwnd;
            kd.hdr.idFrom = (UINT_PTR)p->id;
            kd.hdr.code = (UINT)LVN_KEYDOWN;
            kd.wVKey = (WORD)wParam;
            if ( p->pParent )
            {
                J3DGui_Send(p->pParent, WM_NOTIFY, (WPARAM)p->id, (LPARAM)&kd);
            }
            switch ( wParam )
            {
                case VK_UP:    item = GUI_MAX(0, item - 1); break;
                case VK_DOWN:  item = GUI_MIN(lv->count - 1, item + 1); break;
                case VK_PRIOR: item = GUI_MAX(0, item - rows + 1); break;
                case VK_NEXT:  item = GUI_MIN(lv->count - 1, item + rows - 1); break;
                case VK_HOME:  item = 0; break;
                case VK_END:   item = lv->count - 1; break;
                default:       return 0;
            }
            if ( item >= 0 && lv->count )
            {
                gui_LVSetState(p, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED, 1);
                gui_LVEnsureVisible(p, item);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
            J3DGui_Invalidate(p);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK J3DGui_HeaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // the list view draws its own header; a header window of its own is never created
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
