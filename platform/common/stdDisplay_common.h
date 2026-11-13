#ifndef J3D_PLATFORM_STDDISPLAY_COMMON_H
#define J3D_PLATFORM_STDDISPLAY_COMMON_H
/*
 * What the port's display backends share (stdDisplay_common.c). A backend (platform/common/null/stdDisplay_headless.c
 * for the null build, platform/apple/stdDisplay_sdl.c for the apple build) supplies the device and mode list and the
 * functions that talk to a window: Startup, Shutdown, Open, Close, SetMode and Update. Everything else in stdDisplay.h
 * that has a real implementation works on the state below and lives in stdDisplay_common.c.
 *
 * The names are the retail module's statics (stdDisplay.c), made visible to the backends.
 */
#include <std/types.h>

#define STDDISPLAY_MAXVIDEOMODES 64 // the retail array size

extern size_t       stdDisplay_numDevices;
extern StdVideoMode stdDisplay_aVideoModes[STDDISPLAY_MAXVIDEOMODES]; // fullscreen modes, sorted by VideoModeCompare
extern StdVideoMode stdDisplay_primaryVideoMode;                      // the windowed mode; SetDefaultResolution sizes it
extern int          stdDisplay_bFullscreen;

// Nonzero when J3D_HEADLESS is set (jones3d --headless): no picture is needed and time runs on the virtual clock.
int J3DDisplay_IsHeadless(void);

// Game time that passes per presented frame on the virtual clock (host_clock.h): 30 frames a second, a typical rate
// for the retail game on the hardware it was written for.
#define J3DDISPLAY_HEADLESS_FRAME_MSEC 33

// Fills in a width x height mode in the port's one pixel format: 32 bpp BGRA without alpha (D3D's X8R8G8B8), as
// std3D_GetTextureFormat reports for STDCOLOR_FORMAT_RGB. The aspect ratio follows the retail rule
// (stdDisplay_SetAspectRatio): 0.75 for 320x200, 320x400 and 640x400, otherwise 1.
void J3DDisplay_InitVideoMode(StdVideoMode* pMode, uint32_t width, uint32_t height);

// The retail sort order of the mode list: by bpp, then width, then height. qsort comparator.
int J3DDisplay_VideoModeCompare(const void* pMode1, const void* pMode2);

// Supplied by the backend: called before the CPU reads or writes the back buffer (P2.17). Retail's renderer drew into
// the back buffer itself; the port's draws into a scene texture. When a scene has been drawn since the last scene
// began, the SDL display copies it into the back buffer (unless bOverwriteAll: the caller is about to overwrite every
// pixel) and makes the back buffer the frame's picture again, so CPU writes after a scene land on it and are presented,
// and CPU readers (SaveScreen, save-game thumbnails) see the frame on screen. The headless display has nothing to do.
// Called from every back-buffer lock, fill and blit.
void J3DDisplay_SyncBackBuffer(int bOverwriteAll);

// Called before every CPU read or write of the back buffer: marks it changed (stdDisplay_bBackBufferChanged) and takes
// back a scene the renderer drew since (J3DDisplay_SyncBackBuffer).
void J3DDisplay_BeforeCpuAccess(int bOverwriteAll);

// Set when the CPU may have changed the back buffer since the renderer last took it (std3D_StartScene clears it): a new
// back buffer, or any CPU access. A scene starts from the back buffer only then; otherwise it starts from the previous
// scene's picture, as retail's back buffer still held the previous frame where nothing was drawn (P2.22).
extern int stdDisplay_bBackBufferChanged;

// Makes stdDisplay_backBuffer a system-memory VBuffer in pMode's format and stdDisplay_frontBuffer a description of
// the same raster with no pixels. 0 on success, 1 when out of memory.
int J3DDisplay_CreateBackBuffer(const StdVideoMode* pMode);

#endif // J3D_PLATFORM_STDDISPLAY_COMMON_H
