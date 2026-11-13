/*
 * System-memory VBuffers (PORT-PLAN.md Phase 1, for the null renderer; kept by the Phase 2 display backend).
 *
 * The retail stdDisplay.c has two kinds of VBuffer, told apart by lockRefCount: 0 = a malloc'd pixel block, 1 = a
 * DirectDraw surface. The port only has the first kind, so these are the system-memory branches of the retail
 * functions, unchanged: a buffer is always "locked" (lockSurfRefCount counts nesting) and pPixels never moves.
 * Material and font loading convert and upload through these even when no display is open.
 *
 * stdDisplay_VBufferFill and stdDisplay_VBufferCopy (the BlitMem* paths) arrive with the Phase 2 display.
 */
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdColorApi.h>
#include <std/General/stdMemory.h>
#include <std/Win95/stdDisplay.h>

#include <platform/common/stdDisplay_common.h>

tVBuffer* J3DAPI stdDisplay_VBufferNew(const tRasterInfo* pRasterInfo, int bUseVSurface, int bUseVideoMemory)
{
    (void)bUseVSurface; (void)bUseVideoMemory; // no video surfaces: always the retail system-memory branch

    STD_ASSERTREL((pRasterInfo->colorInfo.bpp % 8) == 0);
    tVBuffer* pVBuffer = (tVBuffer*)STDMALLOC(sizeof(tVBuffer));
    if ( !pVBuffer )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error allocating vbuffer.\n", 0, 0, 0, 0);
        return NULL;
    }

    memset(pVBuffer, 0, sizeof(tVBuffer));
    memcpy(&pVBuffer->rasterInfo, pRasterInfo, sizeof(pVBuffer->rasterInfo));

    unsigned int bpp = (unsigned int)pVBuffer->rasterInfo.colorInfo.bpp / 8;
    pVBuffer->rasterInfo.rowSize  = pVBuffer->rasterInfo.width * bpp;
    pVBuffer->rasterInfo.rowWidth = pVBuffer->rasterInfo.rowSize / bpp;
    pVBuffer->rasterInfo.size     = pVBuffer->rasterInfo.width * pVBuffer->rasterInfo.height * bpp;

    pVBuffer->lockRefCount = 0;
    pVBuffer->bVideoMemory = 0;
    pVBuffer->pPixels      = (uint8_t*)STDMALLOC(pVBuffer->rasterInfo.size);
    if ( !pVBuffer->pPixels )
    {
        stdMemory_Free(pVBuffer); // retail leaked the header here
        return NULL;
    }

    pVBuffer->lockSurfRefCount = 1;
    return pVBuffer;
}

void J3DAPI stdDisplay_VBufferFree(tVBuffer* pVBuffer)
{
    STD_ASSERTREL(pVBuffer != NULL);
    if ( pVBuffer->pPixels )
    {
        stdMemory_Free(pVBuffer->pPixels);
        pVBuffer->pPixels = NULL;
    }
    stdMemory_Free(pVBuffer);
}

int J3DAPI stdDisplay_VBufferLock(tVBuffer* pVBuffer)
{
    STD_ASSERTREL(pVBuffer != NULL);
    if ( pVBuffer == &stdDisplay_backBuffer )
    {
        J3DDisplay_BeforeCpuAccess(0); // a reader of the back buffer sees what the renderer drew (P2.17)
    }
    ++pVBuffer->lockSurfRefCount;
    return 1;
}

int J3DAPI stdDisplay_VBufferUnlock(tVBuffer* pVBuffer)
{
    STD_ASSERTREL(pVBuffer != NULL);
    if ( pVBuffer->lockSurfRefCount )
    {
        --pVBuffer->lockSurfRefCount;
    }
    return 1;
}

int J3DAPI stdDisplay_VBufferSetColorKey(tVBuffer* pVBuffer, int color)
{
    STD_ASSERT(pVBuffer != NULL);
    pVBuffer->colorKey = color;
    return 1;
}

// Port: widening to full range. The port's renderer takes only 32-bit textures (std3D_GetTextureFormat), so the game's
// 16-bit materials (RGB565, ARGB1555, ARGB4444) are converted here, where retail's Direct3D hardware sampled them as
// they were. stdColor_ColorConvertOneRow widens a channel by shifting alone: 5-bit 31 became 0xF8 instead of 0xFF,
// 4-bit alpha 15 became 0xF0, and 1-bit alpha 1 became 0x80, which fails the alpha test (reference 0xA0), so every
// opaque texel of a 1-bit-alpha texture was discarded (Indy's revolver, among others). A channel is now expanded as the
// GPU expands a normalised value, v * 255 / (2^n - 1), rounded. Used when every channel widens and no colour key is set.
static uint32_t stdDisplay_ExpandChannel(uint32_t v, int srcBits, int destBits)
{
    uint32_t srcMax  = (1u << srcBits) - 1;
    uint32_t destMax = (1u << destBits) - 1;
    return (v * destMax + srcMax / 2) / srcMax;
}

static int stdDisplay_IsWidening(const ColorInfo* pDest, const ColorInfo* pSrc)
{
    return pSrc->colorMode != STDCOLOR_PAL && pDest->colorMode != STDCOLOR_PAL
        && (pSrc->bpp == 16 || pSrc->bpp == 32) && pDest->bpp == 32
        && pSrc->redBPP && pSrc->greenBPP && pSrc->blueBPP
        && pDest->redBPP >= pSrc->redBPP && pDest->greenBPP >= pSrc->greenBPP && pDest->blueBPP >= pSrc->blueBPP
        && (!pSrc->alphaBPP || pDest->alphaBPP >= pSrc->alphaBPP);
}

static void stdDisplay_WidenRow(uint8_t* pDestRow, const ColorInfo* pDest, const uint8_t* pSrcRow, const ColorInfo* pSrc, int width)
{
    for ( int i = 0; i < width; ++i )
    {
        uint32_t pixel;
        uint32_t out;
        if ( pSrc->bpp == 16 )
        {
            uint16_t v;
            memcpy(&v, pSrcRow + 2 * i, sizeof(v));
            pixel = v;
        }
        else
        {
            memcpy(&pixel, pSrcRow + 4 * i, sizeof(pixel));
        }

        out = stdDisplay_ExpandChannel((pixel >> pSrc->redPosShift) & ((1u << pSrc->redBPP) - 1), pSrc->redBPP, pDest->redBPP) << pDest->redPosShift;
        out |= stdDisplay_ExpandChannel((pixel >> pSrc->greenPosShift) & ((1u << pSrc->greenBPP) - 1), pSrc->greenBPP, pDest->greenBPP) << pDest->greenPosShift;
        out |= stdDisplay_ExpandChannel((pixel >> pSrc->bluePosShift) & ((1u << pSrc->blueBPP) - 1), pSrc->blueBPP, pDest->blueBPP) << pDest->bluePosShift;
        if ( pSrc->alphaBPP )
        {
            out |= stdDisplay_ExpandChannel((pixel >> pSrc->alphaPosShift) & ((1u << pSrc->alphaBPP) - 1), pSrc->alphaBPP, pDest->alphaBPP) << pDest->alphaPosShift;
        }

        memcpy(pDestRow + 4 * i, &out, sizeof(out));
    }
}

tVBuffer* J3DAPI stdDisplay_VBufferConvertColorFormat(const ColorInfo* pDesiredColorFormat, tVBuffer* pSrc, int bColorKey, uint32_t colorKey)
{
    STD_ASSERTREL(pSrc != NULL);
    if ( memcmp(pDesiredColorFormat, &pSrc->rasterInfo.colorInfo, sizeof(ColorInfo)) == 0 )
    {
        return pSrc;
    }

    if ( pSrc->rasterInfo.colorInfo.colorMode == STDCOLOR_PAL && pDesiredColorFormat->colorMode == STDCOLOR_PAL )
    {
        return pSrc;
    }

    const ColorInfo* pSrcColorFormat = &pSrc->rasterInfo.colorInfo;
    STD_ASSERTREL(pSrcColorFormat->colorMode != STDCOLOR_PAL);
    STD_ASSERTREL(pDesiredColorFormat->colorMode != STDCOLOR_PAL);

    tVBuffer* pDest = pSrc;
    if ( pSrc->rasterInfo.colorInfo.bpp != pDesiredColorFormat->bpp )
    {
        tRasterInfo destRaster = pSrc->rasterInfo;
        destRaster.colorInfo = *pDesiredColorFormat;
        pDest = stdDisplay_VBufferNew(&destRaster, 0, 0);
        if ( !pDest )
        {
            stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Unable to allocate memory for new tVBuffer", 0, 0, 0, 0);
            return NULL;
        }
    }

    stdDisplay_VBufferLock(pSrc);
    stdDisplay_VBufferLock(pDest);
    for ( int y = 0; y < (int)pDest->rasterInfo.height; ++y )
    {
        const uint8_t* pSrcRow = &pSrc->pPixels[y * pSrc->rasterInfo.rowSize];
        uint8_t* pDestRow      = &pDest->pPixels[y * pDest->rasterInfo.rowSize];
        if ( !bColorKey && pDest != pSrc && stdDisplay_IsWidening(pDesiredColorFormat, pSrcColorFormat) )
        {
            stdDisplay_WidenRow(pDestRow, pDesiredColorFormat, pSrcRow, pSrcColorFormat, (int)pDest->rasterInfo.width);
        }
        else
        {
            stdColor_ColorConvertOneRow(pDestRow, pDesiredColorFormat, pSrcRow, pSrcColorFormat, pDest->rasterInfo.width, bColorKey, colorKey);
        }
    }
    stdDisplay_VBufferUnlock(pSrc);
    stdDisplay_VBufferUnlock(pDest);

    pDest->rasterInfo.colorInfo = *pDesiredColorFormat;
    if ( pDest != pSrc )
    {
        stdDisplay_VBufferFree(pSrc);
    }
    return pDest;
}
