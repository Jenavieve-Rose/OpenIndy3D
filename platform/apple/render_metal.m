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
 * TEXTUREPERSPECTIVE on.
 *
 * Clipping. sithRender does not clip world faces against the near plane (rdClip_FaceToPlane only rejects faces wholly
 * outside the frustum): a face reaching behind the eye arrives with negative rhw on those vertices and their screen
 * positions mirrored. Direct3D 6 clipped pre-transformed vertices in homogeneous space, rebuilding clip coordinates
 * from the screen position and w = 1/rhw; with rdroid's projection (sx = x f / y + cx, rhw = 1 / (32 y)) that rebuild
 * gives the true camera-space point, behind the eye included. The vertex stage does the same, and Metal's clip planes
 * (-w <= x, y <= w) keep only w > 0, so the part behind the eye is cut off exactly. Depth is clamped, not clipped
 * (render_OpenPass): Direct3D did not clip TL vertices on z. A vertex with rhw = 0 is drawn with w = 1.
 *
 * Multisampling (phase 9, the "MSAA" setting). With more than one sample the scene is drawn into a multisampled
 * colour texture and depth beside it, and every render pass stores them and resolves the colour into the scene
 * texture, which stays the frame's picture and what the CPU reads back. A multisampled texture cannot be loaded from
 * the back buffer, so BeginScene uploads it into a texture of its own and the scene's first pass starts by copying it
 * across, every sample of a pixel taking that pixel's colour; edges drawn after it are smoothed. Retail's alpha test
 * stays per pixel: alpha-tested edges keep their steps, as on a 1999 card.
 *
 * Anisotropic filtering (phase 9, "Anisotropy") is the maximum anisotropy of every sampler; 1 is retail's filtering.
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
     "struct Uniforms { float2 viewportSize; float alphaRef; float fogEnabled; float4 fogColor; float fogStart, fogEnd; float2 pad; };\n"
     "struct VertexOut\n"
     "{\n"
     "    float4 position [[position]];\n"
     "    float4 diffuse;\n"
     "    float4 specular;\n"
     "    float2 uv;\n"
     "    float  eyeDepth;  // w, interpolated perspective-correctly: the depth retail's table fog used\n"
     "    float  pointSize [[point_size]];\n"
     "};\n"
     "vertex VertexOut scene_vertex(uint vid [[vertex_id]], const device TLVertex* verts [[buffer(0)]],\n"
     "                              constant Uniforms& u [[buffer(1)]])\n"
     "{\n"
     "    TLVertex v = verts[vid];\n"
     "    float  w   = v.rhw != 0.0 ? 1.0 / v.rhw : 1.0; // negative behind the eye: clipped away below\n"
     "    float2 ndc = float2((v.sx + 0.5) / u.viewportSize.x * 2.0 - 1.0, 1.0 - (v.sy + 0.5) / u.viewportSize.y * 2.0);\n"
     "    VertexOut out;\n"
     "    out.position  = float4(ndc * w, v.sz * w, w);\n"
     "    out.diffuse   = unpack_unorm4x8_to_float(v.color).zyxw;    // 0xAARRGGBB: bytes B, G, R, A\n"
     "    out.specular  = unpack_unorm4x8_to_float(v.specular).zyxw;\n"
     "    out.uv        = float2(v.tu, v.tv);\n"
     "    out.eyeDepth  = w;\n"
     "    out.pointSize = 1.0;\n"
     "    return out;\n"
     "}\n"
     "// Direct3D's alpha test compared 8-bit alpha: GREATER passes when alpha > ref.\n"
     "static bool alpha_passes(float alpha, float ref) { return rint(alpha * 255.0) > ref; }\n"
     "// Direct3D's linear table fog: the fog colour blended in by depth before the alpha blend.\n"
     "static float4 apply_fog(float4 color, float depth, constant Uniforms& u)\n"
     "{\n"
     "    if ( u.fogEnabled == 0.0 ) return color;\n"
     "    float f = clamp((u.fogEnd - depth) / (u.fogEnd - u.fogStart), 0.0, 1.0);\n"
     "    return float4(mix(u.fogColor.rgb, color.rgb, f), color.a);\n"
     "}\n"
     "fragment float4 scene_fragment_flat(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(1)]])\n"
     "{\n"
     "    float4 color = in.diffuse;\n"
     "    if ( !alpha_passes(color.a, u.alphaRef) ) discard_fragment();\n"
     "    return apply_fog(color, in.eyeDepth, u);\n"
     "}\n"
     "fragment float4 scene_fragment_textured(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(1)]],\n"
     "                                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]])\n"
     "{\n"
     "    float4 color = tex.sample(smp, in.uv) * in.diffuse; // D3DTOP_MODULATE, colour and alpha\n"
     "    if ( !alpha_passes(color.a, u.alphaRef) ) discard_fragment();\n"
     "    return apply_fog(color, in.eyeDepth, u);\n"
     "}\n"
     "// Multisampling: the back buffer's pixels into every sample of the scene's first pass.\n"
     "struct CopyOut { float4 position [[position]]; };\n"
     "vertex CopyOut copy_vertex(uint vid [[vertex_id]])\n"
     "{\n"
     "    float2 p = float2((vid << 1) & 2, vid & 2); // one triangle covering clip space\n"
     "    CopyOut out;\n"
     "    out.position = float4(p * 2.0 - 1.0, 1.0, 1.0);\n"
     "    return out;\n"
     "}\n"
     "fragment float4 copy_fragment(CopyOut in [[stage_in]], texture2d<float> tex [[texture(0)]])\n"
     "{\n"
     "    return tex.read(uint2(in.position.xy));\n"
     "}\n";

typedef struct RenderUniforms // the shaders' Uniforms
{
    float viewportSize[2];
    float alphaRef;   // 0 to 255
    float fogEnabled; // 0 or 1
    float fogColor[4];
    float fogStart, fogEnd;
    float pad[2];
} RenderUniforms;
_Static_assert(sizeof(RenderUniforms) == 48, "RenderUniforms must match the shaders' Uniforms");

static id<MTLLibrary>              render_library;
static id<MTLRenderPipelineState> render_pipelineFlat;
static id<MTLRenderPipelineState> render_pipelineTextured;
static id<MTLRenderPipelineState> render_pipelineCopy;     // multisampling only
static NSUInteger                 render_pipelineSamples;  // the sample count the pipelines were made for
static id<MTLDepthStencilState>   render_depthWrite;
static id<MTLDepthStencilState>   render_depthNoWrite;     // STD3D_RS_ZWRITE_DISABLED, from P2.13
static id<MTLDepthStencilState>   render_depthAlways;      // the multisample copy: no test, no write
static RenderUniforms             render_uniforms;
static int                        render_bUniformsDirty;
static int                        render_bDepthWrite = 1;

#define RENDER_ARENA_SIZE ((size_t)4 << 20) // a level frame uses well under 1 MB
static id<MTLBuffer> render_arena;
static size_t        render_arenaOffset;
static int           render_bSceneNewer; // the scene texture holds a scene the back buffer has not been given

static id<MTLTexture>      render_curTexture;   // J3DRender_SetTexture
static id<MTLSamplerState> render_aSamplers[2 * 2 * 2 * 3]; // by J3DRender_SetSampler's arguments, made on first use
static size_t              render_curSampler = 1 * 12 + 0 * 6 + 0 * 3 + J3DRENDER_MIP_NEAREST; // bilinear, wrap, nearest mip
static uint32_t            render_maxAnisotropy = 1; // J3DRender_SetQuality
static uint32_t            render_wantSamples   = 1;

static id<MTLTexture>              render_sceneColor;  // the picture: drawn into, or resolved into when multisampling
static id<MTLTexture>              render_msaaColor;   // multisampling: the colour drawn into
static id<MTLTexture>              render_backBuffer;  // multisampling: the back buffer, copied in by the first pass
static id<MTLTexture>              render_depth;       // multisampled when the colour is
static NSUInteger                  render_samples = 1; // of the open targets
static id<MTLCommandBuffer>        render_commands;
static id<MTLRenderCommandEncoder> render_encoder;
static int                         render_bClearDepth;
static int                         render_bFirstPass;  // the next pass is the scene's first

void J3DRender_SetQuality(uint32_t samples, uint32_t maxAnisotropy)
{
    render_wantSamples = samples ? samples : 1;
    maxAnisotropy      = maxAnisotropy < 1 ? 1 : maxAnisotropy > 16 ? 16 : maxAnisotropy;
    if ( maxAnisotropy != render_maxAnisotropy )
    {
        render_maxAnisotropy = maxAnisotropy;
        for ( size_t i = 0; i < sizeof(render_aSamplers) / sizeof(render_aSamplers[0]); ++i )
        {
            render_aSamplers[i] = nil; // remade on first use with the new anisotropy
        }
    }
}

uint32_t J3DRender_GetSampleCount(void)
{
    return (uint32_t)render_samples;
}

// The largest sample count the device supports that is no more than the one asked for (1, 2, 4 or 8).
static NSUInteger render_SupportedSamples(id<MTLDevice> device, uint32_t want)
{
    NSUInteger samples = want >= 8 ? 8 : want >= 4 ? 4 : want >= 2 ? 2 : 1;
    while ( samples > 1 && !([device supportsTextureSampleCount:samples]) )
    {
        samples /= 2;
    }
    return samples;
}

// Makes the pipelines for the sample count, unless they are made already. 0 on success.
static int render_MakePipelines(id<MTLDevice> device, NSUInteger samples)
{
    if ( render_pipelineFlat && render_pipelineSamples == samples )
    {
        return 0;
    }

    @autoreleasepool
    {
        NSError* error = nil;
        if ( !render_library )
        {
            render_library = [device newLibraryWithSource:render_shaderSource options:nil error:&error];
            if ( !render_library )
            {
                NSLog(@"render_metal: shader compile failed: %@", error.localizedDescription);
                return 1;
            }
        }

        // Blending as retail enabled it: source alpha over inverse source alpha.
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction                                 = [render_library newFunctionWithName:@"scene_vertex"];
        desc.colorAttachments[0].pixelFormat                = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled            = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor       = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor  = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor     = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.depthAttachmentPixelFormat                     = MTLPixelFormatDepth32Float;
        desc.rasterSampleCount                              = samples;

        desc.fragmentFunction   = [render_library newFunctionWithName:@"scene_fragment_flat"];
        render_pipelineFlat     = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        desc.fragmentFunction   = [render_library newFunctionWithName:@"scene_fragment_textured"];
        render_pipelineTextured = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        render_pipelineCopy     = nil;
        if ( render_pipelineFlat && render_pipelineTextured && samples > 1 )
        {
            desc.vertexFunction                      = [render_library newFunctionWithName:@"copy_vertex"];
            desc.fragmentFunction                    = [render_library newFunctionWithName:@"copy_fragment"];
            desc.colorAttachments[0].blendingEnabled = NO;
            render_pipelineCopy = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        }
        if ( !render_pipelineFlat || !render_pipelineTextured || (samples > 1 && !render_pipelineCopy) )
        {
            NSLog(@"render_metal: pipeline failed: %@", error.localizedDescription);
            render_pipelineFlat = render_pipelineTextured = render_pipelineCopy = nil;
            return 1;
        }
        render_pipelineSamples = samples;

        if ( !render_depthWrite )
        {
            MTLDepthStencilDescriptor* depthDesc = [MTLDepthStencilDescriptor new];
            depthDesc.depthCompareFunction = MTLCompareFunctionLessEqual; // D3DCMP_LESSEQUAL
            depthDesc.depthWriteEnabled    = YES;
            render_depthWrite              = [device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthWriteEnabled    = NO;
            render_depthNoWrite            = [device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthCompareFunction = MTLCompareFunctionAlways;
            render_depthAlways             = [device newDepthStencilStateWithDescriptor:depthDesc];
        }
    }
    return 0;
}

int J3DRender_Open(uint32_t width, uint32_t height)
{
    J3DRender_Close();

    id<MTLDevice> device = J3DMetal_Device();
    if ( !device || !width || !height )
    {
        return 1;
    }

    const NSUInteger samples = render_SupportedSamples(device, render_wantSamples);
    if ( render_MakePipelines(device, samples) )
    {
        J3DRender_Close();
        return 1;
    }

    @autoreleasepool
    {
        // CPU-visible storage: the CPU uploads the back buffer into it at every BeginScene (without multisampling) and
        // reads the picture back.
        MTLTextureDescriptor* colorDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                             width:width
                                                                                            height:height
                                                                                         mipmapped:NO];
        colorDesc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDesc.storageMode = J3DMetal_TextureStorageMode();
        render_sceneColor     = [device newTextureWithDescriptor:colorDesc];
        if ( samples > 1 )
        {
            colorDesc.usage       = MTLTextureUsageShaderRead;
            render_backBuffer     = [device newTextureWithDescriptor:colorDesc];
            colorDesc.textureType = MTLTextureType2DMultisample;
            colorDesc.sampleCount = samples;
            colorDesc.usage       = MTLTextureUsageRenderTarget;
            colorDesc.storageMode = MTLStorageModePrivate;
            render_msaaColor      = [device newTextureWithDescriptor:colorDesc];
        }

        MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                             width:width
                                                                                            height:height
                                                                                         mipmapped:NO];
        depthDesc.usage       = MTLTextureUsageRenderTarget;
        depthDesc.storageMode = MTLStorageModePrivate;
        if ( samples > 1 )
        {
            depthDesc.textureType = MTLTextureType2DMultisample;
            depthDesc.sampleCount = samples;
        }
        render_depth = [device newTextureWithDescriptor:depthDesc];
    }

    if ( !render_sceneColor || !render_depth || (samples > 1 && (!render_msaaColor || !render_backBuffer)) )
    {
        J3DRender_Close();
        return 1;
    }
    render_samples = samples;

    if ( !render_arena )
    {
        render_arena = [device newBufferWithLength:RENDER_ARENA_SIZE options:MTLResourceStorageModeShared];
    }

    render_uniforms.viewportSize[0] = (float)width;
    render_uniforms.viewportSize[1] = (float)height;
    render_uniforms.alphaRef        = 0.0f; // D3DRENDERSTATE_ALPHAREF 0
    render_uniforms.fogEnabled      = 0.0f;
    render_bDepthWrite              = 1;

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
    render_msaaColor  = nil;
    render_backBuffer = nil;
    render_depth      = nil;
    render_samples    = 1;
}

// Opens a render pass on the scene, keeping its colour; the depth is cleared if a clear is pending.
static void render_OpenPass(void)
{
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    const int bCopyBackBuffer = render_msaaColor && render_bFirstPass;
    if ( render_msaaColor )
    {
        pass.colorAttachments[0].texture        = render_msaaColor;
        pass.colorAttachments[0].loadAction     = bCopyBackBuffer ? MTLLoadActionDontCare : MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction    = MTLStoreActionStoreAndMultisampleResolve;
        pass.colorAttachments[0].resolveTexture = render_sceneColor;
    }
    else
    {
        pass.colorAttachments[0].texture     = render_sceneColor;
        pass.colorAttachments[0].loadAction  = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    render_bFirstPass = 0;
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
    if ( bCopyBackBuffer )
    {
        [render_encoder setRenderPipelineState:render_pipelineCopy];
        [render_encoder setDepthStencilState:render_depthAlways];
        [render_encoder setFragmentTexture:render_backBuffer atIndex:0];
        [render_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    [render_encoder setDepthStencilState:render_bDepthWrite ? render_depthWrite : render_depthNoWrite];
    render_bUniformsDirty = 1;
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
        id<MTLTexture> target = render_backBuffer ? render_backBuffer : render_sceneColor;
        [target replaceRegion:MTLRegionMake2D(0, 0, target.width, target.height)
                  mipmapLevel:0
                    withBytes:pBackBuffer
                  bytesPerRow:rowSize];
    }

    render_bFirstPass = 1;
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

static id<MTLSamplerState> render_GetSampler(size_t index)
{
    if ( !render_aSamplers[index] )
    {
        const int bBilinear = (int)(index / 12), bClampU = (int)(index / 6 % 2), bClampV = (int)(index / 3 % 2);
        const J3DRenderMipFilter mip = (J3DRenderMipFilter)(index % 3);

        MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
        desc.minFilter    = bBilinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        desc.magFilter    = desc.minFilter;
        desc.mipFilter    = mip == J3DRENDER_MIP_NONE    ? MTLSamplerMipFilterNotMipmapped
                          : mip == J3DRENDER_MIP_NEAREST ? MTLSamplerMipFilterNearest
                                                         : MTLSamplerMipFilterLinear;
        desc.sAddressMode = bClampU ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        desc.tAddressMode = bClampV ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        desc.maxAnisotropy = render_maxAnisotropy;
        render_aSamplers[index] = [J3DMetal_Device() newSamplerStateWithDescriptor:desc];
    }
    return render_aSamplers[index];
}

J3DRenderTexture* J3DRender_CreateTexture(uint32_t width, uint32_t height, size_t numLevels, int bAlpha)
{
    id<MTLDevice> device = J3DMetal_Device();
    if ( !device || !width || !height || !numLevels )
    {
        return NULL;
    }

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                    width:width
                                                                                   height:height
                                                                                mipmapped:numLevels > 1];
    const size_t maxLevels = desc.mipmapLevelCount;
    desc.mipmapLevelCount = numLevels < maxLevels ? numLevels : maxLevels;
    desc.usage            = MTLTextureUsageShaderRead;
    desc.storageMode      = J3DMetal_TextureStorageMode(); // one copy on unified memory: retail's system and video textures
    if ( !bAlpha )
    {
        // X8R8G8B8: the fourth byte is not alpha.
        desc.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleGreen, MTLTextureSwizzleBlue,
            MTLTextureSwizzleOne);
    }

    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    return texture ? (J3DRenderTexture*)CFBridgingRetain(texture) : NULL;
}

void J3DRender_UploadTextureLevel(J3DRenderTexture* pTexture, size_t level, const uint8_t* pPixels, size_t rowSize)
{
    id<MTLTexture> texture = (__bridge id<MTLTexture>)(void*)pTexture;
    if ( !texture || level >= texture.mipmapLevelCount || !pPixels )
    {
        return;
    }

    const NSUInteger width  = texture.width >> level ? texture.width >> level : 1;
    const NSUInteger height = texture.height >> level ? texture.height >> level : 1;
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level withBytes:pPixels bytesPerRow:rowSize];
}

void J3DRender_RetainTexture(J3DRenderTexture* pTexture)
{
    if ( pTexture )
    {
        CFRetain((CFTypeRef)pTexture);
    }
}

void J3DRender_ReleaseTexture(J3DRenderTexture* pTexture)
{
    if ( pTexture )
    {
        if ( (__bridge id<MTLTexture>)(void*)pTexture == render_curTexture )
        {
            render_curTexture = nil;
        }
        CFRelease((CFTypeRef)pTexture); // a command buffer still using it keeps its own reference
    }
}

void J3DRender_SetTexture(J3DRenderTexture* pTexture)
{
    render_curTexture = (__bridge id<MTLTexture>)(void*)pTexture;
}

void J3DRender_SetSampler(int bBilinear, int bClampU, int bClampV, J3DRenderMipFilter mipFilter)
{
    render_curSampler = (size_t)(bBilinear ? 1 : 0) * 12 + (size_t)(bClampU ? 1 : 0) * 6 + (size_t)(bClampV ? 1 : 0) * 3
        + (size_t)mipFilter % 3;
}

void J3DRender_SetDepthWrite(int bWrite)
{
    bWrite = bWrite != 0;
    if ( bWrite != render_bDepthWrite )
    {
        render_bDepthWrite = bWrite;
        if ( render_encoder )
        {
            [render_encoder setDepthStencilState:bWrite ? render_depthWrite : render_depthNoWrite];
        }
    }
}

void J3DRender_SetAlphaRef(int alphaRef)
{
    if ( render_uniforms.alphaRef != (float)alphaRef )
    {
        render_uniforms.alphaRef = (float)alphaRef;
        render_bUniformsDirty    = 1;
    }
}

void J3DRender_SetFog(int bEnabled, float red, float green, float blue, float startDepth, float endDepth)
{
    RenderUniforms u = render_uniforms;
    u.fogEnabled  = bEnabled && endDepth != startDepth ? 1.0f : 0.0f;
    u.fogColor[0] = red;
    u.fogColor[1] = green;
    u.fogColor[2] = blue;
    u.fogColor[3] = 1.0f;
    u.fogStart    = startDepth;
    u.fogEnd      = endDepth;
    if ( memcmp(&u, &render_uniforms, sizeof(u)) != 0 )
    {
        render_uniforms       = u;
        render_bUniformsDirty = 1;
    }
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
    if ( render_bUniformsDirty )
    {
        [render_encoder setVertexBytes:&render_uniforms length:sizeof(render_uniforms) atIndex:1];
        [render_encoder setFragmentBytes:&render_uniforms length:sizeof(render_uniforms) atIndex:1];
        render_bUniformsDirty = 0;
    }
    if ( render_curTexture )
    {
        [render_encoder setRenderPipelineState:render_pipelineTextured];
        [render_encoder setFragmentTexture:render_curTexture atIndex:0];
        [render_encoder setFragmentSamplerState:render_GetSampler(render_curSampler) atIndex:0];
    }
    else
    {
        [render_encoder setRenderPipelineState:render_pipelineFlat];
    }

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

int J3DRender_TakeScene(uint8_t* pPixels, uint32_t width, uint32_t height, size_t rowSize)
{
    if ( !render_sceneColor || render_sceneColor.width != width || render_sceneColor.height != height )
    {
        return 1;
    }

    if ( render_encoder )
    {
        // Inside a scene (the save menu's thumbnail is taken from JonesHud_Process, between StartScene and EndScene):
        // retail's back buffer held what the scene had drawn so far. Submit that much, read it back, and reopen the pass
        // on the same colour and depth so the scene goes on; EndScene still makes the scene texture the picture.
        [render_encoder endEncoding];
        render_encoder = nil;
        J3DMetal_Commit(render_commands);
        if ( pPixels )
        {
            J3DMetal_SyncForCPU(render_sceneColor);
            [render_sceneColor getBytes:pPixels bytesPerRow:rowSize fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
        }
        render_commands = [J3DMetal_Queue() commandBuffer];
        render_OpenPass();
        return 0;
    }

    if ( !render_bSceneNewer )
    {
        return 1;
    }

    if ( pPixels )
    {
        J3DMetal_SyncForCPU(render_sceneColor);
        [render_sceneColor getBytes:pPixels bytesPerRow:rowSize fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
    }
    J3DMetal_SetSceneTexture(nil); // the back buffer is the frame's picture again
    render_bSceneNewer = 0;
    return 0;
}

int J3DRender_ReadScene(uint8_t** ppPixels, uint32_t* pWidth, uint32_t* pHeight)
{
    if ( !render_sceneColor || render_encoder )
    {
        return 1; // no scene texture, or a scene still open
    }

    J3DMetal_SyncForCPU(render_sceneColor);
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
