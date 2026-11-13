#ifndef J3D_PLATFORM_APPLE_STDCONTROL_SDL_H
#define J3D_PLATFORM_APPLE_STDCONTROL_SDL_H
/*
 * The SDL side of stdControl (PORT-PLAN.md Phase 3) that the window's event loop feeds: mouse buttons and wheel, which
 * stdControl reads as buffered events as DirectInput delivered them, and controllers connecting and disconnecting.
 */
#include <SDL3/SDL.h>

// Called by wkernel_sdl.c for every SDL event, before it becomes a window message.
void J3DControl_HandleEvent(const SDL_Event* pEvent);

// Called by wkernel_sdl.c when the game window gains or loses focus, so the mouse is captured only while it has it.
void J3DControl_SetWindowFocus(int bFocus);

#endif
