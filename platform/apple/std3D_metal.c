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
 * Not yet, and doing nothing until their step: textures and the texture cache (P2.12), render states and the mipmap filter (P2.13), fog (P2.14), the projection
 * (P2.15) and wireframe (P2.16). Each returns what the null backend returned.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <std/General/std.h>
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
static size_t   std3D_frameCount = 0; // retail's frame counter for the texture cache (P2.12)

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
    std3D_maxVertices = pDevice->caps.maxVertexCount ? pDevice->caps.maxVertexCount : STD3D_DEFAULT_MAX_VERTICES;
    stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "Max vertices: %d.\n", (int)std3D_maxVertices, 0, 0, 0);

    std3D_frameCount = 1;

    if ( stdDisplay_GetTextureMemory(&pDevice->totalMemory, &pDevice->availableMemory) )
    {
        J3DRender_Close();
        return 0;
    }

    std3D_bOpen = 1;
    return 1;
}

void J3DAPI std3D_Close(void)
{
    std3D_ResetTextureCache();
    J3DRender_Close();
    std3D_bOpen = 0;
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
    (void)pTex; // the texture: P2.12

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

/* ---- Not yet: each returns what the null backend returned until its step ---------------------------------------- */

void J3DAPI std3D_SetRenderState(Std3DRenderState rdflags)
{
    (void)rdflags; // P2.13
}

void J3DAPI std3D_SetWireframeRenderState(void)
{
    // P2.16
}

void J3DAPI std3D_AllocSystemTexture(tSystemTexture* pTexture, tVBuffer** apVBuffers, size_t numMipLevels, StdColorFormatType formatType)
{
    (void)pTexture; (void)apVBuffers; (void)numMipLevels; (void)formatType; // P2.12
}

void J3DAPI std3D_GetValidDimensions(uint32_t width, uint32_t height, uint32_t* pOutWidth, uint32_t* pOutHeight)
{
    // No texture size limits below the device's: the requested size is valid (P2.12 revisits).
    *pOutWidth  = width;
    *pOutHeight = height;
}

void J3DAPI std3D_ClearSystemTexture(tSystemTexture* pTex)
{
    (void)pTex; // P2.12
}

int J3DAPI std3D_PurgeTextureCache(size_t size)
{
    (void)size; // P2.12
    return 0;
}

void J3DAPI std3D_AddToTextureCache(tSystemTexture* pCacheTexture, StdColorFormatType format)
{
    (void)pCacheTexture; (void)format; // P2.12
}

void J3DAPI std3D_ResetTextureCache(void)
{
    // P2.12
}

size_t J3DAPI std3D_GetMipMapCount(const tSystemTexture* pTexture)
{
    (void)pTexture; // P2.12
    return 0;
}

void J3DAPI std3D_UpdateFrameCount(tSystemTexture* pTexture)
{
    (void)pTexture; // P2.12
}

size_t J3DAPI std3D_FindClosestFormat(const ColorInfo* pMatch)
{
    (void)pMatch; // P2.12
    return 0;
}

int J3DAPI std3D_SetMipmapFilter(Std3DMipmapFilterType mode)
{
    (void)mode; // P2.13
    return 0;
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
