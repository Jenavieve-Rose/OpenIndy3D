/*
 * The present path over Metal (PORT-PLAN.md P2.6); see display_metal.h.
 *
 * The shaders are compiled from source when the presenter opens: a single triangle covering the drawable, sampling the
 * back-buffer texture. The renderer proper (std3D, P2.9 on) will have its own pipelines; how its output and the back
 * buffer are combined is P2.17.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "display_metal.h"

#define METAL_TEXTURE_RING 3 // textures (and command buffers) in flight

static NSString* const metal_shaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VertexOut { float4 position [[position]]; float2 uv; };\n"
     "vertex VertexOut present_vertex(uint vid [[vertex_id]])\n"
     "{\n"
     "    // (0,0), (2,0), (0,2): one triangle whose inside covers clip space from -1 to 1.\n"
     "    float2 p = float2((vid << 1) & 2, vid & 2);\n"
     "    VertexOut out;\n"
     "    out.position = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
     "    out.uv       = float2(p.x, 1.0 - p.y); // texture row 0 at the top\n"
     "    return out;\n"
     "}\n"
     "fragment float4 present_fragment(VertexOut in [[stage_in]], texture2d<float> tex [[texture(0)]],\n"
     "                                 sampler smp [[sampler(0)]])\n"
     "{\n"
     "    return float4(tex.sample(smp, in.uv).rgb, 1.0); // the back buffer's fourth byte is not alpha\n"
     "}\n";

static SDL_Window*                metal_pWindow;
static SDL_MetalView              metal_view;
static CAMetalLayer*              metal_layer;
static id<MTLDevice>              metal_device;
static id<MTLCommandQueue>        metal_queue;
static id<MTLRenderPipelineState> metal_pipeline;
static id<MTLSamplerState>        metal_sampler;
static id<MTLTexture>             metal_aTextures[METAL_TEXTURE_RING];
static size_t                     metal_curTexture;
static dispatch_semaphore_t       metal_inFlight;

static int      metal_bCaptureArmed;
static uint8_t* metal_pCapture;
static uint32_t metal_captureWidth, metal_captureHeight;

static id<MTLDevice> metal_GetDevice(void)
{
    if ( !metal_device )
    {
        metal_device = MTLCreateSystemDefaultDevice();
    }
    return metal_device;
}

int J3DMetal_OpenPresenter(SDL_Window* pWindow, int bVsync)
{
    J3DMetal_ClosePresenter();

    @autoreleasepool
    {
        id<MTLDevice> device = metal_GetDevice();
        if ( !device )
        {
            fprintf(stderr, "display_metal: no Metal device\n");
            return 1;
        }

        metal_view = SDL_Metal_CreateView(pWindow);
        if ( !metal_view )
        {
            fprintf(stderr, "display_metal: SDL_Metal_CreateView failed: %s\n", SDL_GetError());
            return 1;
        }

        metal_layer                    = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metal_view);
        metal_layer.device             = device;
        metal_layer.pixelFormat        = MTLPixelFormatBGRA8Unorm;
        metal_layer.framebufferOnly    = NO; // J3DMetal_ArmCapture copies the drawable
        metal_layer.displaySyncEnabled = bVsync ? YES : NO;
        metal_layer.maximumDrawableCount = METAL_TEXTURE_RING;

        NSError*       error   = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:metal_shaderSource options:nil error:&error];
        if ( !library )
        {
            fprintf(stderr, "display_metal: shader compile failed: %s\n", error.localizedDescription.UTF8String);
            J3DMetal_ClosePresenter();
            return 1;
        }

        MTLRenderPipelineDescriptor* pipelineDesc = [MTLRenderPipelineDescriptor new];
        pipelineDesc.vertexFunction                  = [library newFunctionWithName:@"present_vertex"];
        pipelineDesc.fragmentFunction                = [library newFunctionWithName:@"present_fragment"];
        pipelineDesc.colorAttachments[0].pixelFormat = metal_layer.pixelFormat;
        metal_pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if ( !metal_pipeline )
        {
            fprintf(stderr, "display_metal: pipeline failed: %s\n", error.localizedDescription.UTF8String);
            J3DMetal_ClosePresenter();
            return 1;
        }

        MTLSamplerDescriptor* samplerDesc = [MTLSamplerDescriptor new];
        samplerDesc.minFilter    = MTLSamplerMinMagFilterLinear;
        samplerDesc.magFilter    = MTLSamplerMinMagFilterLinear;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        metal_sampler = [device newSamplerStateWithDescriptor:samplerDesc];

        metal_queue      = [device newCommandQueue];
        metal_inFlight   = dispatch_semaphore_create(METAL_TEXTURE_RING);
        metal_curTexture = 0;
        metal_pWindow    = pWindow;
    }
    return 0;
}

void J3DMetal_ClosePresenter(void)
{
    if ( metal_queue && metal_inFlight )
    {
        // Let the frames in flight finish before their textures go.
        for ( int i = 0; i < METAL_TEXTURE_RING; ++i )
        {
            dispatch_semaphore_wait(metal_inFlight, DISPATCH_TIME_FOREVER);
        }
        for ( int i = 0; i < METAL_TEXTURE_RING; ++i )
        {
            dispatch_semaphore_signal(metal_inFlight);
        }
    }

    for ( int i = 0; i < METAL_TEXTURE_RING; ++i )
    {
        metal_aTextures[i] = nil;
    }
    metal_pipeline = nil;
    metal_sampler  = nil;
    metal_queue    = nil;
    metal_inFlight = nil;
    metal_layer    = nil;
    if ( metal_view )
    {
        SDL_Metal_DestroyView(metal_view);
        metal_view = NULL;
    }
    metal_pWindow = NULL;
}

int J3DMetal_IsPresenterOpen(void)
{
    return metal_queue != nil;
}

int J3DMetal_Present(const uint8_t* pPixels, uint32_t width, uint32_t height, size_t rowSize)
{
    if ( !metal_queue || !pPixels || !width || !height )
    {
        return 1;
    }

    @autoreleasepool
    {
        dispatch_semaphore_wait(metal_inFlight, DISPATCH_TIME_FOREVER);

        // Upload into the next texture of the ring; the semaphore guarantees the GPU is done with it.
        id<MTLTexture> texture = metal_aTextures[metal_curTexture];
        if ( !texture || texture.width != width || texture.height != height )
        {
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                            width:width
                                                                                           height:height
                                                                                        mipmapped:NO];
            desc.usage       = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModeShared;
            texture = [metal_device newTextureWithDescriptor:desc];
            metal_aTextures[metal_curTexture] = texture;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:pPixels bytesPerRow:rowSize];
        metal_curTexture = (metal_curTexture + 1) % METAL_TEXTURE_RING;

        // The drawable covers the window in pixels.
        int pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSizeInPixels(metal_pWindow, &pixelWidth, &pixelHeight);
        if ( pixelWidth > 0 && pixelHeight > 0
            && (metal_layer.drawableSize.width != pixelWidth || metal_layer.drawableSize.height != pixelHeight) )
        {
            metal_layer.drawableSize = CGSizeMake(pixelWidth, pixelHeight);
        }

        id<CAMetalDrawable> drawable = [metal_layer nextDrawable];
        if ( !drawable )
        {
            // Nothing to draw on (the window is hidden or minimized): the frame is dropped.
            dispatch_semaphore_signal(metal_inFlight);
            return 0;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture     = drawable.texture;
        pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        id<MTLCommandBuffer>        commands = [metal_queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder  = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:metal_pipeline];
        [encoder setFragmentTexture:texture atIndex:0];
        [encoder setFragmentSamplerState:metal_sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];

        id<MTLBuffer> captureBuffer = nil;
        const NSUInteger drawableWidth  = drawable.texture.width;
        const NSUInteger drawableHeight = drawable.texture.height;
        if ( metal_bCaptureArmed )
        {
            metal_bCaptureArmed = 0;
            captureBuffer = [metal_device newBufferWithLength:drawableWidth * drawableHeight * 4
                                                      options:MTLResourceStorageModeShared];
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit copyFromTexture:drawable.texture
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(drawableWidth, drawableHeight, 1)
                         toBuffer:captureBuffer
                destinationOffset:0
           destinationBytesPerRow:drawableWidth * 4
         destinationBytesPerImage:drawableWidth * drawableHeight * 4];
            [blit endEncoding];
        }

        [commands presentDrawable:drawable];
        dispatch_semaphore_t inFlight = metal_inFlight;
        [commands addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            (void)buffer;
            dispatch_semaphore_signal(inFlight);
        }];
        [commands commit];

        if ( captureBuffer )
        {
            [commands waitUntilCompleted];
            free(metal_pCapture);
            metal_pCapture = (uint8_t*)malloc(captureBuffer.length);
            if ( metal_pCapture )
            {
                memcpy(metal_pCapture, captureBuffer.contents, captureBuffer.length);
                metal_captureWidth  = (uint32_t)drawableWidth;
                metal_captureHeight = (uint32_t)drawableHeight;
            }
        }
    }
    return 0;
}

void J3DMetal_GetMemory(size_t* pTotal, size_t* pFree)
{
    id<MTLDevice> device = metal_GetDevice();
    size_t total = device ? (size_t)device.recommendedMaxWorkingSetSize : 0;
    size_t used  = device ? (size_t)device.currentAllocatedSize : 0;
    *pTotal = total;
    *pFree  = total > used ? total - used : 0;
}

void J3DMetal_ArmCapture(void)
{
    metal_bCaptureArmed = 1;
}

int J3DMetal_TakeCapture(uint8_t** ppPixels, uint32_t* pWidth, uint32_t* pHeight)
{
    if ( !metal_pCapture )
    {
        return 1;
    }

    *ppPixels      = metal_pCapture;
    *pWidth        = metal_captureWidth;
    *pHeight       = metal_captureHeight;
    metal_pCapture = NULL;
    return 0;
}
