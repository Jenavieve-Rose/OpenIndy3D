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

// Image quality, port settings beyond retail (phase 9): the multisample count (1 for none; 2, 4 or 8, lowered to what
// the device supports) used from the next J3DRender_Open, and the samplers' maximum anisotropy (1, retail's filtering,
// to 16), used from the next draw. Both 1 by default.
void     J3DRender_SetQuality(uint32_t samples, uint32_t maxAnisotropy);
uint32_t J3DRender_GetSampleCount(void); // of the open scene targets; 1 when not multisampled

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

/* ---- Drawing (P2.10) ---------------------------------------------------------------------------------------------- */

// A pre-transformed, lit vertex: the layout of Direct3D's D3DTLVERTEX, which rdroid hands to std3D (std3D_metal.c
// checks the two match). sx, sy are back-buffer pixels with pixel centres on whole numbers (Direct3D 6); sz is depth,
// 0 near to 1 far; rhw is 1/w, for perspective-correct interpolation; color is the diffuse colour and specular the
// specular, both 0xAARRGGBB; tu, tv are texture coordinates.
typedef struct J3DRenderVertex
{
    float    sx, sy, sz, rhw;
    uint32_t color;
    uint32_t specular;
    float    tu, tv;
} J3DRenderVertex;

typedef enum J3DRenderPrimitive
{
    J3DRENDER_TRIANGLES,  // indexed triangle list
    J3DRENDER_LINE_STRIP, // indices ignored
    J3DRENDER_POINTS,     // indices ignored
} J3DRenderPrimitive;

// Draws into the open scene with the fixed-function model of retail's Direct3D setup (std3D_InitRenderState):
// colour = texture x diffuse (the diffuse alone with no texture set), alpha-tested (passes when alpha > the reference,
// 0 by default), blended source alpha over inverse source alpha, depth-tested less-or-equal and written, no culling.
// Pre-transformed vertices are never clipped on depth; depth outside 0 to 1 is clamped. Returns 0 when drawn.
int J3DRender_Draw(J3DRenderPrimitive primitive, const J3DRenderVertex* aVerts, size_t numVerts,
    const uint16_t* aIndices, size_t numIndices);

/* ---- Textures and samplers (P2.12) ------------------------------------------------------------------------------ */

// A texture: an id<MTLTexture> held by reference count. The handle is stored in tSystemTexture's
// LPDIRECT3DTEXTURE2 fields, as retail stored its Direct3D textures there.
typedef struct J3DRenderTexture J3DRenderTexture;

// A width x height BGRA texture with numLevels mip levels. bAlpha 0: the fourth byte is not alpha, and the texture
// reads as opaque. Returns NULL on failure. The caller holds one reference.
J3DRenderTexture* J3DRender_CreateTexture(uint32_t width, uint32_t height, size_t numLevels, int bAlpha);
// Uploads mip level 'level' (32 bpp BGRA rows of rowSize bytes, at least the level's size).
void J3DRender_UploadTextureLevel(J3DRenderTexture* pTexture, size_t level, const uint8_t* pPixels, size_t rowSize);
void J3DRender_RetainTexture(J3DRenderTexture* pTexture);
void J3DRender_ReleaseTexture(J3DRenderTexture* pTexture);

// Phase 9, high-resolution textures (texture_image.m). A texture from an image file's bytes (PNG, or any format
// ImageIO reads), as BGRA: with the image's alpha unpremultiplied when bAlpha, opaque otherwise; with a full mip chain
// made by the GPU when bMipmaps. Its size goes to *pWidth, *pHeight. NULL when the image cannot be read or is larger
// than 16384 on a side. The caller holds one reference.
J3DRenderTexture* J3DRender_CreateTextureFromImage(const void* pData, size_t size, int bAlpha, int bMipmaps,
    uint32_t* pWidth, uint32_t* pHeight);
// Writes a texture's top level to a PNG file, with its alpha when bAlpha, opaque otherwise. 0 on success.
int J3DRender_WriteTextureImage(J3DRenderTexture* pTexture, const char* pPath, int bAlpha);

// The texture the following draws use; NULL draws with the vertex colours alone.
void J3DRender_SetTexture(J3DRenderTexture* pTexture);

typedef enum J3DRenderMipFilter
{
    J3DRENDER_MIP_NONE,    // the top level only
    J3DRENDER_MIP_NEAREST, // the nearest level (retail's "bilinear" mipmap filter, D3DTFP_POINT)
    J3DRENDER_MIP_LINEAR,  // blended between levels (retail's "trilinear", D3DTFP_LINEAR)
} J3DRenderMipFilter;

// The sampler for the following draws: bilinear or nearest texel filtering, clamp or wrap per axis, and the mip filter.
void J3DRender_SetSampler(int bBilinear, int bClampU, int bClampV, J3DRenderMipFilter mipFilter);

/* ---- Render state and fog (P2.13, P2.14) -------------------------------------------------------------------------- */

// Depth writes for the following draws (the depth test stays on). On by default.
void J3DRender_SetDepthWrite(int bWrite);
// The alpha test's reference, 0 to 255: a fragment draws when its 8-bit alpha is greater. 0 by default.
void J3DRender_SetAlphaRef(int alphaRef);
// Linear fog for the following draws, by eye depth (w = 1/rhw): full colour at startDepth and nearer, the fog colour
// (0 to 1 per channel) at endDepth and beyond. Off by default.
void J3DRender_SetFog(int bEnabled, float red, float green, float blue, float startDepth, float endDepth);

// When the scene texture holds a scene drawn since the last BeginScene: waits for the GPU, copies it into the back
// buffer (32 bpp, the scene's size; pPixels NULL: no copy, the caller overwrites it all) and hands the frame's picture
// back to the back buffer, so the next present shows the back buffer. Inside a scene it copies what the scene has
// drawn so far and the scene goes on (EndScene still makes it the picture). 0 when there was a scene to take, 1
// otherwise. stdDisplay calls it before the CPU touches the back buffer (P2.17).
int J3DRender_TakeScene(uint8_t* pPixels, uint32_t width, uint32_t height, size_t rowSize);

// Waits for the GPU and copies the scene texture (BGRA, the back buffer's size) into a malloc'd buffer the caller
// frees. 0 on success. For tests and diagnostics.
int J3DRender_ReadScene(uint8_t** ppPixels, uint32_t* pWidth, uint32_t* pHeight);

#endif // J3D_PLATFORM_RENDER_METAL_H
