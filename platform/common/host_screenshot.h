#ifndef J3D_PLATFORM_HOST_SCREENSHOT_H
#define J3D_PLATFORM_HOST_SCREENSHOT_H
/*
 * Screenshots by frame number (PORT-PLAN.md P2.7), so images can be captured by script.
 *
 * J3D_SCREENSHOT_AT=<n>[,<n>...] (jones3d --screenshot-at) names the passes of the game loop after which the back
 * buffer is written with stdDisplay_SaveScreen, a 24-bit BMP. J3D_SCREENSHOT_FILE (jones3d --screenshot-file) is the
 * file name; "{frame}" in it becomes the frame number. The default is "jones3d-{frame}.bmp" in the current directory.
 * Each file written is reported on stdout.
 */

// Called by the event loop (wkernel_null.c, wkernel_sdl.c) after each pass, with the number of passes so far.
void J3DHost_AfterFrame(unsigned long numFrames);

#endif // J3D_PLATFORM_HOST_SCREENSHOT_H
