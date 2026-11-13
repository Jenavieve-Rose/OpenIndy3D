/*
 * The Metal renderer core (PORT-PLAN.md P2.9); see render_metal.h.
 *
 * P2.9 is the skeleton: the scene and depth textures and one command buffer per scene. Drawing (P2.10 on) adds the
 * pipelines, vertex buffers and textures.
 *
 * The scene texture is written by the CPU at BeginScene, so BeginScene first waits for the GPU to finish everything
 * committed so far (the previous scene and its present both read the texture). The game submits a few hundred
 * triangles a frame, so the GPU is long done by then and the wait costs nothing measurable; a ring of scene textures
 * can replace it if that changes.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <string.h>

#include "display_metal_internal.h"
#include "render_metal.h"

static id<MTLTexture>              render_sceneColor;
static id<MTLTexture>              render_depth;
static id<MTLCommandBuffer>        render_commands;
static id<MTLRenderCommandEncoder> render_encoder;
static int                         render_bClearDepth;

int J3DRender_Open(uint32_t width, uint32_t height)
{
    J3DRender_Close();

    id<MTLDevice> device = J3DMetal_Device();
    if ( !device || !width || !height )
    {
        return 1;
    }

    @autoreleasepool
    {
        // Shared storage: the CPU uploads the back buffer into it at every BeginScene.
        MTLTextureDescriptor* colorDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                             width:width
                                                                                            height:height
                                                                                         mipmapped:NO];
        colorDesc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDesc.storageMode = MTLStorageModeShared;
        render_sceneColor     = [device newTextureWithDescriptor:colorDesc];

        MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                             width:width
                                                                                            height:height
                                                                                         mipmapped:NO];
        depthDesc.usage       = MTLTextureUsageRenderTarget;
        depthDesc.storageMode = MTLStorageModePrivate;
        render_depth          = [device newTextureWithDescriptor:depthDesc];
    }

    if ( !render_sceneColor || !render_depth )
    {
        J3DRender_Close();
        return 1;
    }

    render_bClearDepth = 1; // retail cleared Z when it created the viewport
    return 0;
}

void J3DRender_Close(void)
{
    if ( render_encoder )
    {
        J3DRender_EndScene();
    }
    J3DMetal_WaitForGPU();
    J3DMetal_SetSceneTexture(nil);
    render_sceneColor = nil;
    render_depth      = nil;
}

// Opens a render pass on the scene, keeping its colour; the depth is cleared if a clear is pending.
static void render_OpenPass(void)
{
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture     = render_sceneColor;
    pass.colorAttachments[0].loadAction  = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture         = render_depth;
    pass.depthAttachment.loadAction      = render_bClearDepth ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.depthAttachment.clearDepth      = 1.0;
    pass.depthAttachment.storeAction     = MTLStoreActionStore;
    render_bClearDepth = 0;

    render_encoder = [render_commands renderCommandEncoderWithDescriptor:pass];
}

int J3DRender_BeginScene(const uint8_t* pBackBuffer, size_t rowSize)
{
    if ( !render_sceneColor )
    {
        return 1;
    }
    if ( render_encoder )
    {
        J3DRender_EndScene(); // a scene left open: retail's BeginScene would have failed; close it and go on
    }

    // The GPU may still read the scene texture (the previous scene, its present): let it finish before the upload.
    J3DMetal_WaitForGPU();
    if ( pBackBuffer )
    {
        [render_sceneColor replaceRegion:MTLRegionMake2D(0, 0, render_sceneColor.width, render_sceneColor.height)
                             mipmapLevel:0
                               withBytes:pBackBuffer
                             bytesPerRow:rowSize];
    }

    render_commands = [J3DMetal_Queue() commandBuffer];
    render_OpenPass();
    return render_encoder ? 0 : 1;
}

void J3DRender_EndScene(void)
{
    if ( !render_encoder )
    {
        return;
    }

    [render_encoder endEncoding];
    render_encoder = nil;
    J3DMetal_Commit(render_commands);
    render_commands = nil;
    J3DMetal_SetSceneTexture(render_sceneColor);
}

void J3DRender_ClearDepth(void)
{
    render_bClearDepth = 1;
    if ( render_encoder )
    {
        // Within a scene: end this pass and open another that clears the depth and keeps the colour.
        [render_encoder endEncoding];
        render_OpenPass();
    }
}

void J3DRender_GetDeviceName(char* pName, size_t size)
{
    if ( !size )
    {
        return;
    }
    id<MTLDevice> device = J3DMetal_Device();
    const char*   pText  = device ? device.name.UTF8String : "";
    strncpy(pName, pText ? pText : "", size - 1);
    pName[size - 1] = '\0';
}
