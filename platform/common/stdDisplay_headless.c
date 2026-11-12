/*
 * Headless display (PORT-PLAN.md P1.17): a display device that exists only in memory, so the game can open its display,
 * set a mode and run its loop with no window and no renderer.
 *
 * It is off by default: stdDisplay_Open fails with the Phase 0 message unless J3D_HEADLESS is set (jones3d --headless),
 * because a game loop with no window is only useful to tests. When on, there is one device and one video mode, the back
 * buffer is a system-memory VBuffer (32 bpp, the format the null std3D reports for textures), and stdDisplay_Update
 * presents nothing but moves the virtual clock (host_clock.h) by one frame.
 *
 * The functions here follow the retail stdDisplay.c contract (return values, which globals they maintain). The
 * back-buffer half (SetMode's buffer, fill, lock, SaveScreen) is what the Phase 2 display backend keeps; the device and
 * mode half is replaced by SDL3 there.
 *
 * Not here yet: stdDisplay_VBufferCopy (the retail system-memory blits, P2.5) and the GDI device-context functions
 * (Phase 6). They remain in the generated null file.
 */
#include <stdlib.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdBmp.h>
#include <std/General/stdMemory.h>
#include <std/Win95/stdDisplay.h>

#include <platform/common/host_clock.h>

#define HEADLESS_WIDTH  640
#define HEADLESS_HEIGHT 480

// Game time that passes per presented frame on the virtual clock: 30 frames a second, a typical rate for the retail
// game on the hardware it was written for.
#define HEADLESS_FRAME_MSEC 33

static int          stdDisplay_bStartup    = 0;
static int          stdDisplay_bFullscreen = 0;
static StdVideoMode stdDisplay_aVideoModes[1];      // the fullscreen mode list
static StdVideoMode stdDisplay_primaryVideoMode;    // the windowed mode; SetDefaultResolution sizes it

static int stdDisplay_IsHeadless(void)
{
    const char* pValue = getenv("J3D_HEADLESS");
    return pValue && *pValue && *pValue != '0';
}

// 32 bpp BGRA without alpha (D3D's X8R8G8B8), as std3D_GetTextureFormat reports for STDCOLOR_FORMAT_RGB.
static void stdDisplay_InitVideoMode(StdVideoMode* pMode, uint32_t width, uint32_t height)
{
    memset(pMode, 0, sizeof(*pMode));
    pMode->aspectRatio                        = 1.0f;
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
}

int J3DAPI stdDisplay_Startup(void)
{
    if ( stdDisplay_bStartup )
    {
        return 1;
    }

    memset(stdDisplay_aDisplayDevices, 0, sizeof(stdDisplay_aDisplayDevices));
    strcpy(stdDisplay_aDisplayDevices[0].aDeviceName, "Headless display");
    strcpy(stdDisplay_aDisplayDevices[0].aDriverName, "memory");
    stdDisplay_aDisplayDevices[0].bHAL = 1;

    stdDisplay_InitVideoMode(&stdDisplay_aVideoModes[0], HEADLESS_WIDTH, HEADLESS_HEIGHT);
    stdDisplay_InitVideoMode(&stdDisplay_primaryVideoMode, HEADLESS_WIDTH, HEADLESS_HEIGHT);
    stdDisplay_numVideoModes = 1;

    stdDisplay_bStartup = 1;
    return 1;
}

void J3DAPI stdDisplay_Shutdown(void)
{
    if ( stdDisplay_bOpen )
    {
        stdDisplay_Close();
    }

    stdDisplay_numVideoModes = 0;
    stdDisplay_bStartup      = 0;
}

int J3DAPI stdDisplay_Open(size_t deviceNum)
{
    STD_ASSERTREL(stdDisplay_bStartup == 1);
    if ( !stdDisplay_IsHeadless() )
    {
        if ( std_pHS && std_pHS->pErrorPrint )
        {
            std_pHS->pErrorPrint("null platform: stdDisplay_Open: no display backend yet (PORT-PLAN.md Phase 2); "
                                 "set J3D_HEADLESS=1 or pass --headless to run without one\n");
        }
        return 0;
    }

    if ( stdDisplay_bOpen )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Warning: System already open!\n", 0, 0, 0, 0);
        stdDisplay_Close();
    }

    if ( deviceNum > 0 )
    {
        return 0;
    }

    // From here on time moves by a fixed step per presented frame (host_clock.h).
    J3DHost_UseVirtualClock();

    stdDisplay_pCurDevice = &stdDisplay_aDisplayDevices[0];
    stdDisplay_bOpen      = 1;
    return 1;
}

void J3DAPI stdDisplay_Close(void)
{
    if ( stdDisplay_bStartup != 1 )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "stdDisplay_Close when not started.", 0, 0, 0, 0);
        return;
    }

    if ( !stdDisplay_bOpen )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Warning: System already closed!\n", 0, 0, 0, 0);
        return;
    }

    if ( stdDisplay_bModeSet )
    {
        stdDisplay_ClearMode();
    }

    stdDisplay_pCurDevice = NULL;
    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));
    stdDisplay_bOpen = 0;
}

int J3DAPI stdDisplay_SetMode(size_t modeNum, int bFullscreen, size_t numBackBuffers)
{
    (void)numBackBuffers; // nothing is flipped

    if ( !stdDisplay_bOpen )
    {
        return 1;
    }

    if ( bFullscreen && modeNum >= stdDisplay_numVideoModes )
    {
        return 1;
    }

    if ( stdDisplay_bModeSet )
    {
        stdDisplay_ClearMode();
    }

    stdDisplay_pCurVideoMode = bFullscreen ? &stdDisplay_aVideoModes[modeNum] : &stdDisplay_primaryVideoMode;

    // The back buffer: a system-memory VBuffer (lockRefCount 0, see stdDisplay_vbuffer.c) in the mode's format.
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));
    stdDisplay_backBuffer.rasterInfo = stdDisplay_pCurVideoMode->rasterInfo;
    stdDisplay_backBuffer.pPixels    = (uint8_t*)STDMALLOC(stdDisplay_backBuffer.rasterInfo.size);
    if ( !stdDisplay_backBuffer.pPixels )
    {
        stdDisplay_pCurVideoMode = NULL;
        return 1;
    }
    stdDisplay_backBuffer.lockSurfRefCount = 1;

    // There is no separate front buffer; it describes the same raster and owns no pixels.
    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    stdDisplay_frontBuffer.rasterInfo = stdDisplay_backBuffer.rasterInfo;

    stdDisplay_bModeSet    = 1;
    stdDisplay_bFullscreen = bFullscreen;

    stdDisplay_VBufferFill(&stdDisplay_backBuffer, 0, NULL);
    stdDisplay_Update();
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

int J3DAPI stdDisplay_Update(void)
{
    // Nothing to present. A present is where a real display spends a frame's time, so the virtual clock moves here.
    J3DHost_AdvanceClock(HEADLESS_FRAME_MSEC);
    return 0;
}

size_t J3DAPI stdDisplay_GetNumDevices(void)
{
    return stdDisplay_bStartup ? 1 : 0;
}

int J3DAPI stdDisplay_GetDevice(size_t deviceNum, StdDisplayDevice* pDest)
{
    if ( deviceNum >= stdDisplay_GetNumDevices() )
    {
        return 1;
    }

    memcpy(pDest, &stdDisplay_aDisplayDevices[deviceNum], sizeof(StdDisplayDevice));
    return 0;
}

int J3DAPI stdDisplay_GetCurrentDevice(StdDisplayDevice* pDevice)
{
    if ( !stdDisplay_bOpen )
    {
        return 1;
    }

    memcpy(pDevice, stdDisplay_pCurDevice, sizeof(StdDisplayDevice));
    return 0;
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

size_t J3DAPI stdDisplay_FindClosestMode(const StdVideoMode* pMatch)
{
    (void)pMatch;
    return 0; // the only mode
}

void J3DAPI stdDisplay_SetDefaultResolution(uint32_t width, uint32_t height)
{
    if ( width && height )
    {
        stdDisplay_InitVideoMode(&stdDisplay_primaryVideoMode, width, height);
    }
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
    return 0;
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
    return stdDisplay_VBufferFill(&stdDisplay_backBuffer, color, lpRect);
}

int J3DAPI stdDisplay_SaveScreen(const char* pFilename)
{
    return stdBmp_WriteVBuffer(pFilename, &stdDisplay_backBuffer);
}

int J3DAPI stdDisplay_CanRenderWindowed(void)
{
    return 1;
}

int J3DAPI stdDisplay_IsFullscreen(void)
{
    return stdDisplay_bFullscreen;
}

int J3DAPI stdDisplay_LockBackBuffer(void** ppSurface, uint32_t* pWidth, uint32_t* pHeight, int32_t* pPitch)
{
    if ( stdDisplay_bOpen && stdDisplay_bModeSet )
    {
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
