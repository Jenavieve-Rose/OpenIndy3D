/*
 * The Metal renderer core (PORT-PLAN.md P2.9, P2.10); see render_metal.h.
 *
 * P2.9: the scene and depth textures and one command buffer per scene. P2.10: the shaders and pipelines that stand in
 * for Direct3D 6's fixed-function pipeline as std3D_InitRenderState configured it, and J3DRender_Draw. P2.11: the
 * per-scene vertex and index arena. Textures (P2.12), render states (P2.13) and fog (P2.14) build on these.
 *
 * Vertex and index data go into one shared buffer per scene, appended draw by draw (rdCache sends a few hundred small
 * batches a frame). BeginScene waits for the GPU before anything else, so the arena can start over at every scene; a
 * scene that outgrows it moves on to a larger buffer (the command buffer keeps the old one alive until it completes).
 *
 * The vertex stage turns a pre-transformed vertex back into clip space: x and y from back-buffer pixels to normalised
 * device coordinates, shifted half a pixel because Direct3D 6 put pixel centres on whole coordinates and Metal puts
 * them at +0.5; then every component is multiplied by w = 1/rhw, so the GPU's divide by w gives the same screen
 * position and depth while interpolating colours and texture coordinates perspective-correctly, as Direct3D did with
 * TEXTUREPERSPECTIVE on. A vertex with rhw <= 0 is drawn with w = 1.
 *
 * The scene texture is written by the CPU at BeginScene, so BeginScene first waits for the GPU to finish everything
 * committed so far (the previous scene and its present both read the texture). The game submits a few hundred
 * triangles a frame, so the GPU is long done by then and the wait costs nothing measurable; a ring of scene textures
 * can replace it if that changes.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdlib.h>
#include <string.h>

#include "display_metal_internal.h"
#include "render_metal.h"

// Shaders, compiled from source when the renderer opens.
static NSString* const render_shaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct TLVertex { float sx, sy, sz, rhw; uint color, specular; float tu, tv; }; // D3DTLVERTEX, 32 bytes\n"
     "struct Uniforms { float2 viewportSize; float alphaRef; float pad; };\n"
     "struct VertexOut\n"
     "{\n"
     "    float4 position [[position]];\n"
     "    float4 diffuse;\n"
     "    float4 specular;\n"
     "    float2 uv;\n"
     "    float  pointSize [[point_size]];\n"
     "};\n"
     "vertex VertexOut scene_vertex(uint vid [[vertex_id]], const device TLVertex* verts [[buffer(0)]],\n"
     "                              constant Uniforms& u [[buffer(1)]])\n"
     "{\n"
     "    TLVertex v = verts[vid];\n"
     "    float  w   = v.rhw > 0.0 ? 1.0 / v.rhw : 1.0;\n"
     "    float2 ndc = float2((v.sx + 0.5) / u.viewportSize.x * 2.0 - 1.0, 1.0 - (v.sy + 0.5) / u.viewportSize.y * 2.0);\n"
     "    VertexOut out;\n"
     "    out.position  = float4(ndc * w, v.sz * w, w);\n"
     "    out.diffuse   = unpack_unorm4x8_to_float(v.color).zyxw;    // 0xAARRGGBB: bytes B, G, R, A\n"
     "    out.specular  = unpack_unorm4x8_to_float(v.specular).zyxw;\n"
     "    out.uv        = float2(v.tu, v.tv);\n"
     "    out.pointSize = 1.0;\n"
     "    return out;\n"
     "}\n"
     "// Direct3D's alpha test compared 8-bit alpha: GREATER passes when alpha > ref.\n"
     "static bool alpha_passes(float alpha, float ref) { return rint(alpha * 255.0) > ref; }\n"
     "fragment float4 scene_fragment_flat(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(1)]])\n"
     "{\n"
     "    float4 color = in.diffuse;\n"
     "    if ( !alpha_passes(color.a, u.alphaRef) ) discard_fragment();\n"
     "    return color;\n"
     "}\n"
     "fragment float4 scene_fragment_textured(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(1)]],\n"
     "                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]])\n"
     "{\n"
     "    float4 color = tex.sample(smp, in.uv) * in.diffuse; // D3DTOP_MODULATE, colour and alpha\n"
     "    if ( !alpha_passes(color.a, u.alphaRef) ) discard_fragment();\n"
     "    return color;\n"
     "}\n";

typedef struct RenderUniforms
{
    float viewportSize[2];
    float alphaRef; // 0 to 255
    float pad;
} RenderUniforms;

static id<MTLRenderPipelineState> render_pipelineFlat;
static id<MTLRenderPipelineState> render_pipelineTextured; // used from P2.12
static id<MTLDepthStencilState>   render_depthWrite;
static id<MTLDepthStencilState>   render_depthNoWrite;     // STD3D_RS_ZWRITE_DISABLED, from P2.13
static RenderUniforms             render_uniforms;

#define RENDER_ARENA_SIZE ((size_t)4 << 20) // a level frame uses well under 1 MB
static id<MTLBuffer> render_arena;
static size_t        render_arenaOffset;
static int           render_bSceneNewer; // the scene texture holds a scene the back buffer has not been given

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

    if ( !render_pipelineFlat )
    {
        @autoreleasepool
        {
            NSError*       error   = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:render_shaderSource options:nil error:&error];
            if ( !library )
            {
                NSLog(@"render_metal: shader compile failed: %@", error.localizedDescription);
                J3DRender_Close();
                return 1;
            }

            // Blending as retail enabled it: source alpha over inverse source alpha.
            MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction                                 = [library newFunctionWithName:@"scene_vertex"];
            desc.colorAttachments[0].pixelFormat                = MTLPixelFormatBGRA8Unorm;
            desc.colorAttachments[0].blendingEnabled            = YES;
            desc.colorAttachments[0].sourceRGBBlendFactor       = MTLBlendFactorSourceAlpha;
            desc.colorAttachments[0].destinationRGBBlendFactor  = MTLBlendFactorOneMinusSourceAlpha;
            desc.colorAttachments[0].sourceAlphaBlendFactor     = MTLBlendFactorSourceAlpha;
            desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            desc.depthAttachmentPixelFormat                     = MTLPixelFormatDepth32Float;

            desc.fragmentFunction = [library newFunctionWithName:@"scene_fragment_flat"];
            render_pipelineFlat   = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            desc.fragmentFunction   = [library newFunctionWithName:@"scene_fragment_textured"];
            render_pipelineTextured = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if ( !render_pipelineFlat || !render_pipelineTextured )
            {
                NSLog(@"render_metal: pipeline failed: %@", error.localizedDescription);
                render_pipelineFlat = render_pipelineTextured = nil;
                J3DRender_Close();
                return 1;
            }

            MTLDepthStencilDescriptor* depthDesc = [MTLDepthStencilDescriptor new];
            depthDesc.depthCompareFunction = MTLCompareFunctionLessEqual; // D3DCMP_LESSEQUAL
            depthDesc.depthWriteEnabled    = YES;
            render_depthWrite              = [device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthWriteEnabled    = NO;
            render_depthNoWrite            = [device newDepthStencilStateWithDescriptor:depthDesc];
        }
    }

    if ( !render_arena )
    {
        render_arena = [device newBufferWithLength:RENDER_ARENA_SIZE options:MTLResourceStorageModeShared];
    }

    render_uniforms.viewportSize[0] = (float)width;
    render_uniforms.viewportSize[1] = (float)height;
    render_uniforms.alphaRef        = 0.0f; // D3DRENDERSTATE_ALPHAREF 0

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

    // The fixed state for the whole pass: pre-transformed vertices are not clipped on depth (Direct3D 6 did not clip
    // TL vertices), no culling (D3DCULL_NONE), depth test less-or-equal with writes.
    [render_encoder setDepthClipMode:MTLDepthClipModeClamp];
    [render_encoder setCullMode:MTLCullModeNone];
    [render_encoder setDepthStencilState:render_depthWrite];
    [render_encoder setVertexBytes:&render_uniforms length:sizeof(render_uniforms) atIndex:1];
    [render_encoder setFragmentBytes:&render_uniforms length:sizeof(render_uniforms) atIndex:1];
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
    render_arenaOffset = 0;
    render_bSceneNewer = 0;
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
    render_bSceneNewer = 1;
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

// Copies size bytes into the scene's arena, 16-byte aligned; returns the offset, and the buffer in *pBuffer.
static size_t render_Append(const void* pData, size_t size, id<MTLBuffer>* pBuffer)
{
    size_t offset = (render_arenaOffset + 15) & ~(size_t)15;
    if ( offset + size > render_arena.length )
    {
        size_t newSize = render_arena.length * 2;
        while ( newSize < size )
        {
            newSize *= 2;
        }
        render_arena = [J3DMetal_Device() newBufferWithLength:newSize options:MTLResourceStorageModeShared];
        offset       = 0;
    }

    memcpy((uint8_t*)render_arena.contents + offset, pData, size);
    render_arenaOffset = offset + size;
    *pBuffer = render_arena;
    return offset;
}

int J3DRender_Draw(J3DRenderPrimitive primitive, const J3DRenderVertex* aVerts, size_t numVerts,
    const uint16_t* aIndices, size_t numIndices)
{
    if ( !render_encoder || !render_arena || !aVerts || !numVerts )
    {
        return 1;
    }
    if ( primitive == J3DRENDER_TRIANGLES && (!aIndices || !numIndices) )
    {
        return 1;
    }

    id<MTLBuffer> vertexBuffer = nil;
    size_t vertexOffset = render_Append(aVerts, numVerts * sizeof(J3DRenderVertex), &vertexBuffer);
    [render_encoder setVertexBuffer:vertexBuffer offset:vertexOffset atIndex:0];
    [render_encoder setRenderPipelineState:render_pipelineFlat];

    switch ( primitive )
    {
        case J3DRENDER_TRIANGLES:
        {
            id<MTLBuffer> indexBuffer = nil;
            size_t indexOffset = render_Append(aIndices, numIndices * sizeof(uint16_t), &indexBuffer);
            [render_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                       indexCount:numIndices
                                        indexType:MTLIndexTypeUInt16
                                      indexBuffer:indexBuffer
                                indexBufferOffset:indexOffset];
        } break;

        case J3DRENDER_LINE_STRIP:
            [render_encoder drawPrimitives:MTLPrimitiveTypeLineStrip vertexStart:0 vertexCount:numVerts];
            break;

        case J3DRENDER_POINTS:
            [render_encoder drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:numVerts];
            break;
    }
    return 0;
}

int J3DRender_CopySceneToBackBuffer(uint8_t* pPixels, uint32_t width, uint32_t height, size_t rowSize)
{
    if ( !render_bSceneNewer || render_encoder || !render_sceneColor || render_sceneColor.width != width
        || render_sceneColor.height != height )
    {
        return 1;
    }

    J3DMetal_WaitForGPU();
    [render_sceneColor getBytes:pPixels bytesPerRow:rowSize fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
    render_bSceneNewer = 0;
    return 0;
}

int J3DRender_ReadScene(uint8_t** ppPixels, uint32_t* pWidth, uint32_t* pHeight)
{
    if ( !render_sceneColor || render_encoder )
    {
        return 1; // no scene texture, or a scene still open
    }

    J3DMetal_WaitForGPU();
    const NSUInteger width  = render_sceneColor.width;
    const NSUInteger height = render_sceneColor.height;
    uint8_t* pPixels = (uint8_t*)malloc(width * height * 4);
    if ( !pPixels )
    {
        return 1;
    }

    [render_sceneColor getBytes:pPixels bytesPerRow:width * 4 fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
    *ppPixels = pPixels;
    *pWidth   = (uint32_t)width;
    *pHeight  = (uint32_t)height;
    return 0;
}
