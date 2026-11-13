#ifndef J3D_PLATFORM_DISPLAY_METAL_INTERNAL_H
#define J3D_PLATFORM_DISPLAY_METAL_INTERNAL_H
/*
 * The Metal context the presenter (display_metal.m) and the renderer (std3D_metal.m) share. Objective-C only: include
 * it from .m files. C code uses display_metal.h.
 *
 * One device and one command queue for the process, so everything the GPU does runs in commit order. Every command
 * buffer is committed through J3DMetal_Commit, which remembers the last one: waiting for it waits for all GPU work so
 * far, which is how a CPU write into a texture the GPU may still be reading is made safe.
 */
#import <Metal/Metal.h>

#include "display_metal.h"

id<MTLDevice>       J3DMetal_Device(void); // nil when the machine has no Metal device
id<MTLCommandQueue> J3DMetal_Queue(void);

void J3DMetal_Commit(id<MTLCommandBuffer> commands);
void J3DMetal_WaitForGPU(void); // blocks until the last committed command buffer has completed

// The renderer's picture for this frame: the next J3DMetal_Present shows this texture instead of uploading the back
// buffer, then forgets it. Its size need not match the window; it is scaled like the back buffer.
void J3DMetal_SetSceneTexture(id<MTLTexture> texture);

#endif // J3D_PLATFORM_DISPLAY_METAL_INTERNAL_H
