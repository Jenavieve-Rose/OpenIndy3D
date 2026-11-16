#ifndef J3D_PLATFORM_STDDISPLAY_SDL_H
#define J3D_PLATFORM_STDDISPLAY_SDL_H
/*
 * What the SDL display (stdDisplay_sdl.c) offers the rest of the apple platform layer.
 */

// Converts a position in the game window, in SDL's window coordinates (points), to back-buffer pixels: the
// coordinates the game's mouse messages carry. The back buffer is presented scaled to the window, keeping its shape
// unless the "Pillarbox" setting is 0 (P2.20), so the two differ in a fullscreen mode, a resized window and on a Retina
// display. A position in the bars maps outside the back buffer. With no mode set the position is returned unchanged.
void J3DApple_WindowToBackBuffer(float* pX, float* pY);

// A dialog shown before the game sets a display mode (the launcher, a start-up message box) needs somewhere to be
// drawn: opens the display windowed at width x height for it, starting the display module if it was not. Returns 0
// on success. J3DApple_CloseTemporaryScreen undoes all of it, so the game opens its own display as before.
// (PORT-PLAN.md Phase 6)
int  J3DApple_OpenTemporaryScreen(int width, int height);
void J3DApple_CloseTemporaryScreen(void);

#endif // J3D_PLATFORM_STDDISPLAY_SDL_H
