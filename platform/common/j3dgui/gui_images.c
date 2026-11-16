/*
 * j3dgui images (PORT-PLAN.md P6.10): icons, image lists and the drag image, and the cursor functions.
 *
 * The dialogs' 25 icons (Indy3D.rc's ICON resources, IDs 108 to 227) are game art inside the retail executable; the
 * port does not ship them. LoadIcon and LoadImage read them at run time from the player's RESOURCE/INDY3D.EXE: the PE
 * resource directory, RT_GROUP_ICON for the ID, then its RT_ICON image (an 8-bit DIB with its AND mask in the retail
 * file; 1-, 4-, 8-, 24- and 32-bit and PNG-free DIBs are read). With no executable, or no such icon, a plain stand-in is
 * drawn: a raised square with the face colour, so the layout is unchanged.
 *
 * An HICON is a 32-bit image with a 0/255 alpha from the mask. LoadImage at a size other than the icon's scales it.
 */
#include "gui_internal.h"
#include "j3dgui.h"

#include <platform/common/host_dirs.h>
#include <platform/common/host_path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUI_ICON_MAGIC 0x4A49434Fu // "OCIJ"
#define GUI_IMAGELIST_MAGIC 0x4A494C53u

typedef struct GuiIcon
{
    uint32_t magic;
    int width;
    int height;
    uint32_t* pPixels;  // 0xAARRGGBB, alpha 0 or 255
    int bShared;        // from LoadIcon: one per ID, never freed
} GuiIcon;

typedef struct GuiImageList
{
    uint32_t magic;
    int cx;
    int cy;
    int count;
    int capacity;
    GuiIcon** apImages;
    COLORREF bk;
} GuiImageList;

static GuiIcon* gui_Icon(HICON h)
{
    GuiIcon* p = (GuiIcon*)h;
    return p && p->magic == GUI_ICON_MAGIC ? p : NULL;
}

static GuiImageList* gui_ImageList(HIMAGELIST h)
{
    GuiImageList* p = (GuiImageList*)h;
    return p && p->magic == GUI_IMAGELIST_MAGIC ? p : NULL;
}

static GuiIcon* gui_NewIcon(int width, int height)
{
    GuiIcon* p = calloc(1, sizeof(*p));
    if ( !p )
    {
        return NULL;
    }
    p->pPixels = calloc((size_t)width * (size_t)height, 4);
    if ( !p->pPixels )
    {
        free(p);
        return NULL;
    }
    p->magic = GUI_ICON_MAGIC;
    p->width = width;
    p->height = height;
    return p;
}

static GuiIcon* gui_ScaleIcon(const GuiIcon* pSrc, int width, int height)
{
    GuiIcon* p;
    int x;
    int y;
    if ( width <= 0 )
    {
        width = pSrc->width;
    }
    if ( height <= 0 )
    {
        height = pSrc->height;
    }
    p = gui_NewIcon(width, height);
    if ( !p )
    {
        return NULL;
    }
    for ( y = 0; y < height; ++y )
    {
        for ( x = 0; x < width; ++x )
        {
            // a box filter when shrinking (the retail icons are 48 and 64 pixels, the dialogs draw 32 or smaller)
            int sx0 = x * pSrc->width / width;
            int sy0 = y * pSrc->height / height;
            int sx1 = GUI_MAX(sx0 + 1, (x + 1) * pSrc->width / width);
            int sy1 = GUI_MAX(sy0 + 1, (y + 1) * pSrc->height / height);
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            int u;
            int v;
            for ( v = sy0; v < sy1; ++v )
            {
                for ( u = sx0; u < sx1; ++u )
                {
                    uint32_t c = pSrc->pPixels[v * pSrc->width + u];
                    n++;
                    if ( c >> 24 )
                    {
                        a++;
                        r += (c >> 16) & 255;
                        g += (c >> 8) & 255;
                        b += c & 255;
                    }
                }
            }
            // opaque if most of the covered pixels are
            if ( a * 2 >= n && a )
            {
                p->pPixels[y * width + x] = 0xFF000000u | (((r + a / 2) / a) << 16) | (((g + a / 2) / a) << 8) | ((b + a / 2) / a);
            }
        }
    }
    return p;
}

/* ---- Reading the executable ---------------------------------------------------------------------------------------- */

typedef struct GuiExe
{
    uint8_t* pData;
    size_t size;
    size_t rsrcRaw;     // file offset of .rsrc
    uint32_t rsrcRva;
} GuiExe;

static GuiExe gui_exe;
static int gui_bExeTried = 0;

static uint32_t gui_U32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t gui_U16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

// Reads the first of apNames (relative to the data root) that exists and finds its resource section. 1 on success.
static int gui_OpenExe(GuiExe* pExe, const char* const* apNames, size_t numNames)
{
    GuiExe gui_exe; // the code below was written for the one executable; it fills this copy, then pExe
    size_t i;
    memset(&gui_exe, 0, sizeof(gui_exe));
    for ( i = 0; i < numNames && !gui_exe.pData; ++i )
    {
        char aPath[1100];
        FILE* f;
        long size;
        snprintf(aPath, sizeof(aPath), "%s%s", J3DHost_GetDataRoot(), apNames[i]);
        f = J3DHost_fopen(aPath, "rb");
        if ( !f )
        {
            continue;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if ( size > 0x400 && size < (64 << 20) )
        {
            gui_exe.pData = malloc((size_t)size);
            if ( gui_exe.pData && fread(gui_exe.pData, 1, (size_t)size, f) == (size_t)size )
            {
                gui_exe.size = (size_t)size;
            }
            else
            {
                free(gui_exe.pData);
                gui_exe.pData = NULL;
            }
        }
        fclose(f);
    }
    if ( gui_exe.pData )
    {
        // find .rsrc
        uint32_t pe = gui_exe.size > 0x40 ? gui_U32(gui_exe.pData + 0x3C) : 0;
        if ( pe + 24 < gui_exe.size && memcmp(gui_exe.pData + pe, "PE\0\0", 4) == 0 )
        {
            int numSections = gui_U16(gui_exe.pData + pe + 6);
            size_t sections = pe + 24 + gui_U16(gui_exe.pData + pe + 20);
            int k;
            for ( k = 0; k < numSections && sections + (size_t)(k + 1) * 40 <= gui_exe.size; ++k )
            {
                const uint8_t* s = gui_exe.pData + sections + (size_t)k * 40;
                if ( memcmp(s, ".rsrc", 5) == 0 )
                {
                    gui_exe.rsrcRva = gui_U32(s + 12);
                    gui_exe.rsrcRaw = gui_U32(s + 20);
                }
            }
        }
        if ( !gui_exe.rsrcRaw )
        {
            free(gui_exe.pData);
            gui_exe.pData = NULL;
        }
    }
    *pExe = gui_exe;
    return gui_exe.pData != NULL;
}

static int gui_LoadExe(void)
{
    static const char* const aNames[] = { "Resource/Indy3D.exe", "Indy3D.exe" };
    if ( gui_bExeTried )
    {
        return gui_exe.pData != NULL;
    }
    gui_bExeTried = 1;
    if ( !gui_OpenExe(&gui_exe, aNames, sizeof(aNames) / sizeof(aNames[0])) )
    {
        fprintf(stderr, "j3dgui: the dialog icons are read from the game's Resource/Indy3D.exe, which was not found; plain icons are drawn\n");
    }
    return gui_exe.pData != NULL;
}

// The data of resource type/id (the first language) in pExe, or NULL.
static const uint8_t* gui_FindResourceIn(const GuiExe* pExe, uint32_t type, uint32_t id, uint32_t* pSize)
{
    const GuiExe gui_exe = *pExe;
    const uint8_t* base = gui_exe.pData + gui_exe.rsrcRaw;
    size_t limit = gui_exe.size - gui_exe.rsrcRaw;
    uint32_t want[2] = { type, id };
    size_t dir = 0;
    int level;
    for ( level = 0; level < 3; ++level )
    {
        int count;
        int k;
        size_t found = 0;
        if ( dir + 16 > limit )
        {
            return NULL;
        }
        count = gui_U16(base + dir + 12) + gui_U16(base + dir + 14);
        for ( k = 0; k < count; ++k )
        {
            const uint8_t* e = base + dir + 16 + (size_t)k * 8;
            if ( (size_t)(e - base) + 8 > limit )
            {
                return NULL;
            }
            if ( level == 2 || gui_U32(e) == want[level] )
            {
                found = gui_U32(e + 4);
                break;
            }
        }
        if ( !found )
        {
            return NULL;
        }
        if ( level < 2 )
        {
            dir = found & 0x7FFFFFFF;
        }
        else
        {
            size_t entry = found & 0x7FFFFFFF;
            uint32_t rva;
            size_t at;
            if ( entry + 16 > limit )
            {
                return NULL;
            }
            rva = gui_U32(base + entry);
            *pSize = gui_U32(base + entry + 4);
            at = gui_exe.rsrcRaw + (rva - gui_exe.rsrcRva);
            if ( rva < gui_exe.rsrcRva || at + *pSize > gui_exe.size )
            {
                return NULL;
            }
            return gui_exe.pData + at;
        }
    }
    return NULL;
}

static const uint8_t* gui_FindResource(uint32_t type, uint32_t id, uint32_t* pSize)
{
    return gui_FindResourceIn(&gui_exe, type, id, pSize);
}

int J3DGui_ReadExeBitmap(const char* pExeName, int id, uint8_t** ppBmpFile, size_t* pSize)
{
    GuiExe exe;
    const uint8_t* pDib;
    uint32_t dibSize = 0;
    int bFound = 0;
    *ppBmpFile = NULL;
    *pSize = 0;
    if ( !gui_OpenExe(&exe, &pExeName, 1) )
    {
        return 0;
    }
    pDib = gui_FindResourceIn(&exe, 2, (uint32_t)id, &dibSize); // RT_BITMAP: a DIB without its file header
    if ( pDib && dibSize >= 40 )
    {
        const uint32_t headerSize = gui_U32(pDib);
        const uint16_t bpp = gui_U16(pDib + 14);
        uint32_t numColors = gui_U32(pDib + 32);
        if ( !numColors && bpp <= 8 )
        {
            numColors = 1u << bpp;
        }
        *ppBmpFile = malloc((size_t)dibSize + 14);
        if ( *ppBmpFile && headerSize >= 40 && headerSize + numColors * 4 <= dibSize )
        {
            uint8_t* p = *ppBmpFile;
            const uint32_t fileSize = dibSize + 14;
            const uint32_t bitsAt = 14 + headerSize + numColors * 4;
            p[0] = 'B';
            p[1] = 'M';
            memcpy(p + 2, &fileSize, 4);
            memset(p + 6, 0, 4);
            memcpy(p + 10, &bitsAt, 4);
            memcpy(p + 14, pDib, dibSize);
            *pSize = fileSize;
            bFound = 1;
        }
        else
        {
            free(*ppBmpFile);
            *ppBmpFile = NULL;
        }
    }
    free(exe.pData);
    return bFound;
}

// An RT_ICON image: BITMAPINFOHEADER (height doubled), palette, XOR bits, AND mask.
static GuiIcon* gui_DecodeIcon(const uint8_t* p, uint32_t size)
{
    int width;
    int height;
    int bpp;
    int colors;
    size_t header;
    size_t xorPitch;
    size_t andPitch;
    const uint8_t* pPalette;
    const uint8_t* pXor;
    const uint8_t* pAnd;
    GuiIcon* pIcon;
    int x;
    int y;
    if ( size < 40 || gui_U32(p) < 40 )
    {
        return NULL;
    }
    header = gui_U32(p);
    width = (int)gui_U32(p + 4);
    height = (int)gui_U32(p + 8) / 2;
    bpp = gui_U16(p + 14);
    colors = (int)gui_U32(p + 32);
    if ( width <= 0 || height <= 0 || width > 256 || height > 256 )
    {
        return NULL;
    }
    if ( bpp <= 8 && colors == 0 )
    {
        colors = 1 << bpp;
    }
    if ( bpp > 8 )
    {
        colors = 0;
    }
    pPalette = p + header;
    pXor = pPalette + colors * 4;
    xorPitch = (((size_t)width * (size_t)bpp + 31) / 32) * 4;
    andPitch = (((size_t)width + 31) / 32) * 4;
    pAnd = pXor + xorPitch * (size_t)height;
    if ( (size_t)(pAnd - p) + andPitch * (size_t)height > size )
    {
        return NULL;
    }
    pIcon = gui_NewIcon(width, height);
    if ( !pIcon )
    {
        return NULL;
    }
    for ( y = 0; y < height; ++y )
    {
        const uint8_t* row = pXor + xorPitch * (size_t)(height - 1 - y); // bottom-up
        const uint8_t* mask = pAnd + andPitch * (size_t)(height - 1 - y);
        for ( x = 0; x < width; ++x )
        {
            uint32_t c;
            int transparent = (mask[x / 8] >> (7 - (x & 7))) & 1;
            switch ( bpp )
            {
                case 1:
                case 4:
                case 8:
                {
                    int index = bpp == 8 ? row[x] : bpp == 4 ? ((x & 1) ? row[x / 2] & 15 : row[x / 2] >> 4) : (row[x / 8] >> (7 - (x & 7))) & 1;
                    const uint8_t* q = pPalette + index * 4;
                    c = ((uint32_t)q[2] << 16) | ((uint32_t)q[1] << 8) | q[0];
                    break;
                }
                case 24:
                    c = ((uint32_t)row[3 * x + 2] << 16) | ((uint32_t)row[3 * x + 1] << 8) | row[3 * x];
                    break;
                case 32:
                    c = gui_U32(row + 4 * x) & 0xFFFFFF;
                    if ( row[4 * x + 3] < 128 )
                    {
                        transparent = 1;
                    }
                    break;
                default:
                    c = 0;
                    break;
            }
            pIcon->pPixels[y * width + x] = transparent ? 0 : (0xFF000000u | c);
        }
    }
    return pIcon;
}

static GuiIcon* gui_LoadIconResource(int id)
{
    const uint8_t* pGroup;
    uint32_t groupSize;
    int count;
    int best = -1;
    int bestSize = 0;
    int k;
    if ( !gui_LoadExe() )
    {
        return NULL;
    }
    pGroup = gui_FindResource(14, (uint32_t)id, &groupSize); // RT_GROUP_ICON
    if ( !pGroup || groupSize < 6 )
    {
        return NULL;
    }
    count = gui_U16(pGroup + 4);
    for ( k = 0; k < count && 6 + (uint32_t)(k + 1) * 14 <= groupSize; ++k )
    {
        const uint8_t* e = pGroup + 6 + k * 14;
        int w = e[0] ? e[0] : 256;
        if ( w > bestSize )
        {
            bestSize = w;
            best = gui_U16(e + 12);
        }
    }
    if ( best < 0 )
    {
        return NULL;
    }
    {
        uint32_t size;
        const uint8_t* pImage = gui_FindResource(3, (uint32_t)best, &size); // RT_ICON
        return pImage ? gui_DecodeIcon(pImage, size) : NULL;
    }
}

// A stand-in: a raised face-coloured square.
static GuiIcon* gui_PlainIcon(int width, int height)
{
    GuiIcon* p = gui_NewIcon(width, height);
    int x;
    int y;
    if ( !p )
    {
        return NULL;
    }
    for ( y = 0; y < height; ++y )
    {
        for ( x = 0; x < width; ++x )
        {
            uint32_t c = 0xFFC0C0C0u;
            if ( x == 0 || y == 0 )
            {
                c = 0xFFFFFFFFu;
            }
            else if ( x == width - 1 || y == height - 1 )
            {
                c = 0xFF000000u;
            }
            else if ( x == width - 2 || y == height - 2 )
            {
                c = 0xFF808080u;
            }
            p->pPixels[y * width + x] = c;
        }
    }
    return p;
}

static GuiIcon* gui_SharedIcon(int id)
{
    static struct { int id; GuiIcon* pIcon; } aCache[64];
    static int count = 0;
    int k;
    GuiIcon* p;
    for ( k = 0; k < count; ++k )
    {
        if ( aCache[k].id == id )
        {
            return aCache[k].pIcon;
        }
    }
    p = gui_LoadIconResource(id);
    if ( !p )
    {
        p = gui_PlainIcon(32, 32);
    }
    if ( p )
    {
        p->bShared = 1;
        if ( count < 64 )
        {
            aCache[count].id = id;
            aCache[count].pIcon = p;
            count++;
        }
    }
    return p;
}

int J3DGui_GetIconPixels(HICON hIcon, int* pWidth, int* pHeight, const uint32_t** ppPixels)
{
    GuiIcon* p = gui_Icon(hIcon);
    if ( !p )
    {
        return 0;
    }
    *pWidth = p->width;
    *pHeight = p->height;
    *ppPixels = p->pPixels;
    return 1;
}

HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR pName)
{
    (void)hInstance;
    if ( (uintptr_t)pName >= 0x10000 )
    {
        return NULL; // named icons: none in the game's resources
    }
    if ( (uintptr_t)pName >= 32512 )
    {
        return (HICON)gui_SharedIcon(-(int)(uintptr_t)pName); // IDI_*: the stand-in
    }
    return (HICON)gui_SharedIcon((int)(uintptr_t)pName);
}

HANDLE WINAPI LoadImageA(HINSTANCE hInstance, LPCSTR pName, UINT type, int cx, int cy, UINT flags)
{
    GuiIcon* pShared;
    (void)hInstance;
    (void)flags;
    if ( type != IMAGE_ICON || (uintptr_t)pName >= 0x10000 )
    {
        return NULL;
    }
    pShared = gui_SharedIcon((int)(uintptr_t)pName);
    if ( !pShared )
    {
        return NULL;
    }
    if ( (cx <= 0 || cx == pShared->width) && (cy <= 0 || cy == pShared->height) )
    {
        return (HANDLE)gui_ScaleIcon(pShared, pShared->width, pShared->height);
    }
    return (HANDLE)gui_ScaleIcon(pShared, cx, cy);
}

BOOL WINAPI DestroyIcon(HICON hIcon)
{
    GuiIcon* p = gui_Icon(hIcon);
    if ( !p )
    {
        return FALSE;
    }
    if ( !p->bShared )
    {
        p->magic = 0;
        free(p->pPixels);
        free(p);
    }
    return TRUE;
}

BOOL WINAPI DestroyCursor(HCURSOR hCursor)
{
    return DestroyIcon(hCursor);
}

void J3DGui_DrawIconAt(HDC hdc, HICON hIcon, int x, int y, int cx, int cy)
{
    GuiIcon* p = gui_Icon(hIcon);
    GuiIcon* pScaled = NULL;
    int u;
    int v;
    if ( !p )
    {
        return;
    }
    if ( cx > 0 && cy > 0 && (cx != p->width || cy != p->height) )
    {
        pScaled = gui_ScaleIcon(p, cx, cy);
        if ( pScaled )
        {
            p = pScaled;
        }
    }
    for ( v = 0; v < p->height; ++v )
    {
        for ( u = 0; u < p->width; ++u )
        {
            uint32_t c = p->pPixels[v * p->width + u];
            if ( c >> 24 )
            {
                RECT px;
                SetRect(&px, x + u, y + v, x + u + 1, y + v + 1);
                J3DGui_FillRect32(hdc, &px, c & 0xFFFFFF);
            }
        }
    }
    if ( pScaled )
    {
        free(pScaled->pPixels);
        free(pScaled);
    }
}

BOOL WINAPI DrawIcon(HDC hdc, int x, int y, HICON hIcon)
{
    J3DGui_DrawIconAt(hdc, hIcon, x, y, 0, 0);
    return gui_Icon(hIcon) != NULL;
}

BOOL WINAPI DrawIconEx(HDC hdc, int x, int y, HICON hIcon, int cx, int cy, UINT step, HBRUSH hBrush, UINT flags)
{
    (void)step;
    (void)hBrush;
    (void)flags;
    J3DGui_DrawIconAt(hdc, hIcon, x, y, cx, cy);
    return gui_Icon(hIcon) != NULL;
}

BOOL WINAPI GetIconInfo(HICON hIcon, PICONINFO pInfo)
{
    GuiIcon* p = gui_Icon(hIcon);
    BITMAPINFO bmi;
    void* pBits;
    if ( !p || !pInfo )
    {
        return FALSE;
    }
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->fIcon = TRUE;
    pInfo->xHotspot = (DWORD)(p->width / 2);
    pInfo->yHotspot = (DWORD)(p->height / 2);
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = p->width;
    bmi.bmiHeader.biHeight = -p->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    pInfo->hbmColor = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if ( pInfo->hbmColor && pBits )
    {
        int k;
        for ( k = 0; k < p->width * p->height; ++k )
        {
            ((uint32_t*)pBits)[k] = p->pPixels[k] & 0xFFFFFF;
        }
    }
    pInfo->hbmMask = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if ( pInfo->hbmMask && pBits )
    {
        int k;
        for ( k = 0; k < p->width * p->height; ++k )
        {
            ((uint32_t*)pBits)[k] = (p->pPixels[k] >> 24) ? 0 : 0xFFFFFF;
        }
    }
    return TRUE;
}

/* ---- Image lists --------------------------------------------------------------------------------------------------- */

HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int initial, int grow)
{
    GuiImageList* p = calloc(1, sizeof(*p));
    (void)flags;
    (void)grow;
    if ( !p )
    {
        return NULL;
    }
    p->magic = GUI_IMAGELIST_MAGIC;
    p->cx = cx;
    p->cy = cy;
    p->capacity = GUI_MAX(initial, 4);
    p->apImages = calloc((size_t)p->capacity, sizeof(GuiIcon*));
    p->bk = CLR_NONE;
    return (HIMAGELIST)p;
}

BOOL WINAPI ImageList_Destroy(HIMAGELIST h)
{
    GuiImageList* p = gui_ImageList(h);
    int k;
    if ( !p )
    {
        return FALSE;
    }
    for ( k = 0; k < p->count; ++k )
    {
        DestroyIcon((HICON)p->apImages[k]);
    }
    free(p->apImages);
    p->magic = 0;
    free(p);
    return TRUE;
}

int WINAPI ImageList_ReplaceIcon(HIMAGELIST h, int index, HICON hIcon)
{
    GuiImageList* p = gui_ImageList(h);
    GuiIcon* pIcon = gui_Icon(hIcon);
    GuiIcon* pCopy;
    if ( !p || !pIcon )
    {
        return -1;
    }
    pCopy = gui_ScaleIcon(pIcon, p->cx, p->cy);
    if ( !pCopy )
    {
        return -1;
    }
    if ( index < 0 || index >= p->count )
    {
        if ( p->count == p->capacity )
        {
            GuiIcon** a = realloc(p->apImages, (size_t)p->capacity * 2 * sizeof(GuiIcon*));
            if ( !a )
            {
                DestroyIcon((HICON)pCopy);
                return -1;
            }
            p->apImages = a;
            p->capacity *= 2;
        }
        index = p->count++;
    }
    else
    {
        DestroyIcon((HICON)p->apImages[index]);
    }
    p->apImages[index] = pCopy;
    return index;
}

int WINAPI ImageList_GetImageCount(HIMAGELIST h)
{
    GuiImageList* p = gui_ImageList(h);
    return p ? p->count : 0;
}

BOOL WINAPI ImageList_GetIconSize(HIMAGELIST h, int* pcx, int* pcy)
{
    GuiImageList* p = gui_ImageList(h);
    if ( !p )
    {
        return FALSE;
    }
    if ( pcx )
    {
        *pcx = p->cx;
    }
    if ( pcy )
    {
        *pcy = p->cy;
    }
    return TRUE;
}

BOOL WINAPI ImageList_Remove(HIMAGELIST h, int index)
{
    GuiImageList* p = gui_ImageList(h);
    int k;
    if ( !p )
    {
        return FALSE;
    }
    if ( index < 0 )
    {
        for ( k = 0; k < p->count; ++k )
        {
            DestroyIcon((HICON)p->apImages[k]);
        }
        p->count = 0;
        return TRUE;
    }
    if ( index >= p->count )
    {
        return FALSE;
    }
    DestroyIcon((HICON)p->apImages[index]);
    memmove(&p->apImages[index], &p->apImages[index + 1], (size_t)(p->count - index - 1) * sizeof(GuiIcon*));
    p->count--;
    return TRUE;
}

COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST h, COLORREF bk)
{
    GuiImageList* p = gui_ImageList(h);
    COLORREF old = p ? p->bk : CLR_NONE;
    if ( p )
    {
        p->bk = bk;
    }
    return old;
}

BOOL WINAPI ImageList_Draw(HIMAGELIST h, int index, HDC hdc, int x, int y, UINT style)
{
    GuiImageList* p = gui_ImageList(h);
    (void)style;
    if ( !p || index < 0 || index >= p->count )
    {
        return FALSE;
    }
    J3DGui_DrawIconAt(hdc, (HICON)p->apImages[index], x, y, 0, 0);
    return TRUE;
}

int WINAPI ImageList_Add(HIMAGELIST h, HBITMAP hImage, HBITMAP hMask)
{
    (void)h;
    (void)hImage;
    (void)hMask;
    return -1; // the game adds icons only (ImageList_ReplaceIcon)
}

int WINAPI ImageList_AddMasked(HIMAGELIST h, HBITMAP hImage, COLORREF mask)
{
    (void)h;
    (void)hImage;
    (void)mask;
    return -1;
}

/* ---- The drag image ---------------------------------------------------------------------------------------------------- */

static GuiIcon* gui_pDragImage = NULL;
static POINT gui_dragHotspot;
static HWND gui_hDragWindow = NULL;
static POINT gui_dragPos;           // relative to gui_hDragWindow's window rectangle
static int gui_bDragVisible = 0;

BOOL WINAPI ImageList_BeginDrag(HIMAGELIST h, int index, int hotX, int hotY)
{
    GuiImageList* p = gui_ImageList(h);
    if ( !p || index < 0 || index >= p->count )
    {
        return FALSE;
    }
    if ( gui_pDragImage )
    {
        DestroyIcon((HICON)gui_pDragImage);
    }
    gui_pDragImage = gui_ScaleIcon(p->apImages[index], p->cx, p->cy);
    gui_dragHotspot.x = hotX;
    gui_dragHotspot.y = hotY;
    return gui_pDragImage != NULL;
}

void WINAPI ImageList_EndDrag(void)
{
    if ( gui_pDragImage )
    {
        DestroyIcon((HICON)gui_pDragImage);
        gui_pDragImage = NULL;
    }
    gui_bDragVisible = 0;
    J3DGuiHost_RequestPresent();
}

BOOL WINAPI ImageList_DragEnter(HWND hwnd, int x, int y)
{
    gui_hDragWindow = hwnd;
    gui_dragPos.x = x;
    gui_dragPos.y = y;
    gui_bDragVisible = 1;
    J3DGuiHost_RequestPresent();
    return TRUE;
}

BOOL WINAPI ImageList_DragLeave(HWND hwnd)
{
    (void)hwnd;
    gui_bDragVisible = 0;
    J3DGuiHost_RequestPresent();
    return TRUE;
}

BOOL WINAPI ImageList_DragMove(int x, int y)
{
    gui_dragPos.x = x;
    gui_dragPos.y = y;
    J3DGuiHost_RequestPresent();
    return TRUE;
}

BOOL WINAPI ImageList_DragShowNolock(BOOL bShow)
{
    gui_bDragVisible = bShow != 0;
    J3DGuiHost_RequestPresent();
    return TRUE;
}

// Draws the drag image over the composed screen (gui_dialog.c), half-transparent like Windows' drag images.
void J3DGui_ComposeOverlay(uint32_t* pPixels, int width, int height, int pitchPixels)
{
    RECT origin;
    int u;
    int v;
    if ( !gui_bDragVisible || !gui_pDragImage )
    {
        return;
    }
    GetWindowRect(gui_hDragWindow, &origin);
    for ( v = 0; v < gui_pDragImage->height; ++v )
    {
        int y = origin.top + gui_dragPos.y - gui_dragHotspot.y + v;
        if ( y < 0 || y >= height )
        {
            continue;
        }
        for ( u = 0; u < gui_pDragImage->width; ++u )
        {
            int x = origin.left + gui_dragPos.x - gui_dragHotspot.x + u;
            uint32_t c = gui_pDragImage->pPixels[v * gui_pDragImage->width + u];
            if ( x < 0 || x >= width || !(c >> 24) || ((x + y) & 1) )
            {
                continue; // a checkerboard of the image, Windows' "dithered" drag look
            }
            pPixels[(size_t)y * (size_t)pitchPixels + x] = c & 0xFFFFFF;
        }
    }
}

/* ---- Cursor -------------------------------------------------------------------------------------------------------- */

static POINT gui_cursor = { 0, 0 };
static int gui_cursorCount = 0;

void J3DGui_SetCursorPosition(int x, int y)
{
    gui_cursor.x = x;
    gui_cursor.y = y;
}

BOOL WINAPI GetCursorPos(LPPOINT pPoint)
{
    if ( !pPoint )
    {
        return FALSE;
    }
    *pPoint = gui_cursor;
    return TRUE;
}

BOOL WINAPI SetCursorPos(int x, int y)
{
    gui_cursor.x = x;
    gui_cursor.y = y;
    return TRUE;
}

int WINAPI ShowCursor(BOOL bShow)
{
    gui_cursorCount += bShow ? 1 : -1;
    return gui_cursorCount;
}

HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR pName)
{
    static int arrow;
    (void)hInstance;
    (void)pName;
    return (HCURSOR)&arrow; // the system cursor is the host's; this only has to be non-NULL
}

HCURSOR WINAPI SetCursor(HCURSOR hCursor)
{
    static HCURSOR hCurrent = NULL;
    HCURSOR hOld = hCurrent;
    hCurrent = hCursor;
    return hOld;
}

BOOL WINAPI ClipCursor(const RECT* pRect)
{
    (void)pRect;
    return TRUE;
}

BOOL WINAPI GetClipCursor(LPRECT pRect)
{
    int w;
    int h;
    if ( !pRect )
    {
        return FALSE;
    }
    J3DGuiHost_GetScreenSize(&w, &h);
    SetRect(pRect, 0, 0, w, h);
    return TRUE;
}

/* ---- Keyboard state ------------------------------------------------------------------------------------------------ */

static uint8_t gui_aKeyDown[256];

void J3DGui_TrackKey(UINT msg, WPARAM vk)
{
    if ( vk < 256 )
    {
        gui_aKeyDown[vk] = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if ( vk == VK_LSHIFT || vk == VK_RSHIFT )
        {
            gui_aKeyDown[VK_SHIFT] = gui_aKeyDown[vk];
        }
        if ( vk == VK_LCONTROL || vk == VK_RCONTROL )
        {
            gui_aKeyDown[VK_CONTROL] = gui_aKeyDown[vk];
        }
        if ( vk == VK_LMENU || vk == VK_RMENU )
        {
            gui_aKeyDown[VK_MENU] = gui_aKeyDown[vk];
        }
    }
}

SHORT WINAPI GetKeyState(int vk)
{
    return (vk >= 0 && vk < 256 && gui_aKeyDown[vk]) ? (SHORT)0x8000 : 0;
}

SHORT WINAPI GetAsyncKeyState(int vk)
{
    return GetKeyState(vk);
}
