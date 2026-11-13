/*
 * The stdDisplay functions every display backend shares (stdDisplay_common.h): the device and mode queries, the
 * closest-mode search, the back buffer (a 32 bpp system-memory VBuffer in every build), the blits, and the DirectDraw
 * entries that have nothing left to do (z-buffer, clippers).
 *
 * The functions follow the retail stdDisplay.c contract (return values, which globals they maintain). Where the retail
 * function had a DirectDraw-surface branch and a system-memory branch, this is the system-memory branch: every
 * VBuffer in the port is in system memory (stdDisplay_vbuffer.c).
 *
 * Not here: the GDI device-context functions and FlipToGDISurface (Phase 6, P6.5) and stdDisplay_Refresh (P2.19), which
 * remain in the generated null file, and the memory queries, which are the backend's.
 */
#include <stdlib.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdBmp.h>
#include <std/General/stdMemory.h>
#include <std/Win95/stdDisplay.h>

#include <platform/common/stdDisplay_common.h>

size_t       stdDisplay_numDevices = 0;
StdVideoMode stdDisplay_aVideoModes[STDDISPLAY_MAXVIDEOMODES];
StdVideoMode stdDisplay_primaryVideoMode;
int          stdDisplay_bFullscreen = 0;

int J3DDisplay_IsHeadless(void)
{
    const char* pValue = getenv("J3D_HEADLESS");
    return pValue && *pValue && *pValue != '0';
}

void J3DDisplay_InitVideoMode(StdVideoMode* pMode, uint32_t width, uint32_t height)
{
    memset(pMode, 0, sizeof(*pMode));
    pMode->rasterInfo.width                   = width;
    pMode->rasterInfo.height                  = height;
    pMode->rasterInfo.rowSize                 = (size_t)width * 4;
    pMode->rasterInfo.rowWidth                = width;
    pMode->rasterInfo.size                    = (size_t)width * height * 4;
    pMode->rasterInfo.colorInfo.colorMode     = STDCOLOR_RGB;
    pMode->rasterInfo.colorInfo.bpp           = 32;
    pMode->rasterInfo.colorInfo.redBPP        = 8;
    pMode->rasterInfo.colorInfo.greenBPP      = 8;
    pMode->rasterInfo.colorInfo.blueBPP       = 8;
    pMode->rasterInfo.colorInfo.redPosShift   = 16;
    pMode->rasterInfo.colorInfo.greenPosShift = 8;
    pMode->rasterInfo.colorInfo.bluePosShift  = 0;
    // The *PosShiftRight fields stay 0: retail computes stdCalcBitPos(255 / 255) for an 8-bit channel.

    // retail stdDisplay_SetAspectRatio
    if ( (width == 320 && height == 200) || (width == 320 && height == 400) || (width == 640 && height == 400) )
    {
        pMode->aspectRatio = 0.75f;
    }
    else
    {
        pMode->aspectRatio = 1.0f;
    }
}

int J3DDisplay_VideoModeCompare(const void* pVoid1, const void* pVoid2)
{
    // retail stdDisplay_VideoModeCompare, which subtracted the unsigned values; compared here so no difference can wrap
    const StdVideoMode* pMode1 = (const StdVideoMode*)pVoid1;
    const StdVideoMode* pMode2 = (const StdVideoMode*)pVoid2;

    if ( pMode1->rasterInfo.colorInfo.bpp != pMode2->rasterInfo.colorInfo.bpp )
    {
        return pMode1->rasterInfo.colorInfo.bpp < pMode2->rasterInfo.colorInfo.bpp ? -1 : 1;
    }
    if ( pMode1->rasterInfo.width != pMode2->rasterInfo.width )
    {
        return pMode1->rasterInfo.width < pMode2->rasterInfo.width ? -1 : 1;
    }
    if ( pMode1->rasterInfo.height != pMode2->rasterInfo.height )
    {
        return pMode1->rasterInfo.height < pMode2->rasterInfo.height ? -1 : 1;
    }
    return 0;
}

int J3DDisplay_CreateBackBuffer(const StdVideoMode* pMode)
{
    // A system-memory VBuffer (lockRefCount 0, see stdDisplay_vbuffer.c) in the mode's format.
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));
    stdDisplay_backBuffer.rasterInfo = pMode->rasterInfo;
    stdDisplay_backBuffer.pPixels    = (uint8_t*)STDMALLOC(stdDisplay_backBuffer.rasterInfo.size);
    if ( !stdDisplay_backBuffer.pPixels )
    {
        memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));
        return 1;
    }
    stdDisplay_backBuffer.lockSurfRefCount = 1;

    // There is no separate front buffer; it describes the same raster and owns no pixels.
    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    stdDisplay_frontBuffer.rasterInfo = stdDisplay_backBuffer.rasterInfo;
    return 0;
}

void J3DAPI stdDisplay_ClearMode(void)
{
    if ( stdDisplay_bModeSet && stdDisplay_backBuffer.pPixels )
    {
        stdMemory_Free(stdDisplay_backBuffer.pPixels);
    }

    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));
    stdDisplay_pCurVideoMode = NULL;
    stdDisplay_bModeSet      = 0;
}

size_t J3DAPI stdDisplay_GetNumDevices(void)
{
    return stdDisplay_numDevices;
}

int J3DAPI stdDisplay_GetDevice(size_t deviceNum, StdDisplayDevice* pDest)
{
    if ( deviceNum < stdDisplay_numDevices )
    {
        memcpy(pDest, &stdDisplay_aDisplayDevices[deviceNum], sizeof(StdDisplayDevice));
        return 0;
    }

    return 1;
}

int J3DAPI stdDisplay_GetCurrentDevice(StdDisplayDevice* pDevice)
{
    // Retail copies device stdDisplay_curDevice whenever any device exists; the port keeps no index, so it reports
    // the open device and fails when none is open.
    if ( stdDisplay_pCurDevice )
    {
        memcpy(pDevice, stdDisplay_pCurDevice, sizeof(StdDisplayDevice));
        return 0;
    }

    return 1;
}

size_t J3DAPI stdDisplay_GetNumVideoModes(void)
{
    return stdDisplay_numVideoModes;
}

int J3DAPI stdDisplay_GetVideoMode(size_t modeNum, StdVideoMode* pDestMode)
{
    if ( modeNum < stdDisplay_numVideoModes )
    {
        memcpy(pDestMode, &stdDisplay_aVideoModes[modeNum], sizeof(StdVideoMode));
        return 0;
    }

    return 1;
}

int J3DAPI stdDisplay_GetCurrentVideoMode(StdVideoMode* pDisplayMode)
{
    if ( stdDisplay_pCurVideoMode )
    {
        memcpy(pDisplayMode, stdDisplay_pCurVideoMode, sizeof(StdVideoMode));
        return 0;
    }

    return 1;
}

// The retail function (linker-discarded in the retail exe, body from the debug build), unchanged: scores each mode on
// colour mode, bpp, width, height and channel sizes in that order, and returns the first perfect match or the best.
size_t J3DAPI stdDisplay_FindClosestMode(const StdVideoMode* pMatch)
{
    if ( !stdDisplay_numVideoModes )
    {
        return 0;
    }

    int    closestScore = 0;
    size_t bestMode     = 0;
    for ( size_t i = 0; i < stdDisplay_numVideoModes; ++i )
    {
        const StdVideoMode* pMode = &stdDisplay_aVideoModes[i];
        int curScore = 0;
        if ( pMode->rasterInfo.colorInfo.colorMode == pMatch->rasterInfo.colorInfo.colorMode )
        {
            ++curScore;
            if ( pMode->rasterInfo.colorInfo.bpp == pMatch->rasterInfo.colorInfo.bpp )
            {
                ++curScore;
                if ( pMode->rasterInfo.width == pMatch->rasterInfo.width )
                {
                    ++curScore;
                    if ( pMode->rasterInfo.height == pMatch->rasterInfo.height )
                    {
                        ++curScore;
                        if ( pMatch->rasterInfo.colorInfo.colorMode != STDCOLOR_RGB
                            || (pMode->rasterInfo.colorInfo.redBPP == pMatch->rasterInfo.colorInfo.redBPP
                                && pMode->rasterInfo.colorInfo.greenBPP == pMatch->rasterInfo.colorInfo.greenBPP
                                && pMode->rasterInfo.colorInfo.blueBPP == pMatch->rasterInfo.colorInfo.blueBPP) )
                        {
                            stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "Found a perfect mode match #%d!\n", (int)i, 0, 0, 0);
                            return i;
                        }
                    }
                }
            }
        }

        if ( curScore > closestScore )
        {
            bestMode     = i;
            closestScore = curScore;
        }
    }

    stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "Settling for a closest match #%d..\n", (int)bestMode, 0, 0, 0);
    return bestMode;
}

void J3DAPI stdDisplay_SetDefaultResolution(uint32_t width, uint32_t height)
{
    // Retail stores width and height into the windowed mode and keeps its other fields; here the whole mode is
    // rebuilt, so its row size and byte size match the new size.
    if ( width && height )
    {
        J3DDisplay_InitVideoMode(&stdDisplay_primaryVideoMode, width, height);
    }
}

int J3DAPI stdDisplay_CanRenderWindowed(void)
{
    return 1;
}

int J3DAPI stdDisplay_IsFullscreen(void)
{
    return stdDisplay_bFullscreen;
}

void J3DAPI stdDisplay_GetBackBufferSize(uint32_t* pWidth, uint32_t* pHeight)
{
    *pWidth  = stdDisplay_backBuffer.rasterInfo.width;
    *pHeight = stdDisplay_backBuffer.rasterInfo.height;
}

// The system-memory branch of the retail function for every pixel size. pRect is {left, top, width, height}.
int J3DAPI stdDisplay_VBufferFill(tVBuffer* pVBuffer, uint32_t dwFillColor, const StdRect* pRect)
{
    STD_ASSERTREL(pVBuffer != NULL);
    if ( !pVBuffer->pPixels )
    {
        return 1;
    }

    if ( pVBuffer == &stdDisplay_backBuffer )
    {
        J3DDisplay_SyncBackBuffer(pRect == NULL); // a fill after a scene lands on it (P2.17)
    }

    const size_t bytesPerPixel = pVBuffer->rasterInfo.colorInfo.bpp / 8;
    size_t left = 0, top = 0, width = pVBuffer->rasterInfo.width, height = pVBuffer->rasterInfo.height;
    if ( pRect )
    {
        left   = (size_t)pRect->left;
        top    = (size_t)pRect->top;
        width  = (size_t)pRect->right;
        height = (size_t)pRect->bottom;
    }

    for ( size_t y = 0; y < height; ++y )
    {
        uint8_t* pRow = pVBuffer->pPixels + (top + y) * pVBuffer->rasterInfo.rowSize + left * bytesPerPixel;
        if ( dwFillColor == 0 )
        {
            memset(pRow, 0, width * bytesPerPixel);
            continue;
        }

        for ( size_t x = 0; x < width; ++x )
        {
            memcpy(pRow + x * bytesPerPixel, &dwFillColor, bytesPerPixel); // little-endian: the low bytes are the pixel
        }
    }
    return 1; // retail returns 1 once it has filled
}

int J3DAPI stdDisplay_ClearRect(tVBuffer* pVBuffer, int fillColor, const StdRect* pRect)
{
    stdDisplay_VBufferFill(pVBuffer, (uint32_t)fillColor, pRect);
    return 1; // retail returns 1 once the blit has finished
}

int J3DAPI stdDisplay_BackBufferFill(uint32_t color, const StdRect* lpRect)
{
    if ( !stdDisplay_bModeSet )
    {
        return 1;
    }

    // 0 on success: retail returned ColorFillSurface's result, which JonesDisplay_Startup checks.
    stdDisplay_VBufferFill(&stdDisplay_backBuffer, color, lpRect);
    return 0;
}

int J3DAPI stdDisplay_SaveScreen(const char* pFilename)
{
    return stdBmp_WriteVBuffer(pFilename, &stdDisplay_backBuffer); // it locks the back buffer, which syncs it (P2.17)
}

int J3DAPI stdDisplay_LockBackBuffer(void** ppSurface, uint32_t* pWidth, uint32_t* pHeight, int32_t* pPitch)
{
    if ( stdDisplay_bOpen && stdDisplay_bModeSet )
    {
        J3DDisplay_SyncBackBuffer(0); // the CPU is about to read or write the frame on screen (P2.17)
        *pWidth    = stdDisplay_backBuffer.rasterInfo.width;
        *pHeight   = stdDisplay_backBuffer.rasterInfo.height;
        *pPitch    = (int32_t)stdDisplay_backBuffer.rasterInfo.rowSize;
        *ppSurface = stdDisplay_backBuffer.pPixels;
        return 0;
    }

    return 1;
}

void J3DAPI stdDisplay_UnlockBackBuffer(void)
{
}

/* ---- Blits (P2.5) --------------------------------------------------------------------------------------------------- */

// Retail stdDisplay_BlitSysToSys, the system-memory to system-memory path, for every pixel size it handled (8, 16, 24
// and 32 bpp; the colour key only for 8, 16 and 32, as retail). pRect is the source rectangle {left, top, width,
// height}, or NULL for the whole source; (x, y) is where it lands in pDst.
//
// Retail clipped against the destination only, in unsigned arithmetic: a rectangle starting left of or above the
// destination with x + width still negative passed the "entirely outside" test and was read and written out of
// bounds. Here the clip is signed, and the rectangle is also clipped to the source, so no pixel outside either buffer
// is touched; every copy retail performed in bounds is performed the same way.
static void stdDisplay_BlitSysToSys(tVBuffer* pDst, const tVBuffer* pSrc, int x, int y, const StdRect* pRect, int bAlpha)
{
    int64_t srcX = 0, srcY = 0;
    int64_t width  = pSrc->rasterInfo.width;
    int64_t height = pSrc->rasterInfo.height;
    if ( pRect )
    {
        srcX   = pRect->left;
        srcY   = pRect->top;
        width  = pRect->right;
        height = pRect->bottom;
    }

    int64_t dstX = x, dstY = y;

    // Clip to the source.
    if ( srcX < 0 ) { width  += srcX; dstX -= srcX; srcX = 0; }
    if ( srcY < 0 ) { height += srcY; dstY -= srcY; srcY = 0; }
    if ( srcX + width > (int64_t)pSrc->rasterInfo.width )   { width  = (int64_t)pSrc->rasterInfo.width - srcX; }
    if ( srcY + height > (int64_t)pSrc->rasterInfo.height ) { height = (int64_t)pSrc->rasterInfo.height - srcY; }

    // Clip to the destination.
    if ( dstX < 0 ) { width  += dstX; srcX -= dstX; dstX = 0; }
    if ( dstY < 0 ) { height += dstY; srcY -= dstY; dstY = 0; }
    if ( dstX + width > (int64_t)pDst->rasterInfo.width )   { width  = (int64_t)pDst->rasterInfo.width - dstX; }
    if ( dstY + height > (int64_t)pDst->rasterInfo.height ) { height = (int64_t)pDst->rasterInfo.height - dstY; }

    if ( width <= 0 || height <= 0 || !pSrc->pPixels || !pDst->pPixels )
    {
        return;
    }

    const size_t bytesPerPixel = pSrc->rasterInfo.colorInfo.bpp / 8;
    const size_t rowBytes      = (size_t)width * bytesPerPixel;
    const uint8_t* pSrcRow = pSrc->pPixels + (size_t)srcY * pSrc->rasterInfo.rowSize + (size_t)srcX * bytesPerPixel;
    uint8_t*       pDstRow = pDst->pPixels + (size_t)dstY * pDst->rasterInfo.rowSize + (size_t)dstX * bytesPerPixel;

    const int bKeyed = (bAlpha & 1) && bytesPerPixel != 3; // retail had no keyed 24 bpp path
    for ( int64_t row = 0; row < height; ++row )
    {
        if ( !bKeyed )
        {
            memcpy(pDstRow, pSrcRow, rowBytes);
        }
        else if ( bytesPerPixel == 4 )
        {
            const uint32_t* pS = (const uint32_t*)pSrcRow;
            uint32_t*       pD = (uint32_t*)pDstRow;
            for ( int64_t col = 0; col < width; ++col )
            {
                if ( pS[col] != (uint32_t)pSrc->colorKey )
                {
                    pD[col] = pS[col];
                }
            }
        }
        else if ( bytesPerPixel == 2 )
        {
            const uint16_t* pS = (const uint16_t*)pSrcRow;
            uint16_t*       pD = (uint16_t*)pDstRow;
            for ( int64_t col = 0; col < width; ++col )
            {
                if ( pS[col] != (uint16_t)pSrc->colorKey )
                {
                    pD[col] = pS[col];
                }
            }
        }
        else
        {
            for ( int64_t col = 0; col < width; ++col )
            {
                if ( pSrcRow[col] != (uint8_t)pSrc->colorKey )
                {
                    pDstRow[col] = pSrcRow[col];
                }
            }
        }

        pSrcRow += pSrc->rasterInfo.rowSize;
        pDstRow += pDst->rasterInfo.rowSize;
    }
}

int J3DAPI stdDisplay_VBufferCopy(tVBuffer* pDst, tVBuffer* pSrc, unsigned int blitX, int blitY, const StdRect* pRect, int bAlpha)
{
    STD_ASSERT(pDst != NULL);
    STD_ASSERT(pSrc != NULL);

    if ( pDst == &stdDisplay_backBuffer || pSrc == &stdDisplay_backBuffer )
    {
        J3DDisplay_SyncBackBuffer(0); // the back buffer as the frame on screen (P2.17)
    }

    // Retail dispatched on the two buffers' kind to four Blit* helpers; with no DirectDraw surfaces only the
    // system-memory pair remains. blitX is unsigned in the retail signature and passed on as int, as retail did.
    stdDisplay_BlitSysToSys(pDst, pSrc, (int)blitX, blitY, pRect, bAlpha);
    return 1;
}

// The retail function, unchanged: expands an RGB565 pixel to 8 bits a channel (replicating the low bit, so full
// intensity stays full) and packs it in the current video mode's format.
uint32_t J3DAPI stdDisplay_EncodeFromRGB565(uint16_t pixel)
{
    STD_ASSERTREL(stdDisplay_pCurVideoMode != NULL);
    const ColorInfo* pCI = &stdDisplay_pCurVideoMode->rasterInfo.colorInfo;

    uint8_t red   = (uint8_t)(((pixel >> 11) & 0x1F) << 3);
    uint8_t green = (uint8_t)(((pixel >> 5) & 0x3F) << 2);
    uint8_t blue  = (uint8_t)((pixel & 0x1F) << 3);
    if ( red & 8 )
    {
        red |= 7;
    }
    if ( green & 4 )
    {
        green |= 3;
    }
    if ( blue & 8 )
    {
        blue |= 7;
    }

    return ((uint32_t)(red >> pCI->redPosShiftRight) << pCI->redPosShift)
        | ((uint32_t)(green >> pCI->greenPosShiftRight) << pCI->greenPosShift)
        | ((uint32_t)(blue >> pCI->bluePosShiftRight) << pCI->bluePosShift);
}

/* ---- DirectDraw entries with nothing left to do (P2.8) ----------------------------------------------------------- */

int J3DAPI stdDisplay_CreateZBuffer(LPDDPIXELFORMAT pPixelFormat, int bSystemMemory)
{
    // Retail created a DirectDraw z-buffer surface and attached it to the back buffer for Direct3D. The Metal renderer
    // owns its depth texture (P2.9), so there is nothing to create; success.
    (void)pPixelFormat; (void)bSystemMemory;
    return 0;
}

int J3DAPI stdDisplay_SetBufferClipper(int bFrontBuffer)
{
    // Retail attached a window clipper to a fullscreen surface so GDI dialogs drew over it, and returned 1 when it had
    // set one (the caller then removes it). The port's dialogs are drawn by the game (Phase 6); no clipper is set.
    (void)bFrontBuffer;
    return 0;
}

HRESULT J3DAPI stdDisplay_RemoveBufferClipper(int bFrontBuffer)
{
    (void)bFrontBuffer;
    return S_OK;
}

