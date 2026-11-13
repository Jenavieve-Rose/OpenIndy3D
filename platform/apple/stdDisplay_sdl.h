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

#endif // J3D_PLATFORM_STDDISPLAY_SDL_H
