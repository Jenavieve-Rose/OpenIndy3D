#ifndef J3D_PLATFORM_STDDISPLAY_SDL_H
#define J3D_PLATFORM_STDDISPLAY_SDL_H
/*
 * What the SDL display (stdDisplay_sdl.c) offers the rest of the apple platform layer.
 */

// Converts a position in the game window, in SDL's window coordinates (points), to back-buffer pixels: the
// coordinates the game's mouse messages carry. The back buffer is presented scaled to fill the window (and, with the
// 4:3 option, pillarboxed: P2.20), so the two differ in a fullscreen mode and on a Retina display. With no mode set the
// position is returned unchanged.
void J3DApple_WindowToBackBuffer(float* pX, float* pY);

#endif // J3D_PLATFORM_STDDISPLAY_SDL_H
