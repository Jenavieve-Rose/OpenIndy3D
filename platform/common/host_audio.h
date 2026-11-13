#ifndef J3D_PLATFORM_HOST_AUDIO_H
#define J3D_PLATFORM_HOST_AUDIO_H
/*
 * The audio output device behind the sound driver (PORT-PLAN.md Phase 4, decision 19).
 *
 * The driver (SoundDriver_mix.c) reads, resamples and mixes every voice itself. A device pulls two kinds of signal from
 * it: one stereo bus with every voice that is not placed in 3D, and, while 3D sound is on, one mono signal per 3D voice
 * slot, which the device places at the slot's position around the listener. The apple build's device is AVAudioEngine
 * with an AVAudioEnvironmentNode (platform/apple/audio_device_avf.m); the null build, and any headless run, has none
 * (platform/common/null/audio_device_null.c), and the driver then keeps its voices without rendering them.
 *
 * Positions and directions are in the game's coordinates (x right, y forward, z up); the device converts.
 */
#include <stdint.h>

// The number of 3D voice slots a device provides; slot numbers run from 0 to this - 1.
#define J3DAUDIO_NUM3DSLOTS 32

// Fills pLeft and pRight with frameCount frames of the stereo bus. Called on the device's own thread.
typedef void (*J3DAudioRenderFunc)(float* pLeft, float* pRight, uint32_t frameCount, void* pUser);

// Fills pMono with frameCount frames of 3D slot `slot`; returns 0 when the slot is silent (pMono is then zeros).
typedef int (*J3DAudioRenderSlotFunc)(int slot, float* pMono, uint32_t frameCount, void* pUser);

// Opens the default output device and starts pulling frames. Returns 1 and the rate the callbacks run at, or 0 when
// there is no device (null build, headless run, J3D_NO_AUDIO set, or the device failed to open).
int  J3DAudio_OpenDevice(J3DAudioRenderFunc pfRender, J3DAudioRenderSlotFunc pfRenderSlot, void* pUser, uint32_t* pSampleRate);

// Stops and closes the device. After it returns the callbacks are not called again.
void J3DAudio_CloseDevice(void);

// The listener (the camera) and its orientation, in game coordinates. front and up need not be normalised.
void J3DAudio_SetListener(const float aPos[3], const float aFront[3], const float aUp[3]);

// Where 3D slot `slot` sounds from, in game coordinates. Ignored for a slot nothing is playing in.
void J3DAudio_SetSlotPosition(int slot, const float aPos[3]);

// Mirrors the 3D scene left to right (the game's "Reverse sound" option).
void J3DAudio_SetMirror(int bMirror);

// Renders as a device's callbacks would, at 48,000 Hz when no device is open. For tests (tests/sounddriver.c); the game
// never calls them. J3DAudio_Render writes interleaved stereo.
void J3DAudio_Render(float* pOut, uint32_t frameCount);
int  J3DAudio_RenderSlot(int slot, float* pMono, uint32_t frameCount);

// The game window gained or lost focus. DirectSound kept a buffer playing, silent, while its window was in the
// background unless the buffer was created with DSBCAPS_GLOBALFOCUS; the driver does the same (wkernel_sdl.c calls it).
void J3DAudio_SetFocus(int bFocused);

#endif
