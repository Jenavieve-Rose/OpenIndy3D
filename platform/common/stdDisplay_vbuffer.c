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
        stdColor_ColorConvertOneRow(pDestRow, pDesiredColorFormat, pSrcRow, pSrcColorFormat, pDest->rasterInfo.width, bColorKey, colorKey);
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
