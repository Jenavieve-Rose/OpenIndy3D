#ifndef J3D_PLATFORM_HOST_AUDIO_H
#define J3D_PLATFORM_HOST_AUDIO_H
/*
 * The audio output device behind the sound driver (PORT-PLAN.md Phase 4).
 *
 * The driver (SoundDriver_mix.c) mixes every voice itself; a device only pulls finished frames from it. The apple
 * build's device is miniaudio on Core Audio (platform/apple/audio_device_ma.c); the null build, and any headless run,
 * has none (platform/common/null/audio_device_null.c), and the driver then keeps its voices without rendering them.
 */
#include <stdint.h>

// Fills pOut with frameCount frames of interleaved stereo float. Called on the device's own thread.
typedef void (*J3DAudioRenderFunc)(float* pOut, uint32_t frameCount, void* pUser);

// Opens the default output device and starts pulling frames from pfRender. Returns 1 and the device's sample rate, or
// 0 when there is no device (null build, headless run, J3D_NO_AUDIO set, or the device failed to open).
int  J3DAudio_OpenDevice(J3DAudioRenderFunc pfRender, void* pUser, uint32_t* pSampleRate);

// Stops and closes the device. After it returns pfRender is not called again.
void J3DAudio_CloseDevice(void);

// Renders frameCount frames of the driver's mix into pOut (interleaved stereo float), exactly as a device's callback
// would, at 48,000 Hz when no device is open. For tests (tests/sounddriver.c); the game never calls it.
void J3DAudio_Render(float* pOut, uint32_t frameCount);

// The game window gained or lost focus. DirectSound kept a buffer playing, silent, while its window was in the
// background unless the buffer was created with DSBCAPS_GLOBALFOCUS; the driver does the same (wkernel_sdl.c calls it).
void J3DAudio_SetFocus(int bFocused);

#endif
