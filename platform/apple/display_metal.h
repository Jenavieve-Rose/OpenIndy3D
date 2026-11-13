#ifndef J3D_PLATFORM_DISPLAY_METAL_H
#define J3D_PLATFORM_DISPLAY_METAL_H
/*
 * The present path (PORT-PLAN.md P2.6): puts the engine's system-memory back buffer on screen through Metal. C
 * interface; the implementation is Objective-C (display_metal.m). stdDisplay_sdl.c is its only caller.
 *
 * One presenter at a time, attached to one SDL window (a CAMetalLayer from SDL_Metal_CreateView). Each present uploads
 * the back buffer into a texture (a ring of three, so the CPU never writes one the GPU is still reading) and draws it
 * over the whole drawable with bilinear filtering, so a back buffer smaller than the window (a 640x480 mode on a
 * fullscreen 2560x1440 window) is scaled up to fill it. With vsync on, Metal paces presents to the display's refresh.
 */
#include <stddef.h>
#include <stdint.h>

struct SDL_Window;

// Attaches a Metal layer to pWindow and builds the pipeline. bVsync: wait for the display's refresh on present.
// Returns 0 on success; on failure prints the reason to stderr and returns 1, and nothing is presented.
int  J3DMetal_OpenPresenter(struct SDL_Window* pWindow, int bVsync);
void J3DMetal_ClosePresenter(void);
int  J3DMetal_IsPresenterOpen(void);

// The rectangle a width x height picture occupies in a windowWidth x windowHeight window (any one unit): the whole
// window, or with bKeepAspect the largest rectangle of the picture's shape, centred (pillarboxed or letterboxed).
void J3DMetal_PictureRect(float width, float height, float windowWidth, float windowHeight, int bKeepAspect,
    float* pX, float* pY, float* pWidth, float* pHeight);

// Whether presents keep the picture's shape (J3DMetal_PictureRect); off at first.
void J3DMetal_SetKeepAspect(int bKeepAspect);

// Presents a 32 bpp BGRX raster (the back buffer's format; the fourth byte is ignored). Returns 0 when the frame was
// presented or when the window has no drawable to give (hidden, minimized), 1 on error.
int J3DMetal_Present(const uint8_t* pPixels, uint32_t width, uint32_t height, size_t rowSize);

// The Metal device's memory: the working set it recommends (total) and what is not yet allocated from it (free).
// Valid without a presenter; both 0 when there is no Metal device.
void J3DMetal_GetMemory(size_t* pTotal, size_t* pFree);

// Reads back what the next J3DMetal_Present puts on screen: Arm before the present, Take after it. Take returns 0 and
// a malloc'd BGRA copy of the drawable (the caller frees it), at the drawable's size in pixels, or 1 when nothing was
// captured. For tests and diagnostics (tests/present.c); a captured present waits for the GPU.
void J3DMetal_ArmCapture(void);
int  J3DMetal_TakeCapture(uint8_t** ppPixels, uint32_t* pWidth, uint32_t* pHeight);

#endif // J3D_PLATFORM_DISPLAY_METAL_H
