/*
 * display: the system-memory half of stdDisplay that every build shares (platform/common/stdDisplay_common.c), through
 * the headless display device. Needs no game data.
 *
 *   - a windowed mode at an odd size, its back buffer, lock and size queries (P2.4)
 *   - VBufferFill and BackBufferFill, whole and by rectangle (P2.4)
 *   - VBufferCopy at 8, 16 and 32 bpp, plain and colour-keyed, at positions and rectangles that overhang the
 *     destination or the source on every side, against a pixel-by-pixel reference (P2.5)
 *   - EncodeFromRGB565 (P2.5)
 *   - SaveScreen: the BMP header and the pixels, at a width whose rows need padding (P2.7)
 *   - VBufferConvertColorFormat from the materials' 16-bit formats (RGB565, ARGB1555, ARGB4444) to the renderer's
 *     32-bit one expands each channel to full range, as the GPU sampled them in retail: an opaque 1555 texel has alpha
 *     255, not 0x80, which the alpha test (reference 0xA0) discarded (Indy's revolver was invisible)
 *   - the camera's focal length by the canvas's shape (P8.9): across the width at 4:3 and on a narrower picture, as
 *     retail; on a wider one, with J3D_FIXES, so that the 4:3 height is seen (more at the sides)
 *
 * Exit status: the number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <std/General/std.h>
#include <std/General/stdUtil.h>
#include <std/Win95/stdDisplay.h>
#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Engine/rdCanvas.h>

#include "harness.h"

static int display_numFailures = 0;

#define CHECK(cond, ...)                                    \
    do                                                      \
    {                                                       \
        if ( !(cond) )                                      \
        {                                                   \
            ++display_numFailures;                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while ( 0 )

#define BB_WIDTH  37 // 3 * 37 = 111 bytes: a BMP row needs one byte of padding
#define BB_HEIGHT 23

static uint32_t display_GetPixel(const tVBuffer* pBuf, size_t x, size_t y)
{
    const size_t bpp = pBuf->rasterInfo.colorInfo.bpp / 8;
    uint32_t pixel = 0;
    memcpy(&pixel, pBuf->pPixels + y * pBuf->rasterInfo.rowSize + x * bpp, bpp);
    return pixel;
}

static void display_SetPixel(tVBuffer* pBuf, size_t x, size_t y, uint32_t pixel)
{
    const size_t bpp = pBuf->rasterInfo.colorInfo.bpp / 8;
    memcpy(pBuf->pPixels + y * pBuf->rasterInfo.rowSize + x * bpp, &pixel, bpp);
}

static tVBuffer* display_NewBuffer(uint32_t width, uint32_t height, uint32_t bpp)
{
    tRasterInfo info;
    memset(&info, 0, sizeof(info));
    info.width               = width;
    info.height              = height;
    info.colorInfo.colorMode = bpp == 8 ? STDCOLOR_PAL : STDCOLOR_RGB;
    info.colorInfo.bpp       = bpp;
    return stdDisplay_VBufferNew(&info, 0, 0);
}

// The focal length a 90-degree camera gets on a canvas of the given size (rdCamera_BuildFOV)
static float display_FocalLength(uint32_t width, uint32_t height)
{
    tVBuffer* pBuf = display_NewBuffer(width, height, 32);
    rdCanvas* pCanvas = pBuf ? rdCanvas_New(0, pBuf, 0, 0, 0, 0) : NULL;
    rdCamera* pCamera = rdCamera_New(90.0f, 0, 0.1f, 100.0f, 1.0f);
    float focal = -1.0f;
    if ( pCanvas && pCamera )
    {
        rdCamera_SetCanvas(pCamera, pCanvas);
        focal = pCamera->focalLength;
    }
    if ( pCamera )
    {
        rdCamera_Free(pCamera);
    }
    if ( pCanvas )
    {
        rdCanvas_Free(pCanvas);
    }
    if ( pBuf )
    {
        stdDisplay_VBufferFree(pBuf);
    }
    return focal;
}

static void display_TestCameraShape(void)
{
    // The canvas's rectangle is inclusive, so its half sizes are (w - 1) / 2 and (h - 1) / 2; tan(45) is 1
    float f = display_FocalLength(640, 480);
    CHECK(f > 319.49f && f < 319.51f, "4:3 focal length %f, not 319.5 (its half width)", (double)f);
    f = display_FocalLength(1024, 1280); // narrower than 4:3: across the width, as retail
    CHECK(f > 511.49f && f < 511.51f, "5:4 portrait focal length %f, not 511.5 (its half width)", (double)f);
    f = display_FocalLength(1920, 1080);
#ifdef J3D_FIXES
    CHECK(f > 719.32f && f < 719.35f, "16:9 focal length %f, not 719.33 (the half height's 4:3 width)", (double)f);
#else
    CHECK(f > 959.49f && f < 959.51f, "16:9 focal length %f, not retail's 959.5 (its half width)", (double)f);
#endif
}

// What VBufferCopy must do, one pixel at a time: every source pixel inside both the rectangle and the source lands at
// its offset from (x, y), if that is inside the destination, unless it is keyed out.
static void display_ReferenceCopy(tVBuffer* pDst, const tVBuffer* pSrc, int x, int y, const StdRect* pRect, int bAlpha)
{
    int left = 0, top = 0, width = (int)pSrc->rasterInfo.width, height = (int)pSrc->rasterInfo.height;
    if ( pRect )
    {
        left = pRect->left; top = pRect->top; width = pRect->right; height = pRect->bottom;
    }

    const uint32_t mask = pSrc->rasterInfo.colorInfo.bpp == 32 ? 0xFFFFFFFFu : (1u << pSrc->rasterInfo.colorInfo.bpp) - 1;
    for ( int sy = top; sy < top + height; ++sy )
    {
        for ( int sx = left; sx < left + width; ++sx )
        {
            if ( sx < 0 || sy < 0 || sx >= (int)pSrc->rasterInfo.width || sy >= (int)pSrc->rasterInfo.height )
            {
                continue;
            }
            const int dx = x + (sx - left), dy = y + (sy - top);
            if ( dx < 0 || dy < 0 || dx >= (int)pDst->rasterInfo.width || dy >= (int)pDst->rasterInfo.height )
            {
                continue;
            }
            const uint32_t pixel = display_GetPixel(pSrc, (size_t)sx, (size_t)sy);
            if ( (bAlpha & 1) && pixel == ((uint32_t)pSrc->colorKey & mask) )
            {
                continue;
            }
            display_SetPixel(pDst, (size_t)dx, (size_t)dy, pixel);
        }
    }
}

static void display_TestCopy(uint32_t bpp)
{
    tVBuffer* pSrc = display_NewBuffer(9, 7, bpp);
    tVBuffer* pDst = display_NewBuffer(BB_WIDTH, BB_HEIGHT, bpp);
    tVBuffer* pRef = display_NewBuffer(BB_WIDTH, BB_HEIGHT, bpp);
    if ( !pSrc || !pDst || !pRef )
    {
        CHECK(0, "%u bpp: VBufferNew failed", bpp);
        return;
    }

    const uint32_t key = bpp == 8 ? 0x5A : 0x5A5A;
    for ( size_t y = 0; y < 7; ++y )
    {
        for ( size_t x = 0; x < 9; ++x )
        {
            uint32_t pixel = (uint32_t)(0x01020304u * (y * 9 + x + 1));
            display_SetPixel(pSrc, x, y, (x + y) % 3 == 0 ? key : pixel); // every third diagonal is the colour key
        }
    }
    stdDisplay_VBufferSetColorKey(pSrc, (int)key);

    static const int aPositions[][2] = {
        { 0, 0 }, { 5, 4 }, { -4, -3 }, { 32, 19 }, { -8, 10 }, { 30, -6 }, { -20, 0 }, { 0, 40 }, { 36, 22 },
    };
    static const StdRect aRects[] = {
        { 0, 0, 9, 7 }, { 2, 1, 4, 3 }, { -2, -1, 6, 5 }, { 5, 4, 10, 10 }, { 0, 0, 0, 3 },
    };

    for ( size_t p = 0; p < STD_ARRAYLEN(aPositions); ++p )
    {
        for ( size_t r = 0; r <= STD_ARRAYLEN(aRects); ++r ) // the last pass: no rectangle
        {
            for ( int bAlpha = 0; bAlpha <= 1; ++bAlpha )
            {
                const StdRect* pRect = r < STD_ARRAYLEN(aRects) ? &aRects[r] : NULL;
                for ( size_t i = 0; i < pDst->rasterInfo.size; ++i )
                {
                    pDst->pPixels[i] = pRef->pPixels[i] = (uint8_t)(i * 7);
                }

                display_ReferenceCopy(pRef, pSrc, aPositions[p][0], aPositions[p][1], pRect, bAlpha);
                int res = stdDisplay_VBufferCopy(pDst, pSrc, (unsigned int)aPositions[p][0], aPositions[p][1], pRect, bAlpha);
                CHECK(res == 1, "%u bpp: VBufferCopy returned %d", bpp, res);
                CHECK(memcmp(pDst->pPixels, pRef->pPixels, pDst->rasterInfo.size) == 0,
                    "%u bpp: copy to (%d,%d), rect %zu, key %d differs from the reference", bpp, aPositions[p][0],
                    aPositions[p][1], r, bAlpha);
            }
        }
    }

    stdDisplay_VBufferFree(pSrc);
    stdDisplay_VBufferFree(pDst);
    stdDisplay_VBufferFree(pRef);
}

static void display_TestSaveScreen(void)
{
    char aPath[] = "/tmp/j3d-display-XXXXXX.bmp";
    int  fd = mkstemps(aPath, 4);
    CHECK(fd >= 0, "mkstemps failed");
    if ( fd < 0 )
    {
        return;
    }
    close(fd);

    // A distinct colour per pixel, so a row or column mixed up shows.
    for ( size_t y = 0; y < BB_HEIGHT; ++y )
    {
        for ( size_t x = 0; x < BB_WIDTH; ++x )
        {
            display_SetPixel(&stdDisplay_backBuffer, x, y, (uint32_t)((x * 6) << 16 | (y * 11) << 8 | (x + y)));
        }
    }

    CHECK(stdDisplay_SaveScreen(aPath) == 0, "SaveScreen failed");

    FILE* pFile = fopen(aPath, "rb");
    uint8_t aData[54 + 112 * BB_HEIGHT + 16];
    size_t  numRead = pFile ? fread(aData, 1, sizeof(aData), pFile) : 0;
    if ( pFile )
    {
        fclose(pFile);
    }
    unlink(aPath);

    const size_t rowSize = 112; // 3 * 37 = 111, padded to 4
    CHECK(numRead == 54 + rowSize * BB_HEIGHT, "BMP is %zu bytes, expected %zu", numRead, 54 + rowSize * BB_HEIGHT);
    if ( numRead != 54 + rowSize * BB_HEIGHT )
    {
        return;
    }

    uint32_t bfSize, biWidth, biHeight, biSizeImage;
    uint16_t biBitCount;
    memcpy(&bfSize, aData + 2, 4);
    memcpy(&biWidth, aData + 18, 4);
    memcpy(&biHeight, aData + 22, 4);
    memcpy(&biBitCount, aData + 28, 2);
    memcpy(&biSizeImage, aData + 34, 4);
    CHECK(aData[0] == 'B' && aData[1] == 'M', "BMP signature");
    CHECK(bfSize == numRead, "bfSize %u, file %zu bytes", bfSize, numRead);
    CHECK(biWidth == BB_WIDTH && biHeight == BB_HEIGHT && biBitCount == 24, "BMP %ux%u x %u", biWidth, biHeight, biBitCount);
    CHECK(biSizeImage == rowSize * BB_HEIGHT, "biSizeImage %u", biSizeImage);

    int bPixelsOk = 1;
    for ( size_t y = 0; y < BB_HEIGHT && bPixelsOk; ++y )
    {
        const uint8_t* pRow = aData + 54 + (BB_HEIGHT - 1 - y) * rowSize; // bottom-up
        for ( size_t x = 0; x < BB_WIDTH; ++x )
        {
            uint32_t want = display_GetPixel(&stdDisplay_backBuffer, x, y);
            uint32_t got  = (uint32_t)pRow[3 * x] | (uint32_t)pRow[3 * x + 1] << 8 | (uint32_t)pRow[3 * x + 2] << 16;
            if ( got != (want & 0xFFFFFF) )
            {
                CHECK(0, "BMP pixel (%zu,%zu) is %06X, back buffer %06X", x, y, got, want & 0xFFFFFF);
                bPixelsOk = 0;
                break;
            }
        }
        CHECK(pRow[rowSize - 1] == 0, "BMP row %zu padding byte is %u", y, pRow[rowSize - 1]);
    }
}

// One 16-bit texel through the conversion to 32-bit BGRA (with alpha when the source has it).
static uint32_t display_Convert16(uint16_t texel, int redBits, int greenBits, int blueBits, int alphaBits)
{
    tRasterInfo info;
    ColorInfo dest;
    tVBuffer* pBuf;
    uint32_t out;

    memset(&info, 0, sizeof(info));
    info.width  = 1;
    info.height = 1;
    info.colorInfo.colorMode     = alphaBits ? STDCOLOR_RGBA : STDCOLOR_RGB;
    info.colorInfo.bpp           = 16;
    info.colorInfo.blueBPP       = blueBits;
    info.colorInfo.greenBPP      = greenBits;
    info.colorInfo.redBPP        = redBits;
    info.colorInfo.alphaBPP      = alphaBits;
    info.colorInfo.bluePosShift  = 0;
    info.colorInfo.greenPosShift = blueBits;
    info.colorInfo.redPosShift   = blueBits + greenBits;
    info.colorInfo.alphaPosShift = blueBits + greenBits + redBits;
    pBuf = stdDisplay_VBufferNew(&info, 0, 0);
    memcpy(pBuf->pPixels, &texel, sizeof(texel));

    memset(&dest, 0, sizeof(dest));
    dest.colorMode     = alphaBits ? STDCOLOR_RGBA : STDCOLOR_RGB;
    dest.bpp           = 32;
    dest.redBPP        = dest.greenBPP = dest.blueBPP = 8;
    dest.redPosShift   = 16;
    dest.greenPosShift = 8;
    dest.bluePosShift  = 0;
    if ( alphaBits )
    {
        dest.alphaBPP      = 8;
        dest.alphaPosShift = 24;
    }

    pBuf = stdDisplay_VBufferConvertColorFormat(&dest, pBuf, 0, 0);
    memcpy(&out, pBuf->pPixels, sizeof(out));
    stdDisplay_VBufferFree(pBuf);
    return out;
}

static void display_TestConvert(void)
{
    uint32_t v;
    v = display_Convert16(0xFFFF, 5, 6, 5, 0);
    CHECK(v == 0x00FFFFFF, "565 white to 32: %08X", v);
    v = display_Convert16(0x8410, 5, 6, 5, 0); // 16, 32, 16 of 31, 63, 31
    CHECK(v == 0x00848284, "565 grey to 32: %08X", v);
    v = display_Convert16(0xFFFF, 5, 5, 5, 1);
    CHECK(v == 0xFFFFFFFF, "1555 opaque white to 32: %08X", v);
    v = display_Convert16(0x8000, 5, 5, 5, 1);
    CHECK(v == 0xFF000000, "1555 opaque black: %08X", v);
    v = display_Convert16(0x7FFF, 5, 5, 5, 1);
    CHECK(v == 0x00FFFFFF, "1555 transparent: %08X", v);
    v = display_Convert16(0xF00F, 4, 4, 4, 4);
    CHECK(v == 0xFF0000FF, "4444 opaque blue: %08X", v);
    v = display_Convert16(0x8888, 4, 4, 4, 4);
    CHECK(v == 0x88888888, "4444 half: %08X", v);
}

int main(void)
{
    harness_InstallHandlers();
    setenv("J3D_HEADLESS", "1", 1); // the null build's display device opens only headless
    if ( harness_StartupStd() )
    {
        return 1;
    }

    CHECK(stdDisplay_Startup() == 1, "Startup");
    CHECK(stdDisplay_GetNumDevices() == 1, "one device");
    CHECK(stdDisplay_Open(0) == 1, "Open");

    // P2.4: a windowed mode at an odd size and its back buffer.
    stdDisplay_SetDefaultResolution(BB_WIDTH, BB_HEIGHT);
    CHECK(stdDisplay_SetMode(0, 0, 1) == 0, "SetMode");
    CHECK(!stdDisplay_IsFullscreen(), "windowed");

    uint32_t width = 0, height = 0;
    stdDisplay_GetBackBufferSize(&width, &height);
    CHECK(width == BB_WIDTH && height == BB_HEIGHT, "back buffer %ux%u", width, height);

    StdVideoMode mode;
    CHECK(stdDisplay_GetCurrentVideoMode(&mode) == 0 && mode.rasterInfo.colorInfo.bpp == 32, "current mode 32 bpp");

    void*   pSurface = NULL;
    int32_t pitch    = 0;
    CHECK(stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch) == 0, "LockBackBuffer");
    CHECK(pSurface == stdDisplay_backBuffer.pPixels && pitch == BB_WIDTH * 4, "lock gives the pixels, pitch %d", pitch);
    stdDisplay_UnlockBackBuffer();

    // Fills, whole and by rectangle {left, top, width, height}.
    CHECK(stdDisplay_BackBufferFill(0x00112233, NULL) == 0, "BackBufferFill");
    StdRect rect = { 2, 3, 5, 4 };
    CHECK(stdDisplay_VBufferFill(&stdDisplay_backBuffer, 0x00ABCDEF, &rect) == 1, "VBufferFill returns 1, as retail");
    int bFillOk = 1;
    for ( size_t y = 0; y < BB_HEIGHT; ++y )
    {
        for ( size_t x = 0; x < BB_WIDTH; ++x )
        {
            int bInside = x >= 2 && x < 7 && y >= 3 && y < 7;
            if ( display_GetPixel(&stdDisplay_backBuffer, x, y) != (bInside ? 0x00ABCDEFu : 0x00112233u) )
            {
                bFillOk = 0;
            }
        }
    }
    CHECK(bFillOk, "fill rectangle");

    // P2.5: blits.
    display_TestCopy(32);
    display_TestCopy(16);
    display_TestCopy(8);

    CHECK(stdDisplay_EncodeFromRGB565(0xFFFF) == 0x00FFFFFF, "565 white: %08X", stdDisplay_EncodeFromRGB565(0xFFFF));
    CHECK(stdDisplay_EncodeFromRGB565(0xF800) == 0x00FF0000, "565 red: %08X", stdDisplay_EncodeFromRGB565(0xF800));
    CHECK(stdDisplay_EncodeFromRGB565(0x07E0) == 0x0000FF00, "565 green: %08X", stdDisplay_EncodeFromRGB565(0x07E0));
    CHECK(stdDisplay_EncodeFromRGB565(0x001F) == 0x000000FF, "565 blue: %08X", stdDisplay_EncodeFromRGB565(0x001F));
    CHECK(stdDisplay_EncodeFromRGB565(0x0841) == 0x000F080F, "565 low bits: %08X", stdDisplay_EncodeFromRGB565(0x0841));

    // P2.7
    display_TestSaveScreen();

    // Material textures to the renderer's format.
    display_TestConvert();

    // P8.9: the field of view on wide and narrow pictures.
    display_TestCameraShape();

    stdDisplay_Close();
    stdDisplay_Shutdown();

    printf("display: %d failure(s)\n", display_numFailures);
    return display_numFailures;
}
