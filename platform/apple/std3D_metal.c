/*
 * std3D over Metal (PORT-PLAN.md phase 2B): the apple build's renderer, in place of the generated std3D_null.c. This
 * file is the engine-facing half, in C; the GPU work is in render_metal.m (render_metal.h).
 *
 * P2.9, the skeleton:
 *   - one 3D device, the default Metal device, described as a hardware (HAL) device with a z-buffer, so the game's
 *     device search (JonesDevDialog, JonesDisplay) picks it; Startup, Shutdown, GetNumDevices, GetAllDevices
 *   - Open and Close: the scene and depth textures at the back buffer's size, the vertex limit, the memory figures
 *   - StartScene, EndScene and ClearZBuffer: one command buffer per scene, the depth cleared on request
 *   - the display environment built as retail built it (BuildDisplayEnvironment, FreeDisplayEnvironment)
 *   - the texture formats: 32 bpp BGRA, with alpha for the alpha formats and without for RGB, as the null backend
 *     reported them, which the material loader already converts to
 *
 * P2.11: DrawRenderList, DrawLineStrip and DrawPointList.
 *
 * P2.12: textures and the texture cache, the mipmap filter.
 *
 * Not yet, and doing nothing until their step: render states (P2.13), fog (P2.14), the projection
 * (P2.15) and wireframe (P2.16). Each returns what the null backend returned.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>
#include <std/Win95/std3D.h>
#include <std/Win95/stdDisplay.h>

#include <platform/apple/render_metal.h>

#define STD3D_DEFAULT_MAX_VERTICES 512 // retail std3D.c: used when the device reports no limit

// rdroid's vertices go to the renderer as they are: the two layouts must match.
_Static_assert(sizeof(J3DRenderVertex) == sizeof(D3DTLVERTEX), "J3DRenderVertex is not D3DTLVERTEX");
_Static_assert(offsetof(J3DRenderVertex, rhw) == offsetof(D3DTLVERTEX, rhw), "J3DRenderVertex.rhw");
_Static_assert(offsetof(J3DRenderVertex, color) == offsetof(D3DTLVERTEX, color), "J3DRenderVertex.color");
_Static_assert(offsetof(J3DRenderVertex, specular) == offsetof(D3DTLVERTEX, specular), "J3DRenderVertex.specular");
_Static_assert(offsetof(J3DRenderVertex, tu) == offsetof(D3DTLVERTEX, tu), "J3DRenderVertex.tu");
_Static_assert(offsetof(J3DRenderVertex, tv) == offsetof(D3DTLVERTEX, tv), "J3DRenderVertex.tv");

float  std3D_fogDensity;
size_t std3D_maxVertices;

static int      std3D_bStartup = 0;
static int      std3D_bOpen    = 0;
static size_t   std3D_numDevices = 0;
static Device3D std3D_aDevices[1];
static size_t   std3D_frameCount = 0; // retail's frame counter for the texture cache
static Device3D* std3D_pCurDevice = NULL;

// Render state, as retail kept it (std3D_InitRenderState, SetRenderState, SetMipmapFilter).
static Std3DRenderState      std3D_renderState  = 0;
static Std3DMipmapFilterType std3D_mipmapFilter = (Std3DMipmapFilterType)-1;

// Retail's texture cache: a list of the textures loaded "into video memory", least recently used first.
static tSystemTexture* std3D_pFirstTexCache    = NULL;
static tSystemTexture* std3D_pLastTexCache     = NULL;
static size_t          std3D_numCachedTextures = 0;

static void std3D_AddTextureToCacheList(tSystemTexture* pTexture);
static void std3D_RemoveTextureFromCacheList(tSystemTexture* pCacheTexture);
static void std3D_UpdateSampler(void);

/* ---- Devices -------------------------------------------------------------------------------------------------------- */

int J3DAPI std3D_Startup(void)
{
    STD_ASSERTREL(std3D_bStartup == 0);
    memset(std3D_aDevices, 0, sizeof(std3D_aDevices));
    std3D_numDevices = 0;

    char aName[96];
    J3DRender_GetDeviceName(aName, sizeof(aName));
    if ( !aName[0] )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "No Metal device.\n", 0, 0, 0, 0);
        return 0;
    }

    Device3D* pDevice = &std3D_aDevices[0];
    pDevice->caps.bHAL                         = 1;
    pDevice->caps.bTexturePerspectiveSupported = 1;
    pDevice->caps.hasZBuffer                   = 1;
    pDevice->caps.bColorkeyTextureSupported    = 0; // colour keys become alpha when textures are converted
    pDevice->caps.bAlphaTextureSupported       = 1;
    pDevice->caps.bStippledShadeSupported      = 0;
    pDevice->caps.bAlphaBlendSupported         = 1;
    pDevice->caps.bSqareOnlyTexture            = 0;
    pDevice->caps.minTexWidth                  = 1;
    pDevice->caps.minTexHeight                 = 1;
    pDevice->caps.maxTexWidth                  = 16384; // every Apple GPU
    pDevice->caps.maxTexHeight                 = 16384;
    pDevice->caps.maxVertexCount               = 0;     // no limit: STD3D_DEFAULT_MAX_VERTICES applies, as retail
    strcpy(pDevice->deviceName, "Metal");
    // The game picks the 3D device whose description contains "HAL" (JonesDevDialog_LoadSettings).
    snprintf(pDevice->deviceDescription, sizeof(pDevice->deviceDescription), "Metal HAL: %s", aName);
    std3D_numDevices = 1;

    std3D_bStartup = 1;
    return 1;
}

void J3DAPI std3D_Shutdown(void)
{
    if ( std3D_bOpen )
    {
        std3D_Close();
    }

    std3D_numDevices = 0;
    memset(std3D_aDevices, 0, sizeof(std3D_aDevices));
    std3D_bStartup = 0;
}

size_t J3DAPI std3D_GetNumDevices(void)
{
    return std3D_numDevices;
}

const Device3D* J3DAPI std3D_GetAllDevices(void)
{
    return std3D_aDevices;
}

void J3DAPI std3D_SetFindAllDevices(int bFindAll)
{
    (void)bFindAll; // retail listed software rasterisers too when set; there is only the one device
}

int J3DAPI std3D_Open(size_t deviceNum)
{
    STD_ASSERTREL(std3D_bStartup == 1);
    if ( std3D_bOpen )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Warning: System already open!\n", 0, 0, 0, 0);
        return 0;
    }

    if ( deviceNum >= std3D_numDevices )
    {
        return 0;
    }

    // Retail created its z-buffer and Direct3D device on the back buffer surface; here, the scene and depth textures
    // at its size. The display mode must be set first, as retail required.
    if ( !stdDisplay_bModeSet
        || J3DRender_Open(stdDisplay_backBuffer.rasterInfo.width, stdDisplay_backBuffer.rasterInfo.height) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error creating the Metal render targets.\n", 0, 0, 0, 0);
        return 0;
    }

    Device3D* pDevice = &std3D_aDevices[deviceNum];
    std3D_pCurDevice  = pDevice;
    std3D_maxVertices = pDevice->caps.maxVertexCount ? pDevice->caps.maxVertexCount : STD3D_DEFAULT_MAX_VERTICES;
    stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "Max vertices: %d.\n", (int)std3D_maxVertices, 0, 0, 0);

    std3D_frameCount        = 1;
    std3D_numCachedTextures = 0;
    std3D_pFirstTexCache    = NULL;
    std3D_pLastTexCache     = NULL;

    // retail std3D_InitRenderState: perspective, bilinear texel filter, subpixel correction, wrap, the bilinear mipmap
    // filter. The rest of its setup is the fixed pipeline state of render_metal.m.
    std3D_renderState  = STD3D_RS_UNKNOWN_1 | STD3D_RS_TEXFILTER_BILINEAR | STD3D_RS_SUBPIXEL_CORRECTION | STD3D_RS_UNKNOWN_2;
    std3D_mipmapFilter = (Std3DMipmapFilterType)-1;
    std3D_SetMipmapFilter(STD3D_MIPMAPFILTER_BILINEAR);

    if ( stdDisplay_GetTextureMemory(&pDevice->totalMemory, &pDevice->availableMemory) )
    {
        J3DRender_Close();
        std3D_pCurDevice = NULL;
        return 0;
    }

    std3D_bOpen = 1;
    return 1;
}

void J3DAPI std3D_Close(void)
{
    std3D_ResetTextureCache();
    J3DRender_Close();
    std3D_pCurDevice = NULL;
    std3D_bOpen      = 0;
}

/* ---- Texture formats ------------------------------------------------------------------------------------------------ */

// 32 bpp BGRA (D3D's A8R8G8B8, or X8R8G8B8 for RGB): the layout the renderer uploads. No colour key.
void J3DAPI std3D_GetTextureFormat(StdColorFormatType type, ColorInfo* pDest, int* pbColorKeySet, uint32_t* pColorKey)
{
    memset(pDest, 0, sizeof(ColorInfo));
    pDest->colorMode     = STDCOLOR_RGBA;
    pDest->bpp           = 32;
    pDest->redBPP        = 8;
    pDest->greenBPP      = 8;
    pDest->blueBPP       = 8;
    pDest->redPosShift   = 16;
    pDest->greenPosShift = 8;
    pDest->bluePosShift  = 0;
    if ( type != STDCOLOR_FORMAT_RGB )
    {
        pDest->alphaBPP      = 8;
        pDest->alphaPosShift = 24;
    }
    else
    {
        pDest->colorMode = STDCOLOR_RGB;
    }
    *pbColorKeySet = 0;
    *pColorKey     = 0;
}

// The retail body: a pure function of the colour info.
StdColorFormatType J3DAPI std3D_GetColorFormat(const ColorInfo* pCi)
{
    switch ( pCi->alphaBPP )
    {
        case 0:
            return STDCOLOR_FORMAT_RGB;

        case 1:
            return STDCOLOR_FORMAT_RGBA_1BIT_ALPHA;

        default:
            return STDCOLOR_FORMAT_RGBA;
    }
}

size_t J3DAPI std3D_GetNumTextureFormats(void)
{
    return std3D_bOpen ? 2 : 0; // BGRX and BGRA
}

/* ---- Scenes ---------------------------------------------------------------------------------------------------------- */

int J3DAPI std3D_StartScene(void)
{
    ++std3D_frameCount;

    // Retail returned Direct3D's result: 0 (D3D_OK) when the scene began.
    if ( !std3D_bOpen || J3DRender_BeginScene(stdDisplay_backBuffer.pPixels, stdDisplay_backBuffer.rasterInfo.rowSize) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error beginning scene.\n", 0, 0, 0, 0);
        return 1;
    }
    return 0;
}

void J3DAPI std3D_EndScene(void)
{
    J3DRender_EndScene();
}

void J3DAPI std3D_ClearZBuffer(void)
{
    if ( std3D_bOpen )
    {
        J3DRender_ClearDepth();
    }
}

/* ---- Drawing (P2.11) ---------------------------------------------------------------------------------------------- */

// The retail functions' checks and order; the Direct3D draw becomes J3DRender_Draw. rdroid's vertices go as they are.
void J3DAPI std3D_DrawRenderList(LPDIRECT3DTEXTURE2 pTex, Std3DRenderState rdflags, LPD3DTLVERTEX aVerts, size_t numVerts, LPWORD aIndices, size_t numIndices)
{
    if ( numVerts > std3D_maxVertices )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %d > %d maxVertices.\n", (int)numVerts, (int)std3D_maxVertices, 0, 0);
        return;
    }

    std3D_SetRenderState(rdflags);
    J3DRender_SetTexture((J3DRenderTexture*)pTex); // a cached texture's handle (std3D_AddToTextureCache), or none

    if ( J3DRender_Draw(J3DRENDER_TRIANGLES, (const J3DRenderVertex*)aVerts, numVerts, aIndices, numIndices) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error DrawPrim.\n", 0, 0, 0, 0);
    }
}

void J3DAPI std3D_DrawLineStrip(LPD3DTLVERTEX aVerts, size_t numVerts)
{
    if ( numVerts > std3D_maxVertices )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %d > %d maxVertices.\n", (int)numVerts, (int)std3D_maxVertices, 0, 0);
        return;
    }

    if ( J3DRender_Draw(J3DRENDER_LINE_STRIP, (const J3DRenderVertex*)aVerts, numVerts, NULL, 0) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error DrawPrim.\n", 0, 0, 0, 0);
    }
}

void J3DAPI std3D_DrawPointList(LPD3DTLVERTEX aVerts, size_t numVerts)
{
    if ( numVerts > std3D_maxVertices )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %d > %d maxVertices.\n", (int)numVerts, (int)std3D_maxVertices, 0, 0);
        return;
    }

    if ( J3DRender_Draw(J3DRENDER_POINTS, (const J3DRenderVertex*)aVerts, numVerts, NULL, 0) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error DrawPrim.\n", 0, 0, 0, 0);
    }
}

/* ---- Display environment (the retail bodies) ---------------------------------------------------------------------- */

StdDisplayEnvironment* J3DAPI std3D_BuildDisplayEnvironment(void)
{
    StdDisplayEnvironment* pDeviceList = (StdDisplayEnvironment*)STDMALLOC(sizeof(StdDisplayEnvironment));
    if ( !pDeviceList ) // Port: retail wrote through the allocation unchecked
    {
        return NULL;
    }
    memset(pDeviceList, 0, sizeof(StdDisplayEnvironment));

    if ( !stdDisplay_Startup() )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error starting stdDisplay system.\n", 0, 0, 0, 0);
        std3D_FreeDisplayEnvironment(pDeviceList);
        return NULL;
    }

    pDeviceList->numInfos      = stdDisplay_GetNumDevices();
    pDeviceList->aDisplayInfos = NULL;
    if ( pDeviceList->numInfos )
    {
        pDeviceList->aDisplayInfos = (StdDisplayInfo*)STDMALLOC(sizeof(StdDisplayInfo) * pDeviceList->numInfos);
        if ( !pDeviceList->aDisplayInfos ) // Port: unchecked in retail
        {
            std3D_FreeDisplayEnvironment(pDeviceList);
            stdDisplay_Shutdown();
            return NULL;
        }
        memset(pDeviceList->aDisplayInfos, 0, sizeof(StdDisplayInfo) * pDeviceList->numInfos);

        StdDisplayInfo* pCurInfo = pDeviceList->aDisplayInfos;
        for ( size_t deviceNum = 0; deviceNum < pDeviceList->numInfos; ++deviceNum, ++pCurInfo )
        {
            if ( stdDisplay_GetDevice(deviceNum, &pCurInfo->displayDevice) )
            {
                stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error getting stdDisplay device.\n", 0, 0, 0, 0);
                std3D_FreeDisplayEnvironment(pDeviceList);
                stdDisplay_Shutdown();
                return NULL;
            }

            if ( !stdDisplay_Open(deviceNum) )
            {
                stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error opening stdDisplay device.\n", 0, 0, 0, 0);
                std3D_FreeDisplayEnvironment(pDeviceList);
                stdDisplay_Shutdown();
                return NULL;
            }

            pCurInfo->numModes = stdDisplay_GetNumVideoModes();
            pCurInfo->aModes   = NULL;
            if ( pCurInfo->numModes )
            {
                pCurInfo->aModes = (StdVideoMode*)STDMALLOC(sizeof(StdVideoMode) * pCurInfo->numModes);
                StdVideoMode* pCurMode = pCurInfo->aModes;
                for ( size_t modeNum = 0; pCurMode && modeNum < pCurInfo->numModes; ++modeNum )
                {
                    if ( !stdDisplay_GetVideoMode(modeNum, pCurMode) )
                    {
                        ++pCurMode;
                    }
                }

                if ( pCurInfo->displayDevice.bHAL )
                {
                    if ( !std3D_Startup() )
                    {
                        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error starting std3D system.\n", 0, 0, 0, 0);
                        stdDisplay_Close();
                        std3D_FreeDisplayEnvironment(pDeviceList);
                        stdDisplay_Shutdown();
                        return NULL;
                    }

                    pCurInfo->numDevices = std3D_GetNumDevices();
                    pCurInfo->aDevices   = NULL;
                    if ( pCurInfo->numDevices )
                    {
                        pCurInfo->aDevices = (Device3D*)STDMALLOC(sizeof(Device3D) * pCurInfo->numDevices);
                        if ( pCurInfo->aDevices )
                        {
                            memcpy(pCurInfo->aDevices, std3D_GetAllDevices(), sizeof(Device3D) * pCurInfo->numDevices);
                        }
                        else
                        {
                            pCurInfo->numDevices = 0;
                        }
                    }

                    std3D_Shutdown();
                }
                else
                {
                    pCurInfo->numDevices = 0;
                    pCurInfo->aDevices   = NULL;
                }
            }

            stdDisplay_Close();
        }
    }

    stdDisplay_Shutdown();
    return pDeviceList;
}

void J3DAPI std3D_FreeDisplayEnvironment(StdDisplayEnvironment* pEnv)
{
    if ( pEnv->aDisplayInfos )
    {
        StdDisplayInfo* pCurDevice = pEnv->aDisplayInfos;
        for ( size_t i = 0; i < pEnv->numInfos; ++i, ++pCurDevice )
        {
            if ( pCurDevice->aDevices )
            {
                stdMemory_Free(pCurDevice->aDevices);
                pCurDevice->aDevices = NULL;
            }

            if ( pCurDevice->aModes )
            {
                stdMemory_Free(pCurDevice->aModes);
                pCurDevice->aModes = NULL;
            }
        }

        stdMemory_Free(pEnv->aDisplayInfos);
        pEnv->aDisplayInfos = NULL;
    }

    stdMemory_Free(pEnv);
}

/* ---- Textures (P2.12) --------------------------------------------------------------------------------------------- */

// The retail body, with the device's limits (1 to 16384): every size the game uses is valid as it is.
void J3DAPI std3D_GetValidDimensions(uint32_t width, uint32_t height, uint32_t* pOutWidth, uint32_t* pOutHeight)
{
    const Device3DCaps* pCaps = std3D_pCurDevice ? &std3D_pCurDevice->caps : &std3D_aDevices[0].caps;
    if ( !pCaps->maxTexWidth ) // no device started: no limits
    {
        *pOutWidth  = width;
        *pOutHeight = height;
        return;
    }

    uint32_t texWidth  = (uint32_t)STDMATH_CLAMP(width, pCaps->minTexWidth, pCaps->maxTexWidth);
    uint32_t texHeight = (uint32_t)STDMATH_CLAMP(height, pCaps->minTexHeight, pCaps->maxTexHeight);
    if ( pCaps->bSqareOnlyTexture && texWidth != texHeight )
    {
        *pOutHeight = *pOutWidth = J3DMAX(texWidth, texHeight);
    }
    else
    {
        *pOutWidth  = texWidth;
        *pOutHeight = texHeight;
    }
}

// Retail made a system-memory DirectDraw texture with the mip chain and kept it in pD3DSrcTexture; AddToTextureCache
// later loaded it into video memory. On unified memory one Metal texture serves both: it is made and filled here, and
// the cache takes another reference to it. textureSize is the top level's size, as retail computed it.
void J3DAPI std3D_AllocSystemTexture(tSystemTexture* pTexture, tVBuffer** apVBuffers, size_t numMipLevels, StdColorFormatType formatType)
{
    memset(pTexture, 0, sizeof(tSystemTexture));
    if ( !std3D_bOpen || !apVBuffers || !numMipLevels )
    {
        return;
    }

    // Skip top levels larger than the device allows (retail).
    tVBuffer* pVBuffer = *apVBuffers;
    uint32_t  texWidth = 0, texHeight = 0;
    std3D_GetValidDimensions(pVBuffer->rasterInfo.width, pVBuffer->rasterInfo.height, &texWidth, &texHeight);
    while ( numMipLevels > 1 && (pVBuffer->rasterInfo.width > texWidth || pVBuffer->rasterInfo.height > texHeight) )
    {
        --numMipLevels;
        pVBuffer = *++apVBuffers;
        std3D_GetValidDimensions(pVBuffer->rasterInfo.width, pVBuffer->rasterInfo.height, &texWidth, &texHeight);
    }

    const size_t texSize = (size_t)texWidth * texHeight * pVBuffer->rasterInfo.colorInfo.bpp / 8;
    if ( std3D_mipmapFilter == STD3D_MIPMAPFILTER_NONE )
    {
        numMipLevels = 1;
    }

    // Keep the levels that are in the renderer's format and at least the size each mip level needs. Retail rescaled a
    // top level larger than the valid size with a blit; with no size limit below 16384 that never happens.
    size_t numLevels = 0;
    for ( ; numLevels < numMipLevels; ++numLevels )
    {
        const tVBuffer* pLevel = apVBuffers[numLevels];
        const uint32_t  levelWidth  = texWidth >> numLevels ? texWidth >> numLevels : 1;
        const uint32_t  levelHeight = texHeight >> numLevels ? texHeight >> numLevels : 1;
        if ( pLevel->rasterInfo.colorInfo.colorMode == STDCOLOR_PAL )
        {
            stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Can't use paletized textures.\n", 0, 0, 0, 0);
            break;
        }
        if ( pLevel->rasterInfo.colorInfo.bpp != 32 || !pLevel->pPixels || pLevel->rasterInfo.width < levelWidth
            || pLevel->rasterInfo.height < levelHeight )
        {
            break;
        }
    }
    if ( !numLevels || texWidth != pVBuffer->rasterInfo.width || texHeight != pVBuffer->rasterInfo.height )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error: texture %dx%d, %d bpp, is not in the renderer's format.\n",
            (int)pVBuffer->rasterInfo.width, (int)pVBuffer->rasterInfo.height, (int)pVBuffer->rasterInfo.colorInfo.bpp, 0);
        return;
    }

    J3DRenderTexture* pTex = J3DRender_CreateTexture(texWidth, texHeight, numLevels, formatType != STDCOLOR_FORMAT_RGB);
    if ( !pTex )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Done error exit from std3D_AllocSystemTexture.\n", 0, 0, 0, 0);
        return;
    }
    for ( size_t mmNum = 0; mmNum < numLevels; ++mmNum )
    {
        stdDisplay_VBufferLock(apVBuffers[mmNum]);
        J3DRender_UploadTextureLevel(pTex, mmNum, apVBuffers[mmNum]->pPixels, apVBuffers[mmNum]->rasterInfo.rowSize);
        stdDisplay_VBufferUnlock(apVBuffers[mmNum]);
    }

    // What retail's surface description told GetMipMapCount: no mip count when the mipmap filter is off.
    pTexture->ddsd.dwSize        = sizeof(DDSURFACEDESC2);
    pTexture->ddsd.dwWidth       = texWidth;
    pTexture->ddsd.dwHeight      = texHeight;
    if ( std3D_mipmapFilter != STD3D_MIPMAPFILTER_NONE )
    {
        pTexture->ddsd.dwMipMapCount = (DWORD)numLevels; // Port: narrowing cast
    }
    pTexture->pD3DSrcTexture = (LPDIRECT3DTEXTURE2)pTex;
    pTexture->textureSize    = texSize;
}

void J3DAPI std3D_ClearSystemTexture(tSystemTexture* pTex)
{
    if ( pTex->pD3DSrcTexture )
    {
        J3DRender_ReleaseTexture((J3DRenderTexture*)pTex->pD3DSrcTexture);
    }

    if ( pTex->pD3DCachedTex )
    {
        std3D_RemoveTextureFromCacheList(pTex);
        J3DRender_ReleaseTexture((J3DRenderTexture*)pTex->pD3DCachedTex);
    }

    memset(pTex, 0, sizeof(tSystemTexture));
}

// Retail loaded the system texture into a new video-memory texture, purging least recently used ones to make room;
// here the cache takes a reference to the one texture. The cache list and the memory count are kept as retail kept
// them, so PurgeTextureCache and ResetTextureCache behave the same.
void J3DAPI std3D_AddToTextureCache(tSystemTexture* pCacheTexture, StdColorFormatType format)
{
    (void)format; // retail set a colour key on the video texture for keyed formats; the port has none
    STD_ASSERTREL(pCacheTexture);
    if ( !pCacheTexture->pD3DSrcTexture || !std3D_pCurDevice )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "No Source texture.\n", 0, 0, 0, 0);
        pCacheTexture->pD3DCachedTex = NULL;
        pCacheTexture->frameNum      = 0;
        return;
    }

    if ( pCacheTexture->textureSize > std3D_pCurDevice->availableMemory )
    {
        std3D_PurgeTextureCache(pCacheTexture->textureSize);
    }

    J3DRender_RetainTexture((J3DRenderTexture*)pCacheTexture->pD3DSrcTexture);
    pCacheTexture->pD3DCachedTex = pCacheTexture->pD3DSrcTexture;
    pCacheTexture->frameNum      = std3D_frameCount;
    std3D_AddTextureToCacheList(pCacheTexture);
}

size_t J3DAPI std3D_GetMipMapCount(const tSystemTexture* pTexture)
{
    if ( !pTexture || !pTexture->pD3DSrcTexture )
    {
        return 0;
    }
    return pTexture->ddsd.dwMipMapCount; // retail read the source surface's description
}

void J3DAPI std3D_ResetTextureCache(void)
{
    tSystemTexture* pCurTex = std3D_pFirstTexCache;
    while ( pCurTex )
    {
        if ( pCurTex->pD3DCachedTex )
        {
            J3DRender_ReleaseTexture((J3DRenderTexture*)pCurTex->pD3DCachedTex);
            pCurTex->pD3DCachedTex = NULL;
        }

        tSystemTexture* pTex = pCurTex;
        pCurTex = pCurTex->pNextCachedTexture;
        pTex->frameNum           = 0;
        pTex->pNextCachedTexture = NULL;
        pTex->pPrevCachedTexture = NULL;
    }

    std3D_pFirstTexCache    = NULL;
    std3D_pLastTexCache     = NULL;
    std3D_numCachedTextures = 0;
    if ( std3D_pCurDevice )
    {
        std3D_pCurDevice->availableMemory = std3D_pCurDevice->totalMemory;
    }

    J3DRender_SetTexture(NULL);
    std3D_frameCount = 1;
}

void J3DAPI std3D_UpdateFrameCount(tSystemTexture* pTexture)
{
    pTexture->frameNum = std3D_frameCount;
    std3D_RemoveTextureFromCacheList(pTexture);
    std3D_AddTextureToCacheList(pTexture);
}

size_t J3DAPI std3D_FindClosestFormat(const ColorInfo* pMatch)
{
    // Retail searched the device's enumerated formats; the port has two: 0 without alpha, 1 with.
    return pMatch->alphaBPP ? 1 : 0;
}

static void std3D_AddTextureToCacheList(tSystemTexture* pTexture)
{
    if ( !std3D_pFirstTexCache )
    {
        std3D_pFirstTexCache         = std3D_pLastTexCache = pTexture;
        pTexture->pPrevCachedTexture = NULL;
        pTexture->pNextCachedTexture = NULL;
    }
    else
    {
        std3D_pLastTexCache->pNextCachedTexture = pTexture;
        pTexture->pPrevCachedTexture            = std3D_pLastTexCache;
        pTexture->pNextCachedTexture            = NULL;
        std3D_pLastTexCache                     = pTexture;
    }

    ++std3D_numCachedTextures;
    std3D_pCurDevice->availableMemory -= pTexture->textureSize;
}

static void std3D_RemoveTextureFromCacheList(tSystemTexture* pCacheTexture)
{
    if ( pCacheTexture == std3D_pFirstTexCache )
    {
        std3D_pFirstTexCache = pCacheTexture->pNextCachedTexture;
        if ( std3D_pFirstTexCache )
        {
            std3D_pFirstTexCache->pPrevCachedTexture = NULL;
            if ( !std3D_pFirstTexCache->pNextCachedTexture )
            {
                std3D_pLastTexCache = std3D_pFirstTexCache;
            }
        }
        else
        {
            std3D_pLastTexCache = NULL;
        }
    }
    else if ( pCacheTexture == std3D_pLastTexCache )
    {
        std3D_pLastTexCache = pCacheTexture->pPrevCachedTexture;
        std3D_pLastTexCache->pNextCachedTexture = NULL;
    }
    else
    {
        pCacheTexture->pPrevCachedTexture->pNextCachedTexture = pCacheTexture->pNextCachedTexture;
        pCacheTexture->pNextCachedTexture->pPrevCachedTexture = pCacheTexture->pPrevCachedTexture;
    }

    --std3D_numCachedTextures;
    if ( std3D_pCurDevice )
    {
        std3D_pCurDevice->availableMemory += pCacheTexture->textureSize;
    }
    pCacheTexture->pNextCachedTexture = NULL;
    pCacheTexture->pPrevCachedTexture = NULL;
    pCacheTexture->frameNum           = 0;
}

// The retail body: first a single unused texture of exactly the size needed, else unused textures from the least
// recently used until enough is freed. Returns nonzero when anything was purged.
int J3DAPI std3D_PurgeTextureCache(size_t size)
{
    tSystemTexture* pNextCachedTexture = NULL;
    size_t          purgedBytes        = 0;

    for ( tSystemTexture* pCacheTexture = std3D_pFirstTexCache; pCacheTexture && pCacheTexture->frameNum != std3D_frameCount;
          pCacheTexture = pNextCachedTexture )
    {
        pNextCachedTexture = pCacheTexture->pNextCachedTexture;
        if ( pCacheTexture->textureSize == size )
        {
            J3DRender_ReleaseTexture((J3DRenderTexture*)pCacheTexture->pD3DCachedTex);
            pCacheTexture->pD3DCachedTex = NULL;
            std3D_RemoveTextureFromCacheList(pCacheTexture);
            return 1;
        }
    }

    tSystemTexture* pCacheTexture = std3D_pFirstTexCache;
    while ( pCacheTexture && purgedBytes < size )
    {
        pNextCachedTexture = pCacheTexture->pNextCachedTexture;
        if ( pCacheTexture->frameNum != std3D_frameCount )
        {
            J3DRender_ReleaseTexture((J3DRenderTexture*)pCacheTexture->pD3DCachedTex);
            pCacheTexture->pD3DCachedTex = NULL;
            purgedBytes += pCacheTexture->textureSize;
            std3D_RemoveTextureFromCacheList(pCacheTexture);
        }
        pCacheTexture = pNextCachedTexture;
    }

    return purgedBytes > 0;
}

/* ---- Samplers ------------------------------------------------------------------------------------------------------- */

// The texture stage state retail set from the render state and the mipmap filter, as one Metal sampler.
static void std3D_UpdateSampler(void)
{
    J3DRenderMipFilter mip = std3D_mipmapFilter == STD3D_MIPMAPFILTER_TRILINEAR ? J3DRENDER_MIP_LINEAR
                           : std3D_mipmapFilter == STD3D_MIPMAPFILTER_BILINEAR  ? J3DRENDER_MIP_NEAREST
                                                                                : J3DRENDER_MIP_NONE;
    J3DRender_SetSampler((std3D_renderState & STD3D_RS_TEXFILTER_BILINEAR) != 0, (std3D_renderState & STD3D_RS_TEX_CPAMP_U) != 0,
        (std3D_renderState & STD3D_RS_TEX_CPAMP_V) != 0, mip);
}

// Retail fell back from trilinear and bilinear when the device lacked them; Metal has both. 0 on success.
int J3DAPI std3D_SetMipmapFilter(Std3DMipmapFilterType filter)
{
    std3D_mipmapFilter = filter;
    std3D_UpdateSampler();
    return 0;
}

/* ---- Not yet: each returns what the null backend returned until its step ---------------------------------------- */

void J3DAPI std3D_SetRenderState(Std3DRenderState rdflags)
{
    (void)rdflags; // P2.13
}

void J3DAPI std3D_SetWireframeRenderState(void)
{
    // P2.16
}







int J3DAPI std3D_SetProjection(float fov, float nearPlane, float farPlane)
{
    (void)fov; (void)nearPlane; (void)farPlane; // P2.15
    return 0;
}

void J3DAPI std3D_EnableFog(int bEnabled, float density)
{
    (void)bEnabled; (void)density; // P2.14
}

int J3DAPI std3D_SetFog(float red, float green, float blue, float startDepth, float endDepth)
{
    (void)red; (void)green; (void)blue; (void)startDepth; (void)endDepth; // P2.14
    return 0;
}
