/*
 * Headless display (PORT-PLAN.md P1.17): the null build's display backend, a device that exists only in memory, so the
 * game can open its display, set a mode and run its loop with no window and no renderer. Not generated: the generator
 * leaves these functions out of stdDisplay_null.c (IMPLEMENTED_ELSEWHERE). The apple build replaces this file with
 * platform/apple/stdDisplay_sdl.c.
 *
 * It is off by default: stdDisplay_Open fails with the Phase 0 message unless J3D_HEADLESS is set (jones3d --headless),
 * because a game loop with no window is only useful to tests. When on, there is one device and one 640x480 video mode,
 * and stdDisplay_Update presents nothing but moves the virtual clock (host_clock.h) by one frame.
 *
 * This file holds the backend's half of stdDisplay (stdDisplay_common.h), with the memory queries; the queries, the
 * back buffer and the blits are in platform/common/stdDisplay_common.c.
 */
#include <string.h>

#include <std/General/std.h>
#include <std/Win95/stdDisplay.h>

#include <platform/common/host_clock.h>
#include <platform/common/host_record.h>
#include <platform/common/stdDisplay_common.h>

#define HEADLESS_WIDTH  640
#define HEADLESS_HEIGHT 480

static int stdDisplay_bStartup = 0;

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
    stdDisplay_numDevices = 1;

    J3DDisplay_InitVideoMode(&stdDisplay_aVideoModes[0], HEADLESS_WIDTH, HEADLESS_HEIGHT);
    J3DDisplay_InitVideoMode(&stdDisplay_primaryVideoMode, HEADLESS_WIDTH, HEADLESS_HEIGHT);
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
    stdDisplay_numDevices    = 0;
    stdDisplay_bStartup      = 0;
}

int J3DAPI stdDisplay_Open(size_t deviceNum)
{
    STD_ASSERTREL(stdDisplay_bStartup == 1);
    if ( !J3DDisplay_IsHeadless() )
    {
        if ( std_pHS && std_pHS->pErrorPrint )
        {
            std_pHS->pErrorPrint("null platform: stdDisplay_Open: no display backend in a null build; "
                                 "set J3D_HEADLESS=1 or pass --headless to run without one\n");
        }
        return 0;
    }

    if ( stdDisplay_bOpen )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Warning: System already open!\n", 0, 0, 0, 0);
        stdDisplay_Close();
    }

    if ( deviceNum >= stdDisplay_numDevices )
    {
        return 0;
    }

    // From here on time moves by a fixed step per presented frame (host_clock.h).
    J3DHost_UseVirtualClock();

    stdDisplay_pCurDevice = &stdDisplay_aDisplayDevices[deviceNum];
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
    if ( J3DDisplay_CreateBackBuffer(stdDisplay_pCurVideoMode) )
    {
        stdDisplay_pCurVideoMode = NULL;
        return 1;
    }

    stdDisplay_bModeSet    = 1;
    stdDisplay_bFullscreen = bFullscreen;

    stdDisplay_VBufferFill(&stdDisplay_backBuffer, 0, NULL);
    stdDisplay_Update();
    return 0;
}

int J3DAPI stdDisplay_Update(void)
{
    // Nothing to present. A present is where a real display spends a frame's time, so the virtual clock moves here
    // (or, replaying a recording, is set to the recorded time: host_record.h).
    J3DRecord_Present(1);
    return 0;
}

// No video memory to ask about; a nominal 256 MB, total and free, so callers that size caches from it have a value.
#define HEADLESS_VIDEO_MEMORY ((size_t)256 << 20)

int J3DAPI stdDisplay_GetTextureMemory(size_t* pTotal, size_t* pFree)
{
    *pTotal = HEADLESS_VIDEO_MEMORY;
    *pFree  = HEADLESS_VIDEO_MEMORY;
    return 0;
}

int J3DAPI stdDisplay_GetTotalMemory(size_t* pTotal, size_t* pFree)
{
    *pTotal = HEADLESS_VIDEO_MEMORY;
    *pFree  = HEADLESS_VIDEO_MEMORY;
    return 0;
}

void J3DDisplay_SyncBackBuffer(int bOverwriteAll)
{
    (void)bOverwriteAll;
    // Nothing draws anywhere but the back buffer.
}

void J3DAPI stdDisplay_Refresh(int bReload)
{
    (void)bReload; // no surfaces to restore
}
