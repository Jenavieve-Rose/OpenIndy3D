#ifndef J3D_PLATFORM_APPLE_STDCONTROL_SDL_H
#define J3D_PLATFORM_APPLE_STDCONTROL_SDL_H
/*
 * The SDL side of stdControl (PORT-PLAN.md Phase 3) that the window's event loop feeds: mouse buttons and wheel, which
 * stdControl reads as buffered events as DirectInput delivered them, and controllers connecting and disconnecting.
 */
#include <SDL3/SDL.h>

// Keys the game never sees, as window messages or as key state: the Mac's Fn (Globe) key, which SDL reports as Right
// Command when a keyboard sends it as a key, and F13, which SDL reports as Print Screen.
int J3DControl_IsIgnoredKey(const SDL_KeyboardEvent* pEvent);

// Called by wkernel_sdl.c for every SDL event, before it becomes a window message.
void J3DControl_HandleEvent(const SDL_Event* pEvent);

// Called by wkernel_sdl.c when the game window gains or loses focus, so the mouse is captured only while it has it.
void J3DControl_SetWindowFocus(int bFocus);

// Called by j3dmenu_apple.c as a menu over the game window opens (1) and closes (0); they nest. The mouse is not captured
// while one is up: the game window keeps the focus under a menu, and the wait for a control button turns the controls on
// to read the pads.
void J3DControl_SetMenuUp(int bUp);

#endif
