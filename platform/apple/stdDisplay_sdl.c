/*
 * stdDisplay over SDL3 (PORT-PLAN.md P2.3, P2.6, P2.8): the apple build's display backend. It replaces the null
 * build's platform/common/null/stdDisplay_headless.c and supplies the backend half of stdDisplay
 * (stdDisplay_common.h): Startup, Shutdown, Open, Close, SetMode, Update and the memory queries. The queries, the
 * closest-mode search, the back buffer and the blits are shared with the null build
 * (platform/common/stdDisplay_common.c).
 *
 * Device. One display device: the display the game window is on. Its name is the display's, its driver name is
 * "SDL <video driver>" (the "Display" setting stores it), and it reports itself as 3D capable and able to render in a
 * window, so the game's device search picks it. The 3D device list is std3D's (Metal, P2.9).
 *
 * Modes. Every mode is 32 bpp BGRA, the back buffer's format. The fullscreen list is, in pixels: the desktop's size,
 * the classic 4:3 sizes from 640x480 up that fit the desktop, and the display's own fullscreen modes no larger than
 * the desktop, without duplicates, at most 64 (the retail cap), sorted as retail sorted them (bpp, width, height). The
 * desktop size and 640x480 are always present. A display mode's size in pixels is its size in points times its pixel
 * density, so a Retina display's native size is in the list.
 *
 * Fullscreen. Retail changed the display mode (DirectDraw exclusive mode). The port does not: a fullscreen mode is
 * the window made fullscreen at the desktop's own mode, with a back buffer of the mode's size that the present path
 * scales to the screen (P2.6). The window leaves fullscreen when the display closes, as DirectDraw restored the
 * desktop mode when the game released it.
 *
 * Present. stdDisplay_Update is retail's flip: it hands the back buffer to the Metal presenter (display_metal.h),
 * which scales it to the window and waits for the display's refresh. The presenter is attached when a mode is first
 * set and detached when the display closes. Under SDL's dummy video driver there is no Metal view and nothing is
 * presented.
 *
 * Headless (J3D_HEADLESS, jones3d --headless). The same device and modes, from SDL's dummy video driver (a 1024x768
 * desktop, or J3D_HEADLESS_SIZE's), and time runs on the virtual clock: stdDisplay_Update moves it by one frame, as in the null build. With
 * SDL_VIDEO_DRIVER=cocoa a headless run still presents to its window, without waiting for the refresh.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <std/General/std.h>
#include <std/Win95/stdDisplay.h>
#include <std/Win95/stdWin95.h>
#include <w32util/wuRegistry.h>

#include <platform/apple/display_metal.h>
#include <platform/apple/render_metal.h>
#include <platform/apple/stdDisplay_sdl.h>
#include <platform/common/host_clock.h>
#include <platform/common/host_record.h>
#include <platform/common/stdDisplay_common.h>

// A windowed run with no presenter (it failed to open) holds each frame to this period, so the game does not spin and
// its scripts see frames of a plausible length (host_clock.h). With a presenter, vsync paces the frames, or the frame
// limiter ("Frame Limit", P8.7).
#define STDDISPLAY_FRAME_NSEC (SDL_NS_PER_SECOND / 60)

static int    stdDisplay_bStartup = 0;
static int    stdDisplay_bKeepAspect = 1; // the "Pillarbox" setting (P2.20)
static Uint64 stdDisplay_lastFrameNsec = 0;
static Uint64 stdDisplay_frameLimitNsec = 0; // the "Frame Limit" setting as a frame period, 0 for none (P8.7)

// The classic 4:3 sizes offered whether or not the display lists them, so the retail defaults (640x480) and the sizes
// the retail settings dialog showed are always there. Only those that fit the desktop are added.
static const uint32_t stdDisplay_aWindowSizes[][2] = {
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 960 }, { 1600, 1200 },
};

static SDL_Window* stdDisplay_GetSDLWindow(void)
{
    return (SDL_Window*)stdWin95_GetWindow(); // wkernel_sdl.c: the HWND is the SDL window
}

// Nonzero when the window can carry a Metal view: any SDL video driver but the dummy one.
static int stdDisplay_CanPresent(void)
{
    const char* pDriver = SDL_GetCurrentVideoDriver();
    return stdDisplay_GetSDLWindow() && pDriver && strcmp(pDriver, "dummy") != 0;
}

static uint32_t stdDisplay_ToPixels(int points, float pixelDensity)
{
    return points > 0 ? (uint32_t)lroundf((float)points * (pixelDensity > 0.0f ? pixelDensity : 1.0f)) : 0;
}

// Adds a width x height mode unless it is already listed or the list is full.
static void stdDisplay_AddVideoMode(uint32_t width, uint32_t height)
{
    if ( !width || !height || stdDisplay_numVideoModes >= STDDISPLAY_MAXVIDEOMODES )
    {
        return;
    }

    for ( size_t i = 0; i < stdDisplay_numVideoModes; ++i )
    {
        if ( stdDisplay_aVideoModes[i].rasterInfo.width == width && stdDisplay_aVideoModes[i].rasterInfo.height == height )
        {
            return;
        }
    }

    J3DDisplay_InitVideoMode(&stdDisplay_aVideoModes[stdDisplay_numVideoModes++], width, height);
}

// Fills in the one device and the mode list from the display the window is on. 1 on success.
static int stdDisplay_EnumDisplay(void)
{
    SDL_Window*   pWindow   = stdDisplay_GetSDLWindow();
    SDL_DisplayID displayID = pWindow ? SDL_GetDisplayForWindow(pWindow) : 0;
    if ( !displayID )
    {
        displayID = SDL_GetPrimaryDisplay();
    }

    const SDL_DisplayMode* pDesktop = displayID ? SDL_GetDesktopDisplayMode(displayID) : NULL;
    if ( !pDesktop )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %s when reading the desktop display mode.\n", SDL_GetError(), 0, 0, 0);
        return 0;
    }

    uint32_t desktopWidth  = stdDisplay_ToPixels(pDesktop->w, pDesktop->pixel_density);
    uint32_t desktopHeight = stdDisplay_ToPixels(pDesktop->h, pDesktop->pixel_density);

    // For tests: J3D_HEADLESS_SIZE=<w>x<h> gives a headless run that desktop instead of the dummy driver's 1024x768,
    // so a wide picture can be rendered and compared on the virtual clock (PORT-PLAN.md P8.9)
    const char* pHeadlessSize = getenv("J3D_HEADLESS_SIZE");
    unsigned int sizeW, sizeH;
    if ( J3DDisplay_IsHeadless() && pHeadlessSize && sscanf(pHeadlessSize, "%ux%u", &sizeW, &sizeH) == 2
         && sizeW >= 640 && sizeH >= 480 && sizeW <= 8192 && sizeH <= 8192 )
    {
        desktopWidth  = sizeW;
        desktopHeight = sizeH;
    }

    // The device.
    StdDisplayDevice* pDevice = &stdDisplay_aDisplayDevices[0];
    memset(pDevice, 0, sizeof(*pDevice));
    const char* pDisplayName = SDL_GetDisplayName(displayID);
    SDL_strlcpy(pDevice->aDeviceName, pDisplayName ? pDisplayName : "Display", sizeof(pDevice->aDeviceName));
    SDL_snprintf(pDevice->aDriverName, sizeof(pDevice->aDriverName), "SDL %s", SDL_GetCurrentVideoDriver());
    pDevice->bHAL                      = 1;
    pDevice->bGuidNotSet               = 1;
    pDevice->bWindowRenderNotSupported = 0;
    J3DMetal_GetMemory(&pDevice->totalVideoMemory, &pDevice->freeVideoMemory);
    stdDisplay_numDevices = 1;

    // The modes: the desktop first, so it survives the cap, then the 4:3 sizes, then the display's own.
    stdDisplay_numVideoModes = 0;
    stdDisplay_AddVideoMode(desktopWidth, desktopHeight);
    for ( size_t i = 0; i < SDL_arraysize(stdDisplay_aWindowSizes); ++i )
    {
        if ( stdDisplay_aWindowSizes[i][0] <= desktopWidth && stdDisplay_aWindowSizes[i][1] <= desktopHeight )
        {
            stdDisplay_AddVideoMode(stdDisplay_aWindowSizes[i][0], stdDisplay_aWindowSizes[i][1]);
        }
    }
    if ( stdDisplay_numVideoModes == 1 )
    {
        stdDisplay_AddVideoMode(640, 480); // a desktop smaller than 640x480 still offers the retail default
    }

    int numDisplayModes = 0;
    SDL_DisplayMode** apDisplayModes = SDL_GetFullscreenDisplayModes(displayID, &numDisplayModes);
    if ( apDisplayModes )
    {
        for ( int i = 0; i < numDisplayModes; ++i )
        {
            const uint32_t width  = stdDisplay_ToPixels(apDisplayModes[i]->w, apDisplayModes[i]->pixel_density);
            const uint32_t height = stdDisplay_ToPixels(apDisplayModes[i]->h, apDisplayModes[i]->pixel_density);
            if ( width <= desktopWidth && height <= desktopHeight )
            {
                stdDisplay_AddVideoMode(width, height);
            }
        }
        SDL_free(apDisplayModes);
    }

    qsort(stdDisplay_aVideoModes, stdDisplay_numVideoModes, sizeof(StdVideoMode), J3DDisplay_VideoModeCompare);

    stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "Found display %s [%s], desktop %dx%d.\n",
        pDevice->aDeviceName, pDevice->aDriverName, (int)desktopWidth, (int)desktopHeight);
    stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "%d video modes.\n", (int)stdDisplay_numVideoModes, 0, 0, 0);
    return 1;
}

// The window half of retail SetWindowMode and SetFullscreenMode. 0 on success.
static int stdDisplay_SetWindowMode(const StdVideoMode* pMode, int bFullscreen)
{
    SDL_Window* pWindow = stdDisplay_GetSDLWindow();
    if ( !pWindow )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error setting display mode: no window.\n", 0, 0, 0, 0);
        return 1;
    }

    // The game window is created hidden (wkernel_sdl.c), so the launcher is alone on screen; the game's first mode shows
    // it, as the game's own ShowWindow did on Windows
    if ( SDL_GetWindowFlags(pWindow) & SDL_WINDOW_HIDDEN )
    {
        SDL_ShowWindow(pWindow);
        SDL_RaiseWindow(pWindow);
    }

    if ( bFullscreen )
    {
        // NULL: fullscreen at the desktop's mode. The back buffer keeps the game's mode size (see the top of the file).
        SDL_SetWindowResizable(pWindow, false);
        if ( !SDL_SetWindowFullscreenMode(pWindow, NULL) || !SDL_SetWindowFullscreen(pWindow, true) )
        {
            stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %s when entering fullscreen.\n", SDL_GetError(), 0, 0, 0);
            return 1;
        }
        return 0;
    }

    if ( !SDL_SetWindowFullscreen(pWindow, false) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %s when leaving fullscreen.\n", SDL_GetError(), 0, 0, 0);
        return 1;
    }

    // The window's client area is the mode's size in points; on a Retina display the drawable is larger (P2.15). The
    // player may resize it: the back buffer keeps the mode's size and the presenter scales it (P2.19).
    SDL_SetWindowSize(pWindow, (int)pMode->rasterInfo.width, (int)pMode->rasterInfo.height);
    SDL_SetWindowResizable(pWindow, true);
    return 0;
}

int J3DAPI stdDisplay_Startup(void)
{
    if ( stdDisplay_bStartup )
    {
        return 1;
    }

    if ( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error %s when starting SDL video.\n", SDL_GetError(), 0, 0, 0);
        return 0;
    }

    stdDisplay_numDevices = 0;
    memset(stdDisplay_aDisplayDevices, 0, sizeof(stdDisplay_aDisplayDevices));
    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));

    if ( !stdDisplay_EnumDisplay() )
    {
        // As retail when DirectDrawEnumerate failed: no display system, and the game reports a display error.
        stdDisplay_numDevices    = 0;
        stdDisplay_numVideoModes = 0;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 0;
    }

    stdDisplay_bStartup = 1;

    J3DDisplay_InitVideoMode(&stdDisplay_primaryVideoMode, 640, 480); // retail: "TODO: Change default res"

    stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "Found %d Display Devices.\n", (int)stdDisplay_numDevices, 0, 0, 0);
    return 1;
}

void J3DAPI stdDisplay_Shutdown(void)
{
    if ( !stdDisplay_bStartup )
    {
        return;
    }

    if ( stdDisplay_bOpen )
    {
        stdDisplay_Close();
    }

    stdDisplay_numVideoModes = 0;
    stdDisplay_pCurVideoMode = NULL;
    stdDisplay_numDevices    = 0;
    memset(stdDisplay_aDisplayDevices, 0, sizeof(stdDisplay_aDisplayDevices));
    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));

    SDL_QuitSubSystem(SDL_INIT_VIDEO); // balances Startup's SDL_InitSubSystem; wkernel_sdl.c holds its own reference
    stdDisplay_bStartup = 0;
}

int J3DAPI stdDisplay_Open(size_t deviceNum)
{
    STD_ASSERTREL(stdDisplay_bStartup == 1);
    if ( stdDisplay_bOpen )
    {
        stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Warning: System already open!\n", 0, 0, 0, 0);
        stdDisplay_Close();
    }

    if ( deviceNum >= stdDisplay_numDevices ) // Port: retail tested deviceNum > numDevices, one past the end
    {
        return 0;
    }

    if ( J3DDisplay_IsHeadless() )
    {
        // From here on time moves by a fixed step per presented frame (host_clock.h).
        J3DHost_UseVirtualClock();
    }

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

    J3DMetal_ClosePresenter();

    // Retail released DirectDraw here, which gave the desktop back its own mode.
    SDL_Window* pWindow = stdDisplay_GetSDLWindow();
    if ( stdDisplay_bFullscreen && pWindow )
    {
        SDL_SetWindowFullscreen(pWindow, false);
    }
    stdDisplay_bFullscreen = 0;

    stdDisplay_pCurDevice = NULL;
    memset(&stdDisplay_frontBuffer, 0, sizeof(stdDisplay_frontBuffer));
    memset(&stdDisplay_backBuffer, 0, sizeof(stdDisplay_backBuffer));
    stdDisplay_bOpen = 0;
}

int J3DAPI stdDisplay_SetMode(size_t modeNum, int bFullscreen, size_t numBackBuffers)
{
    (void)numBackBuffers; // the present path owns its swap chain (P2.6)

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
    if ( stdDisplay_SetWindowMode(stdDisplay_pCurVideoMode, bFullscreen) || J3DDisplay_CreateBackBuffer(stdDisplay_pCurVideoMode) )
    {
        stdDisplay_pCurVideoMode = NULL;
        return 1;
    }

    stdDisplay_bModeSet    = 1;
    stdDisplay_bFullscreen = bFullscreen;

    // P2.20: keep the picture's shape (4:3 modes pillarboxed on a wide screen) unless "Pillarbox" is 0. A port setting:
    // the HUD is laid out for 4:3 (until the widescreen fixes, P8.9), and a stretched 640x480 distorts everything.
    stdDisplay_bKeepAspect = wuRegistry_GetInt("Pillarbox", 1) != 0;
    J3DMetal_SetKeepAspect(stdDisplay_bKeepAspect);

    // The presenter, once per open display. Retail created its primary surface here; its failure failed the mode.
    if ( !J3DMetal_IsPresenterOpen() && stdDisplay_CanPresent() )
    {
        // P8.7: "VSync" (default on) and "Frame Limit" (frames a second, 0 for none), the launcher's Options
        stdDisplay_frameLimitNsec = 0;
        const int frameLimit = wuRegistry_GetInt("Frame Limit", 0);
        if ( frameLimit > 0 )
        {
            stdDisplay_frameLimitNsec = SDL_NS_PER_SECOND / (Uint64)(frameLimit < 10 ? 10 : frameLimit);
        }
        if ( J3DMetal_OpenPresenter(stdDisplay_GetSDLWindow(), !J3DDisplay_IsHeadless() && wuRegistry_GetInt("VSync", 1) != 0) )
        {
            stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error creating the Metal presenter.\n", 0, 0, 0, 0);
            stdDisplay_ClearMode();
            return 1;
        }
    }

    stdDisplay_VBufferFill(&stdDisplay_backBuffer, 0, NULL); // 0 - BLACK
    stdDisplay_Update();
    return 0;
}

int J3DAPI stdDisplay_Update(void)
{
    if ( stdDisplay_bModeSet && J3DMetal_IsPresenterOpen() )
    {
        if ( J3DMetal_Present(stdDisplay_backBuffer.pPixels, stdDisplay_backBuffer.rasterInfo.width,
                stdDisplay_backBuffer.rasterInfo.height, stdDisplay_backBuffer.rasterInfo.rowSize) )
        {
            stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "Error when flipping the page.\n", 0, 0, 0, 0);
            return 1;
        }
    }

    // A present is where a real display spends a frame's time, so the clock moves here: by the fixed headless step, or
    // to the host's time on a recording's frame clock, or to the recorded time in a replay (host_record.h, P3.10).
    J3DRecord_Present(J3DDisplay_IsHeadless());
    if ( J3DDisplay_IsHeadless() )
    {
        return 0;
    }

    // With a presenter, vsync paces the frames, and the frame limiter when one is set; without one, a 60 Hz period
    Uint64 periodNsec = STDDISPLAY_FRAME_NSEC;
    if ( J3DMetal_IsPresenterOpen() )
    {
        if ( !stdDisplay_frameLimitNsec )
        {
            return 0;
        }
        periodNsec = stdDisplay_frameLimitNsec;
    }

    const Uint64 nowNsec = SDL_GetTicksNS();
    const Uint64 dueNsec = stdDisplay_lastFrameNsec + periodNsec;
    if ( stdDisplay_lastFrameNsec && nowNsec < dueNsec )
    {
        SDL_DelayPrecise(dueNsec - nowNsec);
    }
    stdDisplay_lastFrameNsec = SDL_GetTicksNS();
    return 0;
}

// Retail asked DirectDraw for the video memory free for textures and in total; the port reports the Metal device's
// recommended working set for both, and what is not yet allocated from it as free.
int J3DAPI stdDisplay_GetTextureMemory(size_t* pTotal, size_t* pFree)
{
    J3DMetal_GetMemory(pTotal, pFree);
    return 0;
}

int J3DAPI stdDisplay_GetTotalMemory(size_t* pTotal, size_t* pFree)
{
    J3DMetal_GetMemory(pTotal, pFree);
    return 0;
}

void J3DDisplay_SyncBackBuffer(int bOverwriteAll)
{
    // See stdDisplay_common.h. J3DRender_TakeScene does nothing unless a scene was drawn since the last BeginScene.
    if ( stdDisplay_bModeSet )
    {
        J3DRender_TakeScene(bOverwriteAll ? NULL : stdDisplay_backBuffer.pPixels, stdDisplay_backBuffer.rasterInfo.width,
            stdDisplay_backBuffer.rasterInfo.height, stdDisplay_backBuffer.rasterInfo.rowSize);
    }
}

void J3DApple_WindowToBackBuffer(float* pX, float* pY)
{
    SDL_Window* pWindow = stdDisplay_GetSDLWindow();
    int width = 0, height = 0;
    if ( !stdDisplay_bModeSet || !pWindow || !SDL_GetWindowSize(pWindow, &width, &height) || width <= 0 || height <= 0 )
    {
        return;
    }

    // The same rectangle the presenter draws the picture in (pillarboxed with the option on, P2.20).
    const float bbWidth = (float)stdDisplay_backBuffer.rasterInfo.width, bbHeight = (float)stdDisplay_backBuffer.rasterInfo.height;
    float x, y, w, h;
    J3DMetal_PictureRect(bbWidth, bbHeight, (float)width, (float)height, stdDisplay_bKeepAspect, &x, &y, &w, &h);
    *pX = (*pX - x) * bbWidth / w;
    *pY = (*pY - y) * bbHeight / h;
}

// Retail, on reactivation (bReload 1): restore the cooperative level, the fullscreen display mode and the lost surfaces.
// Here: a fullscreen window that was minimized when the game lost focus (wkernel_sdl.c) is restored and made
// fullscreen again. Nothing to do for a window, or on deactivation (bReload 0), as in retail.
void J3DAPI stdDisplay_Refresh(int bReload)
{
    SDL_Window* pWindow = stdDisplay_GetSDLWindow();
    if ( !stdDisplay_bOpen || !stdDisplay_bModeSet || !bReload || !stdDisplay_bFullscreen || !pWindow )
    {
        return;
    }

    if ( SDL_GetWindowFlags(pWindow) & SDL_WINDOW_MINIMIZED )
    {
        SDL_RestoreWindow(pWindow);
    }
    if ( !(SDL_GetWindowFlags(pWindow) & SDL_WINDOW_FULLSCREEN) )
    {
        SDL_SetWindowFullscreen(pWindow, true);
    }
}

/* ---- A screen for dialogs before the game's display (PORT-PLAN.md Phase 6) ------------------------------------------- */

static int stdDisplay_bTempScreen = 0;
static int stdDisplay_bTempStartup = 0;
static StdVideoMode stdDisplay_savedPrimary;

int J3DApple_OpenTemporaryScreen(int width, int height)
{
    if ( stdDisplay_bTempScreen || stdDisplay_bModeSet )
    {
        return 0;
    }
    stdDisplay_bTempStartup = 0;
    if ( !stdDisplay_bStartup )
    {
        if ( stdDisplay_Startup() != 1 )
        {
            return 1;
        }
        stdDisplay_bTempStartup = 1;
    }
    if ( !stdDisplay_bOpen && !stdDisplay_Open(0) )
    {
        if ( stdDisplay_bTempStartup )
        {
            stdDisplay_Shutdown();
        }
        return 1;
    }
    stdDisplay_savedPrimary = stdDisplay_primaryVideoMode;
    J3DDisplay_InitVideoMode(&stdDisplay_primaryVideoMode, (uint32_t)width, (uint32_t)height);
    if ( stdDisplay_SetMode(0, 0, 1) )
    {
        stdDisplay_primaryVideoMode = stdDisplay_savedPrimary;
        stdDisplay_Close();
        if ( stdDisplay_bTempStartup )
        {
            stdDisplay_Shutdown();
        }
        return 1;
    }
    stdDisplay_bTempScreen = 1;
    return 0;
}

void J3DApple_CloseTemporaryScreen(void)
{
    if ( !stdDisplay_bTempScreen )
    {
        return;
    }
    stdDisplay_bTempScreen = 0;
    stdDisplay_ClearMode();
    stdDisplay_Close();
    stdDisplay_primaryVideoMode = stdDisplay_savedPrimary;
    if ( stdDisplay_bTempStartup )
    {
        stdDisplay_Shutdown();
    }
}
