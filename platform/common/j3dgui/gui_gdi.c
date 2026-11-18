/*
 * j3dgui GDI (PORT-PLAN.md P6.5): device contexts, bitmaps, fonts, brushes and pens, blits and text, enough for the
 * game's dialog code (owner-drawn icons and thumbnails, the trading post's images, the statistics pictures), the
 * savegame thumbnail (sithGamesave.c) and stdBmp_Load.
 *
 * A DC draws into a target: a window's top-level surface (32-bit, clipped to the window) or the bitmap selected into a
 * memory DC (a DIB section of 1, 8, 16, 24 or 32 bits per pixel, bottom-up or top-down). Pixels move between targets
 * as 0x00RRGGBB.
 *
 * Text is drawn without anti-aliasing unless J3D_GUI_SMOOTH_TEXT is set: Windows 98 drew dialog text that way.
 */
#include "gui_internal.h"
#include "gui_text.h"
#include "j3dgui.h"

#include <std/Win95/stdDisplay.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GUI_OBJ_MAGIC 0x4A474449u // "IDGJ"
#define GUI_DC_MAGIC  0x4A474443u // "CDGJ"

// The DCs and objects that exist. A handle is used only once it is found here, so one the game keeps after deleting it
// (retail's dialogs paint with DCs they have already deleted, and Windows refused those calls) is refused too, instead
// of being read after it was freed. Stock objects are static and never in the table. A few hundred live at most.
static void** gui_apLive;
static size_t gui_numLive;
static size_t gui_maxLive;

static void gui_Track(void* p)
{
    if ( gui_numLive == gui_maxLive )
    {
        size_t newMax = gui_maxLive ? gui_maxLive * 2 : 256;
        void** apNew = realloc(gui_apLive, newMax * sizeof(*apNew));
        if ( !apNew )
        {
            return; // untracked: refused as stale, which only loses that one handle
        }
        gui_apLive = apNew;
        gui_maxLive = newMax;
    }
    gui_apLive[gui_numLive++] = p;
}

static void gui_Untrack(void* p)
{
    size_t i;
    for ( i = gui_numLive; i-- > 0; )
    {
        if ( gui_apLive[i] == p )
        {
            gui_apLive[i] = gui_apLive[--gui_numLive];
            return;
        }
    }
}

static int gui_IsLive(const void* p)
{
    size_t i;
    for ( i = gui_numLive; i-- > 0; ) // newest first: the ones in use are mostly the newest
    {
        if ( gui_apLive[i] == p )
        {
            return 1;
        }
    }
    return 0;
}

typedef enum GuiObjType { GUI_OBJ_BITMAP = 1, GUI_OBJ_FONT, GUI_OBJ_BRUSH, GUI_OBJ_PEN } GuiObjType;

typedef struct GuiObject
{
    uint32_t magic;
    GuiObjType type;
    int bStock;
    // bitmap
    int width;
    int height;         // rows
    int bpp;
    int pitch;          // bytes per row
    int bTopDown;
    uint8_t* pBits;
    int bOwnsBits;
    RGBQUAD aPalette[256];
    // font
    LOGFONTA lf;
    J3DGuiTextFace* pFace;
    // brush, pen
    COLORREF color;
    int bNull;
    int penWidth;
} GuiObject;

typedef struct GuiDC
{
    uint32_t magic;
    J3DGuiWindow* pWindow;  // a window DC, else a memory DC
    POINT origin;           // window DC: the drawing origin in the top-level surface
    RECT clip;              // window DC: the clip in surface coordinates
    GuiObject* pBitmap;     // memory DC
    GuiObject* pFont;
    GuiObject* pBrush;
    GuiObject* pPen;
    COLORREF textColor;
    COLORREF bkColor;
    int bkMode;
    int stretchMode;
    UINT textAlign;
    POINT pen;
} GuiDC;

static GuiObject gui_stockBitmap;   // the 1x1 bitmap a new memory DC holds
static GuiObject gui_aStockBrushes[8];
static GuiObject gui_aStockPens[3];
static GuiObject gui_aSysBrushes[32];
static GuiObject gui_defaultFont;
static GuiObject gui_defaultFontBold;
static int gui_bStockInited = 0;
static int gui_bSmoothText = -1;

/* ---- Surfaces ------------------------------------------------------------------------------------------------------- */

J3DGuiSurface* J3DGui_NewSurface(int width, int height)
{
    J3DGuiSurface* p = calloc(1, sizeof(*p));
    if ( !p )
    {
        return NULL;
    }
    p->pPixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if ( !p->pPixels )
    {
        free(p);
        return NULL;
    }
    p->width = width;
    p->height = height;
    p->pitch = width;
    return p;
}

void J3DGui_FreeSurface(J3DGuiSurface* p)
{
    if ( p )
    {
        free(p->pPixels);
        free(p);
    }
}

/* ---- Colours -------------------------------------------------------------------------------------------------------- */

// Windows 98's "Windows Standard" scheme.
static COLORREF gui_SysColorRef(int index)
{
    switch ( index )
    {
        case COLOR_SCROLLBAR:           return RGB(192, 192, 192);
        case COLOR_BACKGROUND:          return RGB(0, 128, 128);
        case COLOR_ACTIVECAPTION:       return RGB(0, 0, 128);
        case COLOR_INACTIVECAPTION:     return RGB(128, 128, 128);
        case COLOR_MENU:                return RGB(192, 192, 192);
        case COLOR_WINDOW:              return RGB(255, 255, 255);
        case COLOR_WINDOWFRAME:         return RGB(0, 0, 0);
        case COLOR_MENUTEXT:            return RGB(0, 0, 0);
        case COLOR_WINDOWTEXT:          return RGB(0, 0, 0);
        case COLOR_CAPTIONTEXT:         return RGB(255, 255, 255);
        case COLOR_ACTIVEBORDER:        return RGB(192, 192, 192);
        case COLOR_INACTIVEBORDER:      return RGB(192, 192, 192);
        case COLOR_APPWORKSPACE:        return RGB(128, 128, 128);
        case COLOR_HIGHLIGHT:           return RGB(0, 0, 128);
        case COLOR_HIGHLIGHTTEXT:       return RGB(255, 255, 255);
        case COLOR_3DFACE:              return RGB(192, 192, 192);
        case COLOR_3DSHADOW:            return RGB(128, 128, 128);
        case COLOR_GRAYTEXT:            return RGB(128, 128, 128);
        case COLOR_BTNTEXT:             return RGB(0, 0, 0);
        case COLOR_INACTIVECAPTIONTEXT: return RGB(192, 192, 192);
        case COLOR_3DHILIGHT:           return RGB(255, 255, 255);
        case COLOR_3DDKSHADOW:          return RGB(0, 0, 0);
        case COLOR_3DLIGHT:             return RGB(192, 192, 192);
        case COLOR_INFOTEXT:            return RGB(0, 0, 0);
        case COLOR_INFOBK:              return RGB(255, 255, 225);
        case 27:                        return RGB(16, 132, 208);  // COLOR_GRADIENTACTIVECAPTION
        case 28:                        return RGB(192, 192, 192); // COLOR_GRADIENTINACTIVECAPTION
        default:                        return RGB(192, 192, 192);
    }
}

uint32_t J3DGui_SysColor(int index)
{
    return GUI_RGB32(gui_SysColorRef(index));
}

DWORD WINAPI GetSysColor(int index)
{
    return gui_SysColorRef(index);
}

static void gui_InitStock(void)
{
    static const COLORREF aBrushColors[] = { RGB(255, 255, 255), RGB(192, 192, 192), RGB(128, 128, 128), RGB(64, 64, 64), RGB(0, 0, 0), 0 };
    int i;
    if ( gui_bStockInited )
    {
        return;
    }
    gui_bStockInited = 1;
    gui_stockBitmap.magic = GUI_OBJ_MAGIC;
    gui_stockBitmap.type = GUI_OBJ_BITMAP;
    gui_stockBitmap.bStock = 1;
    gui_stockBitmap.width = 1;
    gui_stockBitmap.height = 1;
    gui_stockBitmap.bpp = 1;
    gui_stockBitmap.pitch = 4;
    gui_stockBitmap.pBits = calloc(1, 4);
    for ( i = 0; i < 6; ++i )
    {
        gui_aStockBrushes[i].magic = GUI_OBJ_MAGIC;
        gui_aStockBrushes[i].type = GUI_OBJ_BRUSH;
        gui_aStockBrushes[i].bStock = 1;
        gui_aStockBrushes[i].color = aBrushColors[i];
        gui_aStockBrushes[i].bNull = i == 5; // NULL_BRUSH
    }
    for ( i = 0; i < 3; ++i )
    {
        gui_aStockPens[i].magic = GUI_OBJ_MAGIC;
        gui_aStockPens[i].type = GUI_OBJ_PEN;
        gui_aStockPens[i].bStock = 1;
        gui_aStockPens[i].color = i == 0 ? RGB(255, 255, 255) : RGB(0, 0, 0);
        gui_aStockPens[i].bNull = i == 2; // NULL_PEN
        gui_aStockPens[i].penWidth = 1;
    }
    for ( i = 0; i < 32; ++i )
    {
        gui_aSysBrushes[i].magic = GUI_OBJ_MAGIC;
        gui_aSysBrushes[i].type = GUI_OBJ_BRUSH;
        gui_aSysBrushes[i].bStock = 1;
        gui_aSysBrushes[i].color = gui_SysColorRef(i);
    }
    gui_defaultFont.magic = GUI_OBJ_MAGIC;
    gui_defaultFont.type = GUI_OBJ_FONT;
    gui_defaultFont.bStock = 1;
    gui_defaultFont.lf.lfHeight = -11; // 8 points at 96 dpi
    gui_defaultFont.lf.lfWeight = FW_NORMAL;
    strcpy(gui_defaultFont.lf.lfFaceName, "MS Sans Serif");
    gui_defaultFontBold = gui_defaultFont;
    gui_defaultFontBold.lf.lfWeight = FW_BOLD;
}

static int gui_IsStock(const GuiObject* p)
{
    return p == &gui_stockBitmap || p == &gui_defaultFont
           || (p >= gui_aStockBrushes && p < gui_aStockBrushes + sizeof(gui_aStockBrushes) / sizeof(gui_aStockBrushes[0]))
           || (p >= gui_aStockPens && p < gui_aStockPens + sizeof(gui_aStockPens) / sizeof(gui_aStockPens[0]))
           || (p >= gui_aSysBrushes && p < gui_aSysBrushes + sizeof(gui_aSysBrushes) / sizeof(gui_aSysBrushes[0]));
}

static GuiObject* gui_Obj(HGDIOBJ h, GuiObjType type)
{
    GuiObject* p = (GuiObject*)h;
    if ( !p || (!gui_IsStock(p) && !gui_IsLive(p)) || p->magic != GUI_OBJ_MAGIC || (type && p->type != type) )
    {
        return NULL;
    }
    return p;
}

static GuiDC* gui_DC(HDC hdc)
{
    GuiDC* p = (GuiDC*)hdc;
    return p && gui_IsLive(p) && p->magic == GUI_DC_MAGIC ? p : NULL;
}

HBRUSH WINAPI GetSysColorBrush(int index)
{
    gui_InitStock();
    return (HBRUSH)&gui_aSysBrushes[index & 31];
}

HGDIOBJ WINAPI GetStockObject(int index)
{
    gui_InitStock();
    switch ( index )
    {
        case WHITE_BRUSH:     return &gui_aStockBrushes[0];
        case LTGRAY_BRUSH:    return &gui_aStockBrushes[1];
        case GRAY_BRUSH:      return &gui_aStockBrushes[2];
        case DKGRAY_BRUSH:    return &gui_aStockBrushes[3];
        case BLACK_BRUSH:     return &gui_aStockBrushes[4];
        case NULL_BRUSH:      return &gui_aStockBrushes[5];
        case WHITE_PEN:       return &gui_aStockPens[0];
        case BLACK_PEN:       return &gui_aStockPens[1];
        case NULL_PEN:        return &gui_aStockPens[2];
        case SYSTEM_FONT:
        case DEFAULT_GUI_FONT:
        case ANSI_VAR_FONT:
            return &gui_defaultFont;
        default:
            return NULL;
    }
}

HFONT J3DGui_DefaultFont(void)
{
    gui_InitStock();
    return (HFONT)&gui_defaultFont;
}

HFONT J3DGui_CaptionFont(void)
{
    gui_InitStock();
    return (HFONT)&gui_defaultFontBold;
}

HFONT J3DGui_GetFont(J3DGuiWindow* pWindow)
{
    return pWindow && gui_Obj(pWindow->hFont, GUI_OBJ_FONT) ? pWindow->hFont : J3DGui_DefaultFont();
}

/* ---- Objects -------------------------------------------------------------------------------------------------------- */

HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
    GuiObject* p = calloc(1, sizeof(*p));
    if ( p )
    {
        gui_Track(p);
        p->magic = GUI_OBJ_MAGIC;
        p->type = GUI_OBJ_BRUSH;
        p->color = color;
    }
    return (HBRUSH)p;
}

HPEN WINAPI CreatePen(int style, int width, COLORREF color)
{
    GuiObject* p = calloc(1, sizeof(*p));
    if ( p )
    {
        gui_Track(p);
        p->magic = GUI_OBJ_MAGIC;
        p->type = GUI_OBJ_PEN;
        p->color = color;
        p->bNull = style == PS_NULL;
        p->penWidth = width < 1 ? 1 : width;
    }
    return (HPEN)p;
}

HFONT WINAPI CreateFontIndirectA(const LOGFONTA* pLogFont)
{
    GuiObject* p;
    if ( !pLogFont )
    {
        return NULL;
    }
    p = calloc(1, sizeof(*p));
    if ( p )
    {
        gui_Track(p);
        p->magic = GUI_OBJ_MAGIC;
        p->type = GUI_OBJ_FONT;
        p->lf = *pLogFont;
    }
    return (HFONT)p;
}

HFONT WINAPI CreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strikeOut,
                         DWORD charSet, DWORD outPrecision, DWORD clipPrecision, DWORD quality, DWORD pitchAndFamily, LPCSTR pFace)
{
    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = height;
    lf.lfWidth = width;
    lf.lfEscapement = escapement;
    lf.lfOrientation = orientation;
    lf.lfWeight = weight;
    lf.lfItalic = (BYTE)italic;
    lf.lfUnderline = (BYTE)underline;
    lf.lfStrikeOut = (BYTE)strikeOut;
    lf.lfCharSet = (BYTE)charSet;
    lf.lfOutPrecision = (BYTE)outPrecision;
    lf.lfClipPrecision = (BYTE)clipPrecision;
    lf.lfQuality = (BYTE)quality;
    lf.lfPitchAndFamily = (BYTE)pitchAndFamily;
    if ( pFace )
    {
        strncpy(lf.lfFaceName, pFace, sizeof(lf.lfFaceName) - 1);
    }
    return CreateFontIndirectA(&lf);
}

static J3DGuiTextFace* gui_Face(GuiObject* pFont)
{
    if ( !pFont )
    {
        gui_InitStock();
        pFont = &gui_defaultFont;
    }
    if ( !pFont->pFace )
    {
        pFont->pFace = J3DGuiText_Open(pFont->lf.lfFaceName[0] ? pFont->lf.lfFaceName : "MS Sans Serif", pFont->lf.lfHeight,
                                       pFont->lf.lfWeight, pFont->lf.lfItalic, pFont->lf.lfUnderline);
    }
    return pFont->pFace;
}

static int gui_BitmapPitch(int width, int bpp)
{
    return ((width * bpp + 31) / 32) * 4;
}

HBITMAP WINAPI CreateDIBSection(HDC hdc, const BITMAPINFO* pInfo, UINT usage, void** ppBits, HANDLE hSection, DWORD offset)
{
    GuiObject* p;
    int height;
    int colors;
    (void)hdc;
    (void)usage;
    (void)hSection;
    (void)offset;

    if ( !pInfo )
    {
        return NULL;
    }
    height = pInfo->bmiHeader.biHeight < 0 ? -pInfo->bmiHeader.biHeight : pInfo->bmiHeader.biHeight;
    if ( pInfo->bmiHeader.biWidth <= 0 || height <= 0 )
    {
        return NULL;
    }
    p = calloc(1, sizeof(*p));
    if ( !p )
    {
        return NULL;
    }
    p->magic = GUI_OBJ_MAGIC;
    p->type = GUI_OBJ_BITMAP;
    p->width = pInfo->bmiHeader.biWidth;
    p->height = height;
    p->bpp = pInfo->bmiHeader.biBitCount;
    p->bTopDown = pInfo->bmiHeader.biHeight < 0;
    p->pitch = gui_BitmapPitch(p->width, p->bpp);
    p->pBits = calloc((size_t)p->pitch, (size_t)height);
    p->bOwnsBits = 1;
    if ( !p->pBits )
    {
        free(p);
        return NULL;
    }
    gui_Track(p);
    if ( p->bpp <= 8 )
    {
        colors = pInfo->bmiHeader.biClrUsed ? (int)pInfo->bmiHeader.biClrUsed : 1 << p->bpp;
        memcpy(p->aPalette, pInfo->bmiColors, (size_t)GUI_MIN(colors, 256) * sizeof(RGBQUAD));
    }
    if ( ppBits )
    {
        *ppBits = p->pBits;
    }
    return (HBITMAP)p;
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int width, int height)
{
    BITMAPINFO bmi;
    (void)hdc;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width < 1 ? 1 : width;
    bmi.bmiHeader.biHeight = -(height < 1 ? 1 : height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    return CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
}

HBITMAP WINAPI CreateBitmap(int width, int height, UINT planes, UINT bpp, const void* pBits)
{
    BITMAPINFO bmi;
    HBITMAP h;
    GuiObject* p;
    (void)planes;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // device-dependent bitmaps are top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = (WORD)bpp;
    if ( bpp == 1 )
    {
        bmi.bmiColors[1].rgbRed = bmi.bmiColors[1].rgbGreen = bmi.bmiColors[1].rgbBlue = 255;
    }
    h = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
    p = gui_Obj(h, GUI_OBJ_BITMAP);
    if ( p && pBits )
    {
        // a DDB's rows are WORD-aligned
        int srcPitch = ((width * (int)bpp + 15) / 16) * 2;
        int y;
        for ( y = 0; y < height; ++y )
        {
            memcpy(p->pBits + (size_t)y * p->pitch, (const uint8_t*)pBits + (size_t)y * srcPitch, (size_t)GUI_MIN(srcPitch, p->pitch));
        }
    }
    return h;
}

int WINAPI GetObjectA(HANDLE h, int size, LPVOID pOut)
{
    GuiObject* p = gui_Obj(h, 0);
    if ( !p || !pOut )
    {
        return 0;
    }
    if ( p->type == GUI_OBJ_BITMAP && size >= (int)sizeof(BITMAP) )
    {
        BITMAP* pBitmap = pOut;
        memset(pBitmap, 0, sizeof(*pBitmap));
        pBitmap->bmWidth = p->width;
        pBitmap->bmHeight = p->height;
        pBitmap->bmWidthBytes = p->pitch;
        pBitmap->bmPlanes = 1;
        pBitmap->bmBitsPixel = (WORD)p->bpp;
        pBitmap->bmBits = p->pBits;
        if ( size >= (int)sizeof(DIBSECTION) )
        {
            // As Windows: a DIB section's header too, its height negative when the rows run top-down
            DIBSECTION* pDib = pOut;
            memset(&pDib->dsBmih, 0, sizeof(*pDib) - sizeof(BITMAP));
            pDib->dsBmih.biSize = sizeof(BITMAPINFOHEADER);
            pDib->dsBmih.biWidth = p->width;
            pDib->dsBmih.biHeight = p->bTopDown ? -p->height : p->height;
            pDib->dsBmih.biPlanes = 1;
            pDib->dsBmih.biBitCount = (WORD)p->bpp;
            pDib->dsBmih.biCompression = BI_RGB;
            pDib->dsBmih.biSizeImage = (DWORD)p->pitch * (DWORD)p->height;
            return (int)sizeof(DIBSECTION);
        }
        return (int)sizeof(BITMAP);
    }
    if ( p->type == GUI_OBJ_FONT && size >= (int)sizeof(LOGFONTA) )
    {
        *(LOGFONTA*)pOut = p->lf;
        return (int)sizeof(LOGFONTA);
    }
    return 0;
}

BOOL WINAPI DeleteObject(HGDIOBJ h)
{
    GuiObject* p = gui_Obj(h, 0);
    if ( !p || p->bStock )
    {
        return p != NULL;
    }
    if ( p->type == GUI_OBJ_FONT && p->pFace )
    {
        J3DGuiText_Close(p->pFace);
    }
    if ( p->bOwnsBits )
    {
        free(p->pBits);
    }
    gui_Untrack(p);
    p->magic = 0;
    free(p);
    return TRUE;
}

BOOL WINAPI GetBitmapDimensionEx(HBITMAP hBitmap, LPSIZE pSize)
{
    GuiObject* p = gui_Obj(hBitmap, GUI_OBJ_BITMAP);
    if ( !p || !pSize )
    {
        return FALSE;
    }
    pSize->cx = 0; // GDI returns what SetBitmapDimensionEx set, which nothing does
    pSize->cy = 0;
    return TRUE;
}

/* ---- DCs ------------------------------------------------------------------------------------------------------------ */

static GuiDC* gui_NewDC(void)
{
    GuiDC* p = calloc(1, sizeof(*p));
    if ( !p )
    {
        return NULL;
    }
    gui_InitStock();
    gui_Track(p);
    p->magic = GUI_DC_MAGIC;
    p->pBitmap = &gui_stockBitmap;
    p->pFont = &gui_defaultFont;
    p->pBrush = &gui_aStockBrushes[0];
    p->pPen = &gui_aStockPens[1];
    p->textColor = RGB(0, 0, 0);
    p->bkColor = RGB(255, 255, 255);
    p->bkMode = OPAQUE;
    p->stretchMode = BLACKONWHITE;
    return p;
}

HDC WINAPI CreateCompatibleDC(HDC hdc)
{
    (void)hdc;
    return (HDC)gui_NewDC();
}

HDC WINAPI CreateDCA(LPCSTR pDriver, LPCSTR pDevice, LPCSTR pOutput, const void* pInit)
{
    (void)pDriver;
    (void)pDevice;
    (void)pOutput;
    (void)pInit;
    return (HDC)gui_NewDC();
}

BOOL WINAPI DeleteDC(HDC hdc)
{
    GuiDC* p = gui_DC(hdc);
    if ( !p )
    {
        return FALSE;
    }
    gui_Untrack(p);
    p->magic = 0;
    free(p);
    return TRUE;
}

HDC J3DGui_GetWindowDC(J3DGuiWindow* pWindow, int bWindowDC)
{
    GuiDC* p;
    J3DGuiWindow* pTop;
    POINT origin;
    POINT topOrigin;
    RECT client;
    J3DGuiWindow* q;

    if ( !pWindow )
    {
        return NULL;
    }
    p = gui_NewDC();
    if ( !p )
    {
        return NULL;
    }
    pTop = J3DGui_TopLevel(pWindow);
    p->pWindow = pWindow;
    p->pBitmap = NULL;
    p->pFont = gui_Obj(J3DGui_GetFont(pWindow), GUI_OBJ_FONT);
    p->bkColor = gui_SysColorRef(COLOR_3DFACE);

    J3DGui_WindowOrigin(pTop, &topOrigin);
    if ( bWindowDC )
    {
        J3DGui_WindowOrigin(pWindow, &origin);
        SetRect(&p->clip, 0, 0, pWindow->rect.right - pWindow->rect.left, pWindow->rect.bottom - pWindow->rect.top);
    }
    else
    {
        J3DGui_ClientOrigin(pWindow, &origin);
        J3DGui_GetClientInWindow(pWindow, &client);
        SetRect(&p->clip, 0, 0, client.right - client.left, client.bottom - client.top);
    }
    p->origin.x = origin.x - topOrigin.x;
    p->origin.y = origin.y - topOrigin.y;
    OffsetRect(&p->clip, p->origin.x, p->origin.y);

    // clip to every ancestor's client area
    for ( q = pWindow->pParent; q; q = q->pParent )
    {
        RECT ancestor;
        POINT o;
        J3DGui_ClientOrigin(q, &o);
        J3DGui_GetClientInWindow(q, &client);
        SetRect(&ancestor, o.x - topOrigin.x, o.y - topOrigin.y, o.x - topOrigin.x + client.right - client.left, o.y - topOrigin.y + client.bottom - client.top);
        IntersectRect(&p->clip, &p->clip, &ancestor);
    }
    if ( pTop->pSurface )
    {
        RECT surface = { 0, 0, pTop->pSurface->width, pTop->pSurface->height };
        IntersectRect(&p->clip, &p->clip, &surface);
    }
    return (HDC)p;
}

void J3DGui_ReleaseWindowDC(HDC hdc)
{
    DeleteDC(hdc);
}

HDC WINAPI GetDC(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    if ( !p )
    {
        return (HDC)gui_NewDC(); // the screen or the game's window: a DC that draws nowhere
    }
    return J3DGui_GetWindowDC(p, 0);
}

HDC WINAPI GetWindowDC(HWND hwnd)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    return p ? J3DGui_GetWindowDC(p, 1) : (HDC)gui_NewDC();
}

int WINAPI ReleaseDC(HWND hwnd, HDC hdc)
{
    (void)hwnd;
    return DeleteDC(hdc);
}

HDC WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT pPaint)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hwnd);
    HDC hdc = p ? J3DGui_GetWindowDC(p, 0) : NULL;
    if ( pPaint )
    {
        memset(pPaint, 0, sizeof(*pPaint));
        pPaint->hdc = hdc;
        if ( p )
        {
            GetClientRect(hwnd, &pPaint->rcPaint);
        }
    }
    return hdc;
}

BOOL WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT* pPaint)
{
    (void)hwnd;
    if ( pPaint && pPaint->hdc )
    {
        DeleteDC(pPaint->hdc);
    }
    return TRUE;
}

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h)
{
    GuiDC* p = gui_DC(hdc);
    GuiObject* pObj = gui_Obj(h, 0);
    HGDIOBJ hOld = NULL;
    if ( !p || !pObj )
    {
        return NULL;
    }
    switch ( pObj->type )
    {
        case GUI_OBJ_BITMAP:
            if ( p->pWindow )
            {
                return NULL;
            }
            hOld = p->pBitmap;
            p->pBitmap = pObj;
            break;
        case GUI_OBJ_FONT:
            hOld = p->pFont;
            p->pFont = pObj;
            break;
        case GUI_OBJ_BRUSH:
            hOld = p->pBrush;
            p->pBrush = pObj;
            break;
        case GUI_OBJ_PEN:
            hOld = p->pPen;
            p->pPen = pObj;
            break;
    }
    return hOld;
}

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color)
{
    GuiDC* p = gui_DC(hdc);
    COLORREF old = p ? p->textColor : CLR_INVALID;
    if ( p )
    {
        p->textColor = color;
    }
    return old;
}

COLORREF WINAPI GetTextColor(HDC hdc)
{
    GuiDC* p = gui_DC(hdc);
    return p ? p->textColor : CLR_INVALID;
}

COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color)
{
    GuiDC* p = gui_DC(hdc);
    COLORREF old = p ? p->bkColor : CLR_INVALID;
    if ( p )
    {
        p->bkColor = color;
    }
    return old;
}

COLORREF WINAPI GetBkColor(HDC hdc)
{
    GuiDC* p = gui_DC(hdc);
    return p ? p->bkColor : CLR_INVALID;
}

int WINAPI SetBkMode(HDC hdc, int mode)
{
    GuiDC* p = gui_DC(hdc);
    int old = p ? p->bkMode : 0;
    if ( p )
    {
        p->bkMode = mode;
    }
    return old;
}

int WINAPI SetStretchBltMode(HDC hdc, int mode)
{
    GuiDC* p = gui_DC(hdc);
    int old = p ? p->stretchMode : 0;
    if ( p )
    {
        p->stretchMode = mode;
    }
    return old;
}

UINT WINAPI SetTextAlign(HDC hdc, UINT align)
{
    GuiDC* p = gui_DC(hdc);
    UINT old = p ? p->textAlign : GDI_ERROR;
    if ( p )
    {
        p->textAlign = align;
    }
    return old;
}

int WINAPI GetDeviceCaps(HDC hdc, int index)
{
    int w;
    int h;
    (void)hdc;
    J3DGuiHost_GetScreenSize(&w, &h);
    switch ( index )
    {
        case LOGPIXELSX:
        case LOGPIXELSY:  return 96;
        case BITSPIXEL:   return 32;
        case PLANES:      return 1;
        case HORZRES:     return w;
        case VERTRES:     return h;
        case NUMCOLORS:   return -1;
        case RASTERCAPS:  return RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP;
        case HORZSIZE:    return w * 254 / 960;
        case VERTSIZE:    return h * 254 / 960;
        default:          return 0;
    }
}

BOOL WINAPI GdiFlush(void)
{
    return TRUE;
}

/* ---- Pixel targets -------------------------------------------------------------------------------------------------- */

// Where a DC's pixels are and how to read and write them.
typedef struct GuiTarget
{
    uint8_t* pBits;
    int width;
    int height;
    int pitch;          // bytes
    int bpp;
    int bTopDown;
    RECT clip;          // in target coordinates
    POINT origin;       // logical (0,0) in target coordinates
    const RGBQUAD* pPalette;
} GuiTarget;

static int gui_Target(GuiDC* p, GuiTarget* t)
{
    memset(t, 0, sizeof(*t));
    if ( p->pWindow )
    {
        J3DGuiWindow* pTop = J3DGui_TopLevel(p->pWindow);
        if ( !pTop || !pTop->pSurface )
        {
            return 0;
        }
        t->pBits = (uint8_t*)pTop->pSurface->pPixels;
        t->width = pTop->pSurface->width;
        t->height = pTop->pSurface->height;
        t->pitch = pTop->pSurface->pitch * 4;
        t->bpp = 32;
        t->bTopDown = 1;
        t->clip = p->clip;
        t->origin = p->origin;
        return 1;
    }
    if ( !p->pBitmap || !p->pBitmap->pBits )
    {
        return 0;
    }
    t->pBits = p->pBitmap->pBits;
    t->width = p->pBitmap->width;
    t->height = p->pBitmap->height;
    t->pitch = p->pBitmap->pitch;
    t->bpp = p->pBitmap->bpp;
    t->bTopDown = p->pBitmap->bTopDown;
    t->pPalette = p->pBitmap->aPalette;
    SetRect(&t->clip, 0, 0, t->width, t->height);
    return 1;
}

static uint8_t* gui_Row(const GuiTarget* t, int y)
{
    return t->pBits + (size_t)(t->bTopDown ? y : t->height - 1 - y) * (size_t)t->pitch;
}

// 0x00RRGGBB
static uint32_t gui_GetPixel(const GuiTarget* t, int x, int y)
{
    const uint8_t* row = gui_Row(t, y);
    switch ( t->bpp )
    {
        case 32:
        {
            uint32_t v;
            memcpy(&v, row + 4 * x, 4);
            return v & 0xFFFFFF;
        }
        case 24:
            return (uint32_t)row[3 * x] | ((uint32_t)row[3 * x + 1] << 8) | ((uint32_t)row[3 * x + 2] << 16);
        case 16:
        {
            uint32_t v = (uint32_t)(row[2 * x] | (row[2 * x + 1] << 8));
            uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        }
        case 8:
        {
            const RGBQUAD* q = &t->pPalette[row[x]];
            return ((uint32_t)q->rgbRed << 16) | ((uint32_t)q->rgbGreen << 8) | q->rgbBlue;
        }
        case 4:
        {
            int index = (x & 1) ? row[x / 2] & 15 : row[x / 2] >> 4;
            const RGBQUAD* q = &t->pPalette[index];
            return ((uint32_t)q->rgbRed << 16) | ((uint32_t)q->rgbGreen << 8) | q->rgbBlue;
        }
        case 1:
        {
            int bit = (row[x / 8] >> (7 - (x & 7))) & 1;
            const RGBQUAD* q = &t->pPalette[bit];
            return ((uint32_t)q->rgbRed << 16) | ((uint32_t)q->rgbGreen << 8) | q->rgbBlue;
        }
        default:
            return 0;
    }
}

static int gui_NearestIndex(const RGBQUAD* pPalette, int count, uint32_t rgb)
{
    int best = 0;
    int bestDist = 0x7FFFFFFF;
    int i;
    for ( i = 0; i < count; ++i )
    {
        int dr = (int)pPalette[i].rgbRed - (int)((rgb >> 16) & 255);
        int dg = (int)pPalette[i].rgbGreen - (int)((rgb >> 8) & 255);
        int db = (int)pPalette[i].rgbBlue - (int)(rgb & 255);
        int d = dr * dr + dg * dg + db * db;
        if ( d < bestDist )
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

static void gui_PutPixel(const GuiTarget* t, int x, int y, uint32_t rgb)
{
    uint8_t* row;
    if ( x < t->clip.left || x >= t->clip.right || y < t->clip.top || y >= t->clip.bottom )
    {
        return;
    }
    row = gui_Row(t, y);
    switch ( t->bpp )
    {
        case 32:
            memcpy(row + 4 * x, &rgb, 4);
            break;
        case 24:
            row[3 * x] = (uint8_t)rgb;
            row[3 * x + 1] = (uint8_t)(rgb >> 8);
            row[3 * x + 2] = (uint8_t)(rgb >> 16);
            break;
        case 16:
        {
            uint16_t v = (uint16_t)((((rgb >> 19) & 31) << 10) | (((rgb >> 11) & 31) << 5) | ((rgb >> 3) & 31));
            row[2 * x] = (uint8_t)v;
            row[2 * x + 1] = (uint8_t)(v >> 8);
            break;
        }
        case 8:
            row[x] = (uint8_t)gui_NearestIndex(t->pPalette, 256, rgb);
            break;
        case 1:
        {
            int bit = gui_NearestIndex(t->pPalette, 2, rgb);
            if ( bit )
            {
                row[x / 8] |= (uint8_t)(0x80 >> (x & 7));
            }
            else
            {
                row[x / 8] &= (uint8_t)~(0x80 >> (x & 7));
            }
            break;
        }
        default:
            break;
    }
}

static uint32_t gui_ColorRefToRGB(COLORREF cr)
{
    return GUI_RGB32(cr);
}

/* ---- Filling and lines ---------------------------------------------------------------------------------------------- */

static void gui_FillTarget(const GuiTarget* t, const RECT* pLogical, uint32_t rgb)
{
    RECT r = *pLogical;
    int x;
    int y;
    OffsetRect(&r, t->origin.x, t->origin.y);
    IntersectRect(&r, &r, &t->clip);
    for ( y = r.top; y < r.bottom; ++y )
    {
        if ( t->bpp == 32 )
        {
            uint32_t* row = (uint32_t*)gui_Row(t, y);
            for ( x = r.left; x < r.right; ++x )
            {
                row[x] = rgb;
            }
        }
        else
        {
            for ( x = r.left; x < r.right; ++x )
            {
                gui_PutPixel(t, x, y, rgb);
            }
        }
    }
}

void J3DGui_FillRect32(HDC hdc, const RECT* pRect, uint32_t color)
{
    GuiDC* p = gui_DC(hdc);
    GuiTarget t;
    if ( p && pRect && gui_Target(p, &t) )
    {
        gui_FillTarget(&t, pRect, color);
    }
}

int WINAPI FillRect(HDC hdc, const RECT* pRect, HBRUSH hBrush)
{
    GuiObject* pBrush;
    gui_InitStock();
    // (HBRUSH)(COLOR_x + 1), the class-background idiom
    if ( (uintptr_t)hBrush > 0 && (uintptr_t)hBrush <= 32 )
    {
        pBrush = &gui_aSysBrushes[(int)(uintptr_t)hBrush - 1];
    }
    else
    {
        pBrush = gui_Obj(hBrush, GUI_OBJ_BRUSH);
    }
    if ( !pBrush || pBrush->bNull )
    {
        return 0;
    }
    J3DGui_FillRect32(hdc, pRect, gui_ColorRefToRGB(pBrush->color));
    return 1;
}

int WINAPI FrameRect(HDC hdc, const RECT* pRect, HBRUSH hBrush)
{
    GuiObject* pBrush = gui_Obj(hBrush, GUI_OBJ_BRUSH);
    RECT r;
    uint32_t c;
    if ( !pBrush || !pRect || IsRectEmpty(pRect) )
    {
        return 0;
    }
    c = gui_ColorRefToRGB(pBrush->color);
    SetRect(&r, pRect->left, pRect->top, pRect->right, pRect->top + 1);
    J3DGui_FillRect32(hdc, &r, c);
    SetRect(&r, pRect->left, pRect->bottom - 1, pRect->right, pRect->bottom);
    J3DGui_FillRect32(hdc, &r, c);
    SetRect(&r, pRect->left, pRect->top, pRect->left + 1, pRect->bottom);
    J3DGui_FillRect32(hdc, &r, c);
    SetRect(&r, pRect->right - 1, pRect->top, pRect->right, pRect->bottom);
    J3DGui_FillRect32(hdc, &r, c);
    return 1;
}

BOOL WINAPI Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    GuiDC* p = gui_DC(hdc);
    RECT r;
    if ( !p )
    {
        return FALSE;
    }
    SetRect(&r, left, top, right, bottom);
    if ( p->pBrush && !p->pBrush->bNull )
    {
        J3DGui_FillRect32(hdc, &r, gui_ColorRefToRGB(p->pBrush->color));
    }
    if ( p->pPen && !p->pPen->bNull )
    {
        HBRUSH h = CreateSolidBrush(p->pPen->color);
        FrameRect(hdc, &r, h);
        DeleteObject(h);
    }
    return TRUE;
}

BOOL WINAPI PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop)
{
    GuiDC* p = gui_DC(hdc);
    RECT r;
    uint32_t c;
    if ( !p )
    {
        return FALSE;
    }
    SetRect(&r, x, y, x + w, y + h);
    switch ( rop )
    {
        case BLACKNESS: c = 0; break;
        case WHITENESS: c = 0xFFFFFF; break;
        default:        c = p->pBrush ? gui_ColorRefToRGB(p->pBrush->color) : 0; break;
    }
    J3DGui_FillRect32(hdc, &r, c);
    return TRUE;
}

BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT pOld)
{
    GuiDC* p = gui_DC(hdc);
    if ( !p )
    {
        return FALSE;
    }
    if ( pOld )
    {
        *pOld = p->pen;
    }
    p->pen.x = x;
    p->pen.y = y;
    return TRUE;
}

BOOL WINAPI LineTo(HDC hdc, int x, int y)
{
    GuiDC* p = gui_DC(hdc);
    GuiTarget t;
    int x0;
    int y0;
    int dx;
    int dy;
    int sx;
    int sy;
    int err;
    uint32_t c;
    if ( !p || !gui_Target(p, &t) )
    {
        return FALSE;
    }
    c = p->pPen ? gui_ColorRefToRGB(p->pPen->color) : 0;
    x0 = p->pen.x;
    y0 = p->pen.y;
    dx = abs(x - x0);
    dy = -abs(y - y0);
    sx = x0 < x ? 1 : -1;
    sy = y0 < y ? 1 : -1;
    err = dx + dy;
    while ( !(x0 == x && y0 == y) ) // GDI leaves out the end point
    {
        if ( !p->pPen || !p->pPen->bNull )
        {
            gui_PutPixel(&t, x0 + t.origin.x, y0 + t.origin.y, c);
        }
        {
            int e2 = 2 * err;
            if ( e2 >= dy )
            {
                err += dy;
                x0 += sx;
            }
            if ( e2 <= dx )
            {
                err += dx;
                y0 += sy;
            }
        }
    }
    p->pen.x = x;
    p->pen.y = y;
    return TRUE;
}

COLORREF WINAPI GetPixel(HDC hdc, int x, int y)
{
    GuiDC* p = gui_DC(hdc);
    GuiTarget t;
    uint32_t v;
    if ( !p || !gui_Target(p, &t) )
    {
        return CLR_INVALID;
    }
    x += t.origin.x;
    y += t.origin.y;
    if ( x < 0 || y < 0 || x >= t.width || y >= t.height )
    {
        return CLR_INVALID;
    }
    v = gui_GetPixel(&t, x, y);
    return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
}

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF color)
{
    GuiDC* p = gui_DC(hdc);
    GuiTarget t;
    if ( !p || !gui_Target(p, &t) )
    {
        return CLR_INVALID;
    }
    gui_PutPixel(&t, x + t.origin.x, y + t.origin.y, gui_ColorRefToRGB(color));
    return color;
}

/* ---- Blits ---------------------------------------------------------------------------------------------------------- */

static uint32_t gui_Rop(DWORD rop, uint32_t src, uint32_t dst, uint32_t pattern)
{
    switch ( rop )
    {
        case SRCCOPY:     return src;
        case SRCAND:      return src & dst;
        case SRCPAINT:    return src | dst;
        case SRCINVERT:   return src ^ dst;
        case NOTSRCCOPY:  return ~src & 0xFFFFFF;
        case SRCERASE:    return src & (~dst & 0xFFFFFF);
        case DSTINVERT:   return ~dst & 0xFFFFFF;
        case BLACKNESS:   return 0;
        case WHITENESS:   return 0xFFFFFF;
        case PATCOPY:     return pattern;
        case MERGECOPY:   return src & pattern;
        default:          return src;
    }
}

BOOL WINAPI StretchBlt(HDC hDst, int xDst, int yDst, int wDst, int hDstH, HDC hSrc, int xSrc, int ySrc, int wSrc, int hSrcH, DWORD rop)
{
    GuiDC* pDst = gui_DC(hDst);
    GuiDC* pSrc = gui_DC(hSrc);
    GuiTarget td;
    GuiTarget ts;
    int bHasSrc = rop != BLACKNESS && rop != WHITENESS && rop != PATCOPY && rop != DSTINVERT;
    uint32_t pattern;
    int x;
    int y;
    int bAverage;

    if ( !pDst || !gui_Target(pDst, &td) || wDst == 0 || hDstH == 0 )
    {
        return FALSE;
    }
    if ( bHasSrc && (!pSrc || !gui_Target(pSrc, &ts) || wSrc == 0 || hSrcH == 0) )
    {
        return FALSE;
    }
    pattern = pDst->pBrush ? gui_ColorRefToRGB(pDst->pBrush->color) : 0;
    // HALFTONE averages the source pixels each destination pixel covers when shrinking; the other modes take one
    bAverage = pDst->stretchMode == HALFTONE && bHasSrc && (abs(wSrc) > abs(wDst) || abs(hSrcH) > abs(hDstH));

    for ( y = 0; y < abs(hDstH); ++y )
    {
        int dy = yDst + (hDstH < 0 ? -y - 1 : y);
        int ty = dy + td.origin.y;
        if ( ty < td.clip.top || ty >= td.clip.bottom )
        {
            continue;
        }
        for ( x = 0; x < abs(wDst); ++x )
        {
            int dx = xDst + (wDst < 0 ? -x - 1 : x);
            int tx = dx + td.origin.x;
            uint32_t src = 0;
            uint32_t dst;
            if ( tx < td.clip.left || tx >= td.clip.right )
            {
                continue;
            }
            if ( bHasSrc )
            {
                int sx0 = (int)((int64_t)x * abs(wSrc) / abs(wDst));
                int sy0 = (int)((int64_t)y * abs(hSrcH) / abs(hDstH));
                if ( bAverage )
                {
                    int sx1 = GUI_MAX(sx0 + 1, (int)((int64_t)(x + 1) * abs(wSrc) / abs(wDst)));
                    int sy1 = GUI_MAX(sy0 + 1, (int)((int64_t)(y + 1) * abs(hSrcH) / abs(hDstH)));
                    uint32_t r = 0, g = 0, b = 0, n = 0;
                    int u;
                    int v;
                    for ( v = sy0; v < sy1; ++v )
                    {
                        for ( u = sx0; u < sx1; ++u )
                        {
                            int sx = xSrc + (wSrc < 0 ? -u - 1 : u) + ts.origin.x;
                            int sy = ySrc + (hSrcH < 0 ? -v - 1 : v) + ts.origin.y;
                            if ( sx >= 0 && sy >= 0 && sx < ts.width && sy < ts.height )
                            {
                                uint32_t c = gui_GetPixel(&ts, sx, sy);
                                r += (c >> 16) & 255;
                                g += (c >> 8) & 255;
                                b += c & 255;
                                n++;
                            }
                        }
                    }
                    src = n ? (((r + n / 2) / n) << 16) | (((g + n / 2) / n) << 8) | ((b + n / 2) / n) : 0;
                }
                else
                {
                    int sx = xSrc + (wSrc < 0 ? -sx0 - 1 : sx0) + ts.origin.x;
                    int sy = ySrc + (hSrcH < 0 ? -sy0 - 1 : sy0) + ts.origin.y;
                    if ( sx < 0 || sy < 0 || sx >= ts.width || sy >= ts.height )
                    {
                        continue;
                    }
                    src = gui_GetPixel(&ts, sx, sy);
                }
            }
            dst = (rop == SRCCOPY || rop == PATCOPY || rop == BLACKNESS || rop == WHITENESS) ? 0 : gui_GetPixel(&td, tx, ty);
            gui_PutPixel(&td, tx, ty, gui_Rop(rop, src, dst, pattern));
        }
    }
    return TRUE;
}

BOOL WINAPI BitBlt(HDC hDst, int x, int y, int w, int h, HDC hSrc, int xSrc, int ySrc, DWORD rop)
{
    return StretchBlt(hDst, x, y, w, h, hSrc, xSrc, ySrc, w, h, rop);
}

int WINAPI StretchDIBits(HDC hdc, int xDst, int yDst, int wDst, int hDst, int xSrc, int ySrc, int wSrc, int hSrc, const void* pBits,
                         const BITMAPINFO* pInfo, UINT usage, DWORD rop)
{
    void* pDibBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(NULL, pInfo, usage, &pDibBits, NULL, 0);
    GuiObject* pBitmap = gui_Obj(hBitmap, GUI_OBJ_BITMAP);
    HDC hMem;
    if ( !pBitmap || !pBits )
    {
        DeleteObject(hBitmap);
        return 0;
    }
    memcpy(pDibBits, pBits, (size_t)pBitmap->pitch * (size_t)pBitmap->height);
    hMem = CreateCompatibleDC(NULL);
    SelectObject(hMem, hBitmap);
    StretchBlt(hdc, xDst, yDst, wDst, hDst, hMem, xSrc, ySrc, wSrc, hSrc, rop);
    DeleteDC(hMem);
    DeleteObject(hBitmap);
    return hDst;
}

int WINAPI SetDIBitsToDevice(HDC hdc, int xDst, int yDst, DWORD w, DWORD h, int xSrc, int ySrc, UINT startScan, UINT lines,
                             const void* pBits, const BITMAPINFO* pInfo, UINT usage)
{
    (void)startScan;
    (void)lines;
    return StretchDIBits(hdc, xDst, yDst, (int)w, (int)h, xSrc, ySrc, (int)w, (int)h, pBits, pInfo, usage, SRCCOPY);
}

/* ---- Text ----------------------------------------------------------------------------------------------------------- */

static int gui_SmoothText(void)
{
    if ( gui_bSmoothText < 0 )
    {
        const char* pValue = getenv("J3D_GUI_SMOOTH_TEXT");
        gui_bSmoothText = pValue && *pValue && *pValue != '0';
    }
    return gui_bSmoothText;
}

static GuiObject* gui_FontObj(HFONT hFont)
{
    GuiObject* p = gui_Obj(hFont, GUI_OBJ_FONT);
    if ( !p )
    {
        gui_InitStock();
        p = &gui_defaultFont;
    }
    return p;
}

void J3DGui_TextExtent(HFONT hFont, const char* pText, int len, SIZE* pSize)
{
    J3DGuiTextFace* pFace = gui_Face(gui_FontObj(hFont));
    J3DGuiTextMetrics m;
    int w = 0;
    memset(&m, 0, sizeof(m)); // read below even if the face could not report its metrics
    int i;
    J3DGuiText_GetMetrics(pFace, &m);
    for ( i = 0; i < len; ++i )
    {
        const J3DGuiGlyph* g = J3DGuiText_GetGlyph(pFace, (unsigned char)pText[i]);
        w += g ? g->advance : m.aveCharWidth;
    }
    pSize->cx = w;
    pSize->cy = m.height;
}

int J3DGui_FontHeight(HFONT hFont)
{
    J3DGuiTextMetrics m;
    J3DGuiText_GetMetrics(gui_Face(gui_FontObj(hFont)), &m);
    return m.height;
}

// Draws text with its cell's top-left at (x, y) in the DC's logical coordinates; returns the advance.
static int gui_DrawString(GuiDC* p, GuiTarget* t, GuiObject* pFont, const char* pText, int len, int x, int y, uint32_t color,
                          int bOpaque, uint32_t bk, int underlineAt)
{
    J3DGuiTextFace* pFace = gui_Face(pFont);
    J3DGuiTextMetrics m;
    int penX = x;
    int i;
    int smooth = gui_SmoothText();
    (void)p;

    J3DGuiText_GetMetrics(pFace, &m);
    if ( bOpaque )
    {
        SIZE size;
        RECT r;
        J3DGui_TextExtent((HFONT)pFont, pText, len, &size);
        SetRect(&r, x, y, x + size.cx, y + m.height);
        gui_FillTarget(t, &r, bk);
    }
    for ( i = 0; i < len; ++i )
    {
        const J3DGuiGlyph* g = J3DGuiText_GetGlyph(pFace, (unsigned char)pText[i]);
        int gx;
        int gy;
        if ( !g )
        {
            penX += m.aveCharWidth;
            continue;
        }
        for ( gy = 0; gy < g->height; ++gy )
        {
            for ( gx = 0; gx < g->width; ++gx )
            {
                int cov = g->pCoverage[gy * g->width + gx];
                int px = penX + g->left + gx + t->origin.x;
                int py = y + m.ascent - g->top + gy + t->origin.y;
                if ( smooth ? cov == 0 : cov < 128 )
                {
                    continue;
                }
                if ( smooth && cov < 255 && px >= t->clip.left && px < t->clip.right && py >= t->clip.top && py < t->clip.bottom )
                {
                    uint32_t under = gui_GetPixel(t, px, py);
                    uint32_t r = (((color >> 16) & 255) * (uint32_t)cov + ((under >> 16) & 255) * (255u - (uint32_t)cov)) / 255;
                    uint32_t gg = (((color >> 8) & 255) * (uint32_t)cov + ((under >> 8) & 255) * (255u - (uint32_t)cov)) / 255;
                    uint32_t b = ((color & 255) * (uint32_t)cov + (under & 255) * (255u - (uint32_t)cov)) / 255;
                    gui_PutPixel(t, px, py, (r << 16) | (gg << 8) | b);
                }
                else
                {
                    gui_PutPixel(t, px, py, color);
                }
            }
        }
        if ( i == underlineAt || pFont->lf.lfUnderline )
        {
            RECT r;
            SetRect(&r, penX, y + m.ascent + 1, penX + g->advance, y + m.ascent + 2);
            gui_FillTarget(t, &r, color);
        }
        penX += g->advance;
    }
    return penX - x;
}

void J3DGui_DrawText32(HDC hdc, HFONT hFont, const char* pText, int len, int x, int y, uint32_t color, int bUnderlineMnemonic)
{
    GuiDC* p = gui_DC(hdc);
    GuiTarget t;
    char aBuf[512];
    int n = 0;
    int underlineAt = -1;
    int i;
    if ( !p || !pText || !gui_Target(p, &t) )
    {
        return;
    }
    if ( len < 0 )
    {
        len = (int)strlen(pText);
    }
    // '&' marks the mnemonic, drawn underlined; "&&" is a literal ampersand
    for ( i = 0; i < len && n < (int)sizeof(aBuf) - 1; ++i )
    {
        if ( bUnderlineMnemonic >= 0 && pText[i] == '&' && i + 1 < len )
        {
            ++i;
            if ( pText[i] != '&' && bUnderlineMnemonic && underlineAt < 0 )
            {
                underlineAt = n;
            }
        }
        aBuf[n++] = pText[i];
    }
    gui_DrawString(p, &t, gui_FontObj(hFont), aBuf, n, x, y, color, 0, 0, underlineAt);
}

BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR pText, int len, LPSIZE pSize)
{
    GuiDC* p = gui_DC(hdc);
    if ( !pSize || !pText )
    {
        return FALSE;
    }
    J3DGui_TextExtent(p ? (HFONT)p->pFont : NULL, pText, len, pSize);
    return TRUE;
}

BOOL WINAPI GetTextExtentPointA(HDC hdc, LPCSTR pText, int len, LPSIZE pSize)
{
    return GetTextExtentPoint32A(hdc, pText, len, pSize);
}

BOOL WINAPI GetTextMetricsA(HDC hdc, LPTEXTMETRICA pTm)
{
    GuiDC* p = gui_DC(hdc);
    GuiObject* pFont = p ? p->pFont : gui_FontObj(NULL);
    J3DGuiTextMetrics m;
    if ( !pTm )
    {
        return FALSE;
    }
    J3DGuiText_GetMetrics(gui_Face(pFont), &m);
    memset(pTm, 0, sizeof(*pTm));
    pTm->tmHeight = m.height;
    pTm->tmAscent = m.ascent;
    pTm->tmDescent = m.descent;
    pTm->tmInternalLeading = m.internalLeading;
    pTm->tmExternalLeading = m.externalLeading;
    pTm->tmAveCharWidth = m.aveCharWidth;
    pTm->tmMaxCharWidth = m.maxCharWidth;
    pTm->tmWeight = pFont->lf.lfWeight ? pFont->lf.lfWeight : FW_NORMAL;
    pTm->tmItalic = pFont->lf.lfItalic;
    pTm->tmUnderlined = pFont->lf.lfUnderline;
    pTm->tmFirstChar = 32;
    pTm->tmLastChar = 255;
    pTm->tmDefaultChar = '?';
    pTm->tmBreakChar = ' ';
    pTm->tmPitchAndFamily = TMPF_FIXED_PITCH | TMPF_TRUETYPE; // TMPF_FIXED_PITCH set means a variable-pitch font
    pTm->tmCharSet = ANSI_CHARSET;
    pTm->tmDigitizedAspectX = 96;
    pTm->tmDigitizedAspectY = 96;
    return TRUE;
}

BOOL WINAPI GetCharWidthA(HDC hdc, UINT first, UINT last, LPINT pWidths)
{
    GuiDC* p = gui_DC(hdc);
    J3DGuiTextFace* pFace = gui_Face(p ? p->pFont : NULL);
    UINT c;
    if ( !pWidths )
    {
        return FALSE;
    }
    for ( c = first; c <= last; ++c )
    {
        const J3DGuiGlyph* g = J3DGuiText_GetGlyph(pFace, (unsigned char)c);
        pWidths[c - first] = g ? g->advance : 0;
    }
    return TRUE;
}

BOOL WINAPI GetCharABCWidthsA(HDC hdc, UINT first, UINT last, LPABC pAbc)
{
    GuiDC* p = gui_DC(hdc);
    J3DGuiTextFace* pFace = gui_Face(p ? p->pFont : NULL);
    UINT c;
    if ( !pAbc )
    {
        return FALSE;
    }
    for ( c = first; c <= last; ++c )
    {
        const J3DGuiGlyph* g = J3DGuiText_GetGlyph(pFace, (unsigned char)c);
        ABC* a = &pAbc[c - first];
        if ( g )
        {
            a->abcA = g->left;
            a->abcB = (UINT)GUI_MAX(g->width, 1);
            a->abcC = g->advance - g->left - (int)a->abcB;
        }
        else
        {
            a->abcA = 0;
            a->abcB = 1;
            a->abcC = 0;
        }
    }
    return TRUE;
}

BOOL WINAPI ExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT* pRect, LPCSTR pText, UINT len, const INT* pDx)
{
    GuiDC* p = gui_DC(hdc);
    GuiTarget t;
    J3DGuiTextMetrics m;
    SIZE size;
    (void)pDx;
    if ( !p || !gui_Target(p, &t) )
    {
        return FALSE;
    }
    if ( (options & ETO_OPAQUE) && pRect )
    {
        gui_FillTarget(&t, pRect, gui_ColorRefToRGB(p->bkColor));
    }
    if ( !pText || !len )
    {
        return TRUE;
    }
    J3DGuiText_GetMetrics(gui_Face(p->pFont), &m);
    J3DGui_TextExtent((HFONT)p->pFont, pText, (int)len, &size);
    if ( (p->textAlign & 6) == TA_CENTER )
    {
        x -= size.cx / 2;
    }
    else if ( (p->textAlign & 6) == TA_RIGHT )
    {
        x -= size.cx;
    }
    if ( (p->textAlign & 24) == TA_BASELINE )
    {
        y -= m.ascent;
    }
    else if ( (p->textAlign & 24) == TA_BOTTOM )
    {
        y -= m.height;
    }
    if ( options & ETO_CLIPPED && pRect )
    {
        RECT clip = *pRect;
        OffsetRect(&clip, t.origin.x, t.origin.y);
        IntersectRect(&t.clip, &t.clip, &clip);
    }
    gui_DrawString(p, &t, p->pFont, pText, (int)len, x, y, gui_ColorRefToRGB(p->textColor), p->bkMode == OPAQUE && !(options & ETO_OPAQUE),
                   gui_ColorRefToRGB(p->bkColor), -1);
    return TRUE;
}

BOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR pText, int len)
{
    return ExtTextOutA(hdc, x, y, 0, NULL, pText, (UINT)len, NULL);
}

// The words of a line that fit in maxWidth: returns how many characters, and the line's width.
static int gui_FitLine(HFONT hFont, const char* pText, int len, int maxWidth, int* pWidth)
{
    int lastBreak = -1;
    int i;
    SIZE size;
    for ( i = 0; i < len; ++i )
    {
        if ( pText[i] == '\n' )
        {
            break;
        }
        J3DGui_TextExtent(hFont, pText, i + 1, &size);
        if ( size.cx > maxWidth && i > 0 )
        {
            if ( lastBreak > 0 )
            {
                i = lastBreak;
            }
            break;
        }
        if ( pText[i] == ' ' )
        {
            lastBreak = i;
        }
    }
    J3DGui_TextExtent(hFont, pText, i, &size);
    *pWidth = size.cx;
    return i;
}

int WINAPI DrawTextA(HDC hdc, LPCSTR pText, int len, LPRECT pRect, UINT format)
{
    GuiDC* p = gui_DC(hdc);
    HFONT hFont = p ? (HFONT)p->pFont : J3DGui_DefaultFont();
    int lineHeight = J3DGui_FontHeight(hFont);
    int maxWidth;
    int y;
    int total;
    int pos;
    const char* s;
    char aClean[1024];
    int n = 0;
    int underline = -1;
    int i;

    if ( !pText || !pRect )
    {
        return 0;
    }
    if ( len < 0 )
    {
        len = (int)strlen(pText);
    }
    // the prefix '&' (DT_NOPREFIX keeps it)
    for ( i = 0; i < len && n < (int)sizeof(aClean) - 1; ++i )
    {
        if ( !(format & DT_NOPREFIX) && pText[i] == '&' && i + 1 < len )
        {
            ++i;
            if ( pText[i] != '&' && underline < 0 )
            {
                underline = n;
            }
        }
        aClean[n++] = pText[i];
    }
    aClean[n] = 0;
    s = aClean;
    len = n;
    maxWidth = pRect->right - pRect->left;

    // measure: lines
    total = 0;
    {
        int widest = 0;
        pos = 0;
        while ( pos < len || (pos == 0 && len == 0) )
        {
            int w;
            int count;
            if ( format & DT_SINGLELINE || !(format & DT_WORDBREAK) )
            {
                count = len - pos;
                if ( !(format & DT_SINGLELINE) )
                {
                    const char* nl = memchr(s + pos, '\n', (size_t)(len - pos));
                    if ( nl )
                    {
                        count = (int)(nl - (s + pos));
                    }
                }
                SIZE size;
                J3DGui_TextExtent(hFont, s + pos, count, &size);
                w = size.cx;
            }
            else
            {
                count = gui_FitLine(hFont, s + pos, len - pos, maxWidth, &w);
            }
            widest = GUI_MAX(widest, w);
            total += lineHeight;
            pos += count;
            while ( pos < len && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r') )
            {
                if ( s[pos] == '\n' && (format & DT_SINGLELINE) )
                {
                    break;
                }
                pos++;
                if ( s[pos - 1] == '\n' )
                {
                    break;
                }
            }
            if ( (format & DT_SINGLELINE) || len == 0 )
            {
                break;
            }
        }
        if ( format & DT_CALCRECT )
        {
            pRect->right = pRect->left + widest;
            pRect->bottom = pRect->top + total;
            return total;
        }
    }

    if ( !p )
    {
        return total;
    }
    y = pRect->top;
    if ( format & DT_SINGLELINE )
    {
        if ( format & DT_VCENTER )
        {
            y = pRect->top + (pRect->bottom - pRect->top - lineHeight) / 2;
        }
        else if ( format & DT_BOTTOM )
        {
            y = pRect->bottom - lineHeight;
        }
    }
    pos = 0;
    while ( pos < len )
    {
        int w;
        int count;
        int x;
        if ( format & DT_SINGLELINE || !(format & DT_WORDBREAK) )
        {
            SIZE size;
            count = len - pos;
            if ( !(format & DT_SINGLELINE) )
            {
                const char* nl = memchr(s + pos, '\n', (size_t)(len - pos));
                if ( nl )
                {
                    count = (int)(nl - (s + pos));
                }
            }
            J3DGui_TextExtent(hFont, s + pos, count, &size);
            w = size.cx;
        }
        else
        {
            count = gui_FitLine(hFont, s + pos, len - pos, maxWidth, &w);
        }
        x = pRect->left;
        if ( format & DT_CENTER )
        {
            x = pRect->left + (maxWidth - w) / 2;
        }
        else if ( format & DT_RIGHT )
        {
            x = pRect->right - w;
        }
        {
            GuiTarget t;
            if ( gui_Target(p, &t) )
            {
                if ( !(format & DT_NOCLIP) )
                {
                    RECT clip = *pRect;
                    OffsetRect(&clip, t.origin.x, t.origin.y);
                    IntersectRect(&t.clip, &t.clip, &clip);
                }
                gui_DrawString(p, &t, p->pFont, s + pos, count, x, y, gui_ColorRefToRGB(p->textColor), p->bkMode == OPAQUE,
                               gui_ColorRefToRGB(p->bkColor), underline >= pos && underline < pos + count ? underline - pos : -1);
            }
        }
        y += lineHeight;
        pos += count;
        while ( pos < len && (s[pos] == ' ' || s[pos] == '\r') )
        {
            pos++;
        }
        if ( pos < len && s[pos] == '\n' )
        {
            pos++;
        }
        if ( format & DT_SINGLELINE )
        {
            break;
        }
    }
    return total;
}

int WINAPI AddFontResourceA(LPCSTR pName)
{
    (void)pName;
    return 0;
}

BOOL WINAPI RemoveFontResourceA(LPCSTR pName)
{
    (void)pName;
    return FALSE;
}

/* ---- The display's buffer DCs (stdDisplay.h) ------------------------------------------------------------------------ */

// IDirectDrawSurface4_GetDC gave the game a GDI DC on a DirectDraw surface. Here the DC draws into the back buffer
// itself, a 32-bit top-down bitmap over the locked pixels, until the DC is released. The windowed dialog path snapshots
// the frame through it (JonesDialog_AllocOffScreenDIBSection) and puts the frame back when the dialogs close; the
// front buffer is the back buffer, as the frame is presented whole.
static HDC gui_BufferDC(void)
{
    void* pPixels;
    uint32_t w;
    uint32_t h;
    int32_t pitch;
    GuiObject* pBitmap;
    GuiDC* pDC;

    if ( stdDisplay_LockBackBuffer(&pPixels, &w, &h, &pitch) != 0 )
    {
        return NULL;
    }
    pBitmap = calloc(1, sizeof(*pBitmap));
    pDC = gui_NewDC();
    if ( !pBitmap || !pDC )
    {
        free(pBitmap);
        if ( pDC )
        {
            gui_Untrack(pDC);
            free(pDC);
        }
        stdDisplay_UnlockBackBuffer();
        return NULL;
    }
    gui_Track(pBitmap);
    pBitmap->magic = GUI_OBJ_MAGIC;
    pBitmap->type = GUI_OBJ_BITMAP;
    pBitmap->width = (int)w;
    pBitmap->height = (int)h;
    pBitmap->bpp = 32;
    pBitmap->pitch = pitch;
    pBitmap->bTopDown = 1;
    pBitmap->pBits = pPixels;
    pBitmap->bOwnsBits = 0;
    pDC->pBitmap = pBitmap;
    return (HDC)pDC;
}

static void gui_ReleaseBufferDC(HDC hdc)
{
    GuiDC* p = gui_DC(hdc);
    if ( !p )
    {
        return;
    }
    DeleteObject((HGDIOBJ)p->pBitmap);
    DeleteDC(hdc);
    stdDisplay_UnlockBackBuffer();
}

HDC J3DAPI stdDisplay_GetFrontBufferDC(void)
{
    return gui_BufferDC();
}

void J3DAPI stdDisplay_ReleaseFrontBufferDC(HDC hdc)
{
    gui_ReleaseBufferDC(hdc);
}

HDC J3DAPI stdDisplay_GetBackBufferDC(void)
{
    return gui_BufferDC();
}

void J3DAPI stdDisplay_ReleaseBackBufferDC(HDC hdc)
{
    gui_ReleaseBufferDC(hdc);
}
