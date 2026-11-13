#ifndef J3D_PLATFORM_RENDER_METAL_H
#define J3D_PLATFORM_RENDER_METAL_H
/*
 * The Metal renderer core (PORT-PLAN.md P2.9 on): what std3D_metal.c, the engine-facing std3D, asks of the GPU. C
 * interface; the implementation is Objective-C (render_metal.m), on the shared device and queue of
 * display_metal_internal.h.
 *
 * The renderer draws into a scene texture the size of the back buffer, with a depth texture beside it. A scene starts
 * from the back buffer's current pixels, so what the CPU wrote into the back buffer before the scene (a clear, a
 * blit) lies under what the scene draws, as it did when Direct3D drew into the back buffer surface itself. A frame
 * that drew a scene is presented from the scene texture (J3DMetal_SetSceneTexture). The order of CPU writes and scenes
 * within a frame is P2.17.
 */
#include <stddef.h>
#include <stdint.h>

// Creates the scene and depth textures for a width x height back buffer. 0 on success; 1 when there is no Metal
// device or the textures cannot be made.
int  J3DRender_Open(uint32_t width, uint32_t height);
void J3DRender_Close(void); // ends a scene left open, waits for the GPU, frees the textures

// Begins a scene: uploads the back buffer's pixels (32 bpp BGRX, the scene texture's size) into the scene texture and
// opens a render pass on it. 0 on success.
int  J3DRender_BeginScene(const uint8_t* pBackBuffer, size_t rowSize);
// Ends the render pass, submits it, and makes the scene texture the frame's picture.
void J3DRender_EndScene(void);
// Clears the depth texture to the far plane (1.0): now, within a scene; at the next BeginScene otherwise.
void J3DRender_ClearDepth(void);

// The default Metal device's name, for the device list; "" when there is none.
void J3DRender_GetDeviceName(char* pName, size_t size);

#endif // J3D_PLATFORM_RENDER_METAL_H
