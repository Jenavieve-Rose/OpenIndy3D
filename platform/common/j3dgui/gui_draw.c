/*
 * j3dgui's classic look (PORT-PLAN.md P6.5, decision 20): the bevels, boxes and arrows of Windows 98's "Windows
 * Standard" scheme, and the non-client area of a dialog (frame, gradient caption, close button).
 *
 * The edges follow DrawEdge: a raised edge is 3D-light/3D-dark-shadow outside and highlight/shadow inside, a sunken
 * one shadow/highlight outside and dark-shadow/light inside. With 3D-light equal to the face colour (as in the
 * Windows Standard scheme), a button's white highlight sits one pixel in from its top-left, and its black shadow on
 * its outer bottom-right, which is the look of the period.
 */
#include "gui_internal.h"

#include <string.h>

static void gui_HLine(HDC hdc, int x0, int x1, int y, uint32_t c)
{
    RECT r;
    SetRect(&r, x0, y, x1, y + 1);
    J3DGui_FillRect32(hdc, &r, c);
}

static void gui_VLine(HDC hdc, int x, int y0, int y1, uint32_t c)
{
    RECT r;
    SetRect(&r, x, y0, x + 1, y1);
    J3DGui_FillRect32(hdc, &r, c);
}

// One ring: top and left in tl, bottom and right in br. Shrinks *pRect by one.
static void gui_Ring(HDC hdc, RECT* pRect, uint32_t tl, uint32_t br)
{
    RECT* r = pRect;
    if ( r->right - r->left < 1 || r->bottom - r->top < 1 )
    {
        return;
    }
    gui_HLine(hdc, r->left, r->right - 1, r->top, tl);
    gui_VLine(hdc, r->left, r->top, r->bottom - 1, tl);
    gui_HLine(hdc, r->left, r->right, r->bottom - 1, br);
    gui_VLine(hdc, r->right - 1, r->top, r->bottom, br);
    InflateRect(r, -1, -1);
}

void J3DGui_DrawEdge3D(HDC hdc, RECT* pRect, int style)
{
    const uint32_t face = J3DGui_SysColor(COLOR_3DFACE);
    const uint32_t hi = J3DGui_SysColor(COLOR_3DHILIGHT);
    const uint32_t light = J3DGui_SysColor(COLOR_3DLIGHT);
    const uint32_t shadow = J3DGui_SysColor(COLOR_3DSHADOW);
    const uint32_t dark = J3DGui_SysColor(COLOR_3DDKSHADOW);
    (void)face;
    switch ( style )
    {
        case GUI_EDGE_RAISED_OUTER:
            gui_Ring(hdc, pRect, light, dark);
            break;
        case GUI_EDGE_RAISED:
            gui_Ring(hdc, pRect, light, dark);
            gui_Ring(hdc, pRect, hi, shadow);
            break;
        case GUI_EDGE_SUNKEN:
            gui_Ring(hdc, pRect, shadow, hi);
            gui_Ring(hdc, pRect, dark, light);
            break;
        case GUI_EDGE_PUSHED:
            gui_Ring(hdc, pRect, dark, dark);
            gui_Ring(hdc, pRect, shadow, shadow);
            break;
        case GUI_EDGE_DEFAULT:
            gui_Ring(hdc, pRect, dark, dark);
            gui_Ring(hdc, pRect, hi, dark);
            gui_Ring(hdc, pRect, light, shadow);
            break;
        case GUI_EDGE_ETCHED:
        {
            // shadow line, then a highlight line one pixel in and down
            RECT r = *pRect;
            gui_HLine(hdc, r.left, r.right - 1, r.top, shadow);
            gui_VLine(hdc, r.left, r.top, r.bottom - 1, shadow);
            gui_HLine(hdc, r.left, r.right - 1, r.bottom - 2, shadow);
            gui_VLine(hdc, r.right - 2, r.top, r.bottom - 1, shadow);
            gui_HLine(hdc, r.left + 1, r.right - 2, r.top + 1, hi);
            gui_VLine(hdc, r.left + 1, r.top + 1, r.bottom - 2, hi);
            gui_HLine(hdc, r.left, r.right, r.bottom - 1, hi);
            gui_VLine(hdc, r.right - 1, r.top, r.bottom, hi);
            InflateRect(pRect, -2, -2);
            break;
        }
        case GUI_EDGE_STATIC_SUNKEN:
            gui_Ring(hdc, pRect, shadow, hi);
            break;
        default:
            break;
    }
}

void J3DGui_DrawFocusRect32(HDC hdc, const RECT* pRect)
{
    // a dotted rectangle, every other pixel (XOR in Windows; on the face colour it reads as black dots)
    const uint32_t c = 0;
    int x;
    int y;
    RECT r;
    for ( x = pRect->left; x < pRect->right; x += 2 )
    {
        SetRect(&r, x, pRect->top, x + 1, pRect->top + 1);
        J3DGui_FillRect32(hdc, &r, c);
        SetRect(&r, x, pRect->bottom - 1, x + 1, pRect->bottom);
        J3DGui_FillRect32(hdc, &r, c);
    }
    for ( y = pRect->top; y < pRect->bottom; y += 2 )
    {
        SetRect(&r, pRect->left, y, pRect->left + 1, y + 1);
        J3DGui_FillRect32(hdc, &r, c);
        SetRect(&r, pRect->right - 1, y, pRect->right, y + 1);
        J3DGui_FillRect32(hdc, &r, c);
    }
}

void J3DGui_DrawCheck(HDC hdc, const RECT* pBox, int bChecked, int bEnabled, int bPushed)
{
    // a 13x13 sunken box, white inside (face colour when pushed or disabled), with a 7x7 check mark
    static const uint8_t aMark[7] = { 0x01, 0x03, 0x47, 0x6E, 0x7C, 0x38, 0x10 };
    RECT r = *pBox;
    RECT inner;
    int y;
    int x;
    J3DGui_DrawEdge3D(hdc, &r, GUI_EDGE_SUNKEN);
    inner = r;
    J3DGui_FillRect32(hdc, &inner, J3DGui_SysColor(bPushed || !bEnabled ? COLOR_3DFACE : COLOR_WINDOW));
    if ( bChecked )
    {
        uint32_t c = J3DGui_SysColor(bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
        for ( y = 0; y < 7; ++y )
        {
            for ( x = 0; x < 7; ++x )
            {
                if ( aMark[y] & (0x40 >> x) )
                {
                    RECT px;
                    SetRect(&px, inner.left + 1 + x, inner.top + 1 + y, inner.left + 2 + x, inner.top + 2 + y);
                    J3DGui_FillRect32(hdc, &px, c);
                }
            }
        }
    }
}

void J3DGui_DrawRadio(HDC hdc, const RECT* pBox, int bChecked, int bEnabled, int bPushed)
{
    // Windows 98's 12x12 radio button, as pixel rows: 's' shadow, 'd' dark shadow, 'h' highlight, 'l' light,
    // 'w' the inside, '.' untouched
    static const char* aRows[12] = {
        "....ssss....",
        "..ssddddss..",
        ".sdd....ddl.",
        ".sd......lh.",
        "sd........lh",
        "sd........lh",
        "sd........lh",
        "sd........lh",
        ".sd......lh.",
        ".sll....llh.",
        "..hhllllhh..",
        "....hhhh....",
    };
    const uint32_t inside = J3DGui_SysColor(bPushed || !bEnabled ? COLOR_3DFACE : COLOR_WINDOW);
    int x;
    int y;
    for ( y = 0; y < 12; ++y )
    {
        int first = -1;
        int last = -1;
        for ( x = 0; x < 12; ++x )
        {
            uint32_t c;
            RECT px;
            switch ( aRows[y][x] )
            {
                case 's': c = J3DGui_SysColor(COLOR_3DSHADOW); break;
                case 'd': c = J3DGui_SysColor(COLOR_3DDKSHADOW); break;
                case 'h': c = J3DGui_SysColor(COLOR_3DHILIGHT); break;
                case 'l': c = J3DGui_SysColor(COLOR_3DLIGHT); break;
                default: continue;
            }
            if ( first < 0 )
            {
                first = x;
            }
            last = x;
            SetRect(&px, pBox->left + x, pBox->top + y, pBox->left + x + 1, pBox->top + y + 1);
            J3DGui_FillRect32(hdc, &px, c);
        }
        // fill between the outline's two sides
        if ( y >= 2 && y <= 9 && first >= 0 )
        {
            int x0 = first;
            int x1 = last;
            while ( x0 < 12 && aRows[y][x0] != '.' )
            {
                x0++;
            }
            while ( x1 >= 0 && aRows[y][x1] != '.' )
            {
                x1--;
            }
            if ( x0 <= x1 )
            {
                RECT fill;
                SetRect(&fill, pBox->left + x0, pBox->top + y, pBox->left + x1 + 1, pBox->top + y + 1);
                J3DGui_FillRect32(hdc, &fill, inside);
            }
        }
    }
    if ( bChecked )
    {
        static const char* aDot[4] = { ".xx.", "xxxx", "xxxx", ".xx." };
        uint32_t c = J3DGui_SysColor(bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
        for ( y = 0; y < 4; ++y )
        {
            for ( x = 0; x < 4; ++x )
            {
                if ( aDot[y][x] == 'x' )
                {
                    RECT px;
                    SetRect(&px, pBox->left + 4 + x, pBox->top + 4 + y, pBox->left + 5 + x, pBox->top + 5 + y);
                    J3DGui_FillRect32(hdc, &px, c);
                }
            }
        }
    }
}

void J3DGui_DrawArrow(HDC hdc, const RECT* pRect, int direction, int bEnabled)
{
    // a 7-pixel-wide solid triangle, centred (disabled: grey with a white copy one pixel down and right)
    int cx = (pRect->left + pRect->right) / 2;
    int cy = (pRect->top + pRect->bottom) / 2;
    int pass;
    for ( pass = bEnabled ? 1 : 0; pass < 2; ++pass )
    {
        uint32_t c = pass == 0 ? J3DGui_SysColor(COLOR_3DHILIGHT) : J3DGui_SysColor(bEnabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
        int o = pass == 0 ? 1 : 0;
        int i;
        for ( i = 0; i < 4; ++i )
        {
            RECT r;
            switch ( direction )
            {
                case 0: SetRect(&r, cx - i + o, cy - 2 + i + o, cx + i + 1 + o, cy - 1 + i + o); break;  // up
                case 1: SetRect(&r, cx - 3 + i + o, cy - 1 + i + o, cx + 4 - i + o, cy + i + o); break;  // down
                case 2: SetRect(&r, cx - 2 + i + o, cy - i + o, cx - 1 + i + o, cy + i + 1 + o); break;  // left
                default: SetRect(&r, cx - 1 + i + o, cy - 3 + i + o, cx + i + o, cy + 4 - i + o); break; // right
            }
            J3DGui_FillRect32(hdc, &r, c);
        }
    }
}

void J3DGui_DrawScrollButton(HDC hdc, const RECT* pRect, int direction, int bPushed, int bEnabled)
{
    RECT r = *pRect;
    RECT arrow;
    J3DGui_DrawEdge3D(hdc, &r, bPushed ? GUI_EDGE_PUSHED : GUI_EDGE_RAISED);
    J3DGui_FillRect32(hdc, &r, J3DGui_SysColor(COLOR_3DFACE));
    arrow = *pRect;
    if ( bPushed )
    {
        OffsetRect(&arrow, 1, 1);
    }
    J3DGui_DrawArrow(hdc, &arrow, direction, bEnabled);
}

void J3DGui_DrawEtchedText(HDC hdc, HFONT hFont, const char* pText, int x, int y, int bMnemonic)
{
    J3DGui_DrawText32(hdc, hFont, pText, -1, x + 1, y + 1, J3DGui_SysColor(COLOR_3DHILIGHT), bMnemonic);
    J3DGui_DrawText32(hdc, hFont, pText, -1, x, y, J3DGui_SysColor(COLOR_GRAYTEXT), bMnemonic);
}

/* ---- The non-client area ---------------------------------------------------------------------------------------------- */

void J3DGuiDraw_NonClient(J3DGuiWindow* p)
{
    HDC hdc;
    RECT r;
    RECT client;
    int bActive;

    if ( !p || p->pParent )
    {
        // a child's border: the sunken client edge or a plain line
        RECT inner;
        if ( !p )
        {
            return;
        }
        J3DGui_GetClientInWindow(p, &inner);
        if ( inner.left == 0 )
        {
            return;
        }
        hdc = J3DGui_GetWindowDC(p, 1);
        SetRect(&r, 0, 0, p->rect.right - p->rect.left, p->rect.bottom - p->rect.top);
        if ( inner.left >= 2 )
        {
            J3DGui_DrawEdge3D(hdc, &r, GUI_EDGE_SUNKEN);
        }
        else
        {
            HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
            FrameRect(hdc, &r, hBlack);
            DeleteObject(hBlack);
        }
        J3DGui_ReleaseWindowDC(hdc);
        return;
    }

    hdc = J3DGui_GetWindowDC(p, 1);
    SetRect(&r, 0, 0, p->rect.right - p->rect.left, p->rect.bottom - p->rect.top);
    J3DGui_GetClientInWindow(p, &client);

    if ( client.left >= 3 )
    {
        J3DGui_DrawEdge3D(hdc, &r, GUI_EDGE_RAISED);
        {
            RECT ring = r;
            uint32_t face = J3DGui_SysColor(COLOR_3DFACE);
            gui_Ring(hdc, &ring, face, face);
        }
        InflateRect(&r, -1, -1);
    }
    else if ( client.left >= 1 )
    {
        HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
        FrameRect(hdc, &r, hBlack);
        DeleteObject(hBlack);
        InflateRect(&r, -1, -1);
    }

    if ( (p->style & WS_CAPTION) == WS_CAPTION )
    {
        RECT caption;
        uint32_t c0;
        uint32_t c1;
        int x;
        int width;
        HFONT hFont = J3DGui_CaptionFont();
        int textY;
        bActive = J3DGui_ActiveDialog() == p;
        SetRect(&caption, r.left, r.top, r.right, r.top + 18 - 1);
        c0 = J3DGui_SysColor(bActive ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
        c1 = J3DGui_SysColor(bActive ? 27 : 28); // the Windows 98 gradient's second colour
        width = caption.right - caption.left;
        for ( x = 0; x < width; ++x )
        {
            RECT col;
            uint32_t r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
            uint32_t r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
            uint32_t t = width > 1 ? (uint32_t)(x * 255 / (width - 1)) : 0;
            uint32_t c = (((r0 * (255 - t) + r1 * t) / 255) << 16) | (((g0 * (255 - t) + g1 * t) / 255) << 8) | ((b0 * (255 - t) + b1 * t) / 255);
            SetRect(&col, caption.left + x, caption.top, caption.left + x + 1, caption.bottom);
            J3DGui_FillRect32(hdc, &col, c);
        }
        // the line under the caption is the face colour
        {
            RECT line;
            SetRect(&line, r.left, caption.bottom, r.right, caption.bottom + 1);
            J3DGui_FillRect32(hdc, &line, J3DGui_SysColor(COLOR_3DFACE));
        }
        textY = caption.top + (caption.bottom - caption.top - J3DGui_FontHeight(hFont)) / 2;
        J3DGui_DrawText32(hdc, hFont, p->pText, -1, caption.left + 4, textY, J3DGui_SysColor(bActive ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT), -1);

        if ( p->style & WS_SYSMENU )
        {
            // the close button: 16x14, two pixels in from the right, an X of two-pixel strokes
            RECT button;
            int i;
            SetRect(&button, caption.right - 2 - 16, caption.top + 2, caption.right - 2, caption.top + 2 + 14);
            J3DGui_DrawEdge3D(hdc, &button, GUI_EDGE_RAISED);
            J3DGui_FillRect32(hdc, &button, J3DGui_SysColor(COLOR_3DFACE));
            for ( i = 0; i < 6; ++i )
            {
                RECT a;
                RECT b;
                int bx = button.left + 3;
                int by = button.top + 2;
                SetRect(&a, bx + i, by + i, bx + i + 2, by + i + 1);
                SetRect(&b, bx + 5 - i, by + i, bx + 7 - i, by + i + 1);
                J3DGui_FillRect32(hdc, &a, 0);
                J3DGui_FillRect32(hdc, &b, 0);
            }
        }
    }
    J3DGui_ReleaseWindowDC(hdc);
}

void J3DGuiDraw_CloseButtonRect(const J3DGuiWindow* p, RECT* pRect)
{
    // in window coordinates; the counterpart of the drawing above
    int frame = 3;
    int right = p->rect.right - p->rect.left - frame;
    SetRect(pRect, right - 2 - 16, frame + 2, right - 2, frame + 2 + 14);
}
