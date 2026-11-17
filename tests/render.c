/*
 * render (apple builds): the Metal renderer core's shaders and fixed-function state (P2.10), offscreen. Draws into the
 * scene texture with J3DRender_Draw, reads it back (J3DRender_ReadScene) and checks:
 *   - pixel coverage: a quad with Direct3D 6 pixel centres covers exactly the pixels it should, no more
 *   - the diffuse colour's byte order (0xAARRGGBB)
 *   - depth: less-or-equal test and writes, whichever order the triangles come in; a depth clear inside a scene
 *   - the alpha test (alpha 0 draws nothing) and blending (source alpha over inverse source alpha)
 *   - perspective-correct interpolation from rhw
 *   - depth outside 0 to 1 is clamped, not clipped
 *   - a face reaching behind the eye (negative rhw) is clipped as Direct3D clipped it
 *   - line strips and points land on the pixels named
 *   - a scene larger than the per-scene vertex arena (P2.11)
 *   - render state and fog (P2.13, P2.14): depth writes off, the 0xA0 alpha reference, linear fog by eye depth
 *   - textures (P2.12): texel placement, wrap, modulation by the vertex colour, RGB textures opaque whatever their
 *     fourth byte, alpha-0 texels discarded, mip level selection with and without a mip filter
 *   - phase 9: a diagonal edge is smoothed with multisampling and hard without; anisotropic samplers draw; a texture
 *     written to a PNG and read back (the high-resolution texture path) keeps its texels and alpha, with a mip chain
 * Needs no game data and no window. Exit status: the number of failed checks.
 *
 *   render [--samples <1|2|4|8>] [--anisotropy <1-16>]
 * Every check runs the same with multisampling (ctest's render-msaa) and with managed textures, as an Intel Mac's GPU
 * needs them (render-managed, J3D_METAL_MANAGED=1).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <platform/apple/render_metal.h>

#include "harness.h"

static int render_numFailures = 0;

#define CHECK(cond, ...)                                    \
    do                                                      \
    {                                                       \
        if ( !(cond) )                                      \
        {                                                   \
            ++render_numFailures;                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while ( 0 )

#define SIZE 64

static uint8_t  render_aBackBuffer[SIZE * SIZE * 4]; // black
static uint8_t* render_pScene;

// Each scene starts with a depth clear, as the game's frames do (std3D_ClearZBuffer before std3D_StartScene); a scene
// keeps the previous depth otherwise.
static void render_Begin(void)
{
    J3DRender_ClearDepth();
    CHECK(J3DRender_BeginScene(render_aBackBuffer, SIZE * 4) == 0, "BeginScene");
}

static void render_EndAndRead(void)
{
    J3DRender_EndScene();
    free(render_pScene);
    render_pScene = NULL;
    uint32_t width = 0, height = 0;
    CHECK(J3DRender_ReadScene(&render_pScene, &width, &height) == 0 && width == SIZE && height == SIZE, "ReadScene");
}

// The pixel at (x, y) as 0x00RRGGBB.
static uint32_t render_Pixel(int x, int y)
{
    const uint8_t* p = render_pScene + ((size_t)y * SIZE + (size_t)x) * 4; // BGRA
    return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static int render_Near(uint32_t got, uint32_t want, int tolerance)
{
    for ( int shift = 0; shift <= 16; shift += 8 )
    {
        if ( abs((int)((got >> shift) & 0xFF) - (int)((want >> shift) & 0xFF)) > tolerance )
        {
            return 0;
        }
    }
    return 1;
}

static J3DRenderVertex render_Vertex(float x, float y, float z, float rhw, uint32_t color)
{
    J3DRenderVertex v = { x, y, z, rhw, color, 0xFF000000, 0.0f, 0.0f };
    return v;
}

// A quad covering pixels [x0, x1) x [y0, y1): with pixel centres on whole numbers, its edges are half a pixel out.
static void render_Quad(int x0, int y0, int x1, int y1, float z, uint32_t color)
{
    J3DRenderVertex aVerts[4] = {
        render_Vertex(x0 - 0.5f, y0 - 0.5f, z, 1.0f, color), render_Vertex(x1 - 0.5f, y0 - 0.5f, z, 1.0f, color),
        render_Vertex(x1 - 0.5f, y1 - 0.5f, z, 1.0f, color), render_Vertex(x0 - 0.5f, y1 - 0.5f, z, 1.0f, color),
    };
    static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
    CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw");
}

int main(int argc, char** argv)
{
    harness_InstallHandlers();

    uint32_t samples = 1, anisotropy = 1;
    for ( int i = 1; i + 1 < argc; i += 2 )
    {
        if ( !strcmp(argv[i], "--samples") )
        {
            samples = (uint32_t)atoi(argv[i + 1]);
        }
        else if ( !strcmp(argv[i], "--anisotropy") )
        {
            anisotropy = (uint32_t)atoi(argv[i + 1]);
        }
    }
    J3DRender_SetQuality(samples, anisotropy);

    if ( J3DRender_Open(SIZE, SIZE) )
    {
        printf("render: no Metal device\n");
        return 1;
    }
    printf("render: %u sample(s), anisotropy %u, %s textures\n", J3DRender_GetSampleCount(), anisotropy,
        getenv("J3D_METAL_MANAGED") ? "managed" : "default");
    CHECK(samples <= 1 || J3DRender_GetSampleCount() > 1, "multisampling asked for, not given");

    // Coverage and colour byte order.
    render_Begin();
    render_Quad(8, 8, 24, 24, 0.5f, 0xFF336699);
    render_EndAndRead();
    CHECK(render_Pixel(8, 8) == 0x336699 && render_Pixel(23, 23) == 0x336699, "quad inside: %06X %06X",
        render_Pixel(8, 8), render_Pixel(23, 23));
    CHECK(render_Pixel(7, 8) == 0 && render_Pixel(8, 7) == 0 && render_Pixel(24, 23) == 0 && render_Pixel(23, 24) == 0,
        "quad edges: %06X %06X %06X %06X", render_Pixel(7, 8), render_Pixel(8, 7), render_Pixel(24, 23),
        render_Pixel(23, 24));

    // Depth, both orders; then a clear inside the scene lets a farther quad through.
    render_Begin();
    render_Quad(0, 0, 16, 16, 0.2f, 0xFF00FF00);  // near green first
    render_Quad(8, 8, 24, 24, 0.8f, 0xFF0000FF);  // far blue: hidden where they overlap
    render_Quad(32, 40, 48, 56, 0.8f, 0xFF0000FF); // far blue first
    render_Quad(40, 32, 56, 48, 0.2f, 0xFF00FF00); // near green over it
    render_Quad(0, 32, 8, 40, 0.5f, 0xFFFF0000);  // equal depth: the later red wins (less-or-equal)
    render_Quad(0, 32, 8, 40, 0.5f, 0xFFFFFFFF);
    J3DRender_ClearDepth();
    render_Quad(0, 0, 4, 4, 0.9f, 0xFFFF00FF);    // far, after the clear: drawn over the near green
    render_EndAndRead();
    CHECK(render_Pixel(10, 10) == 0x00FF00, "near first: %06X", render_Pixel(10, 10));
    CHECK(render_Pixel(20, 20) == 0x0000FF, "far elsewhere: %06X", render_Pixel(20, 20));
    CHECK(render_Pixel(42, 42) == 0x00FF00, "near second: %06X", render_Pixel(42, 42));
    CHECK(render_Pixel(4, 36) == 0xFFFFFF, "equal depth, later wins: %06X", render_Pixel(4, 36));
    CHECK(render_Pixel(2, 2) == 0xFF00FF, "after the depth clear: %06X", render_Pixel(2, 2));

    // Alpha test and blending over black: alpha 0 draws nothing, alpha 0x80 is half.
    render_Begin();
    render_Quad(0, 0, 8, 8, 0.5f, 0x00FFFFFF);
    render_Quad(16, 0, 24, 8, 0.5f, 0x80FFFFFF);
    render_EndAndRead();
    CHECK(render_Pixel(4, 4) == 0, "alpha 0: %06X", render_Pixel(4, 4));
    CHECK(render_Near(render_Pixel(20, 4), 0x808080, 2), "alpha 0x80: %06X", render_Pixel(20, 4));

    // Perspective: across the scene, red from 0 at rhw 1 to 255 at rhw 0.25.
    render_Begin();
    {
        J3DRenderVertex aVerts[4] = {
            render_Vertex(-0.5f, -0.5f, 0.5f, 1.0f, 0xFF000000), render_Vertex(SIZE - 0.5f, -0.5f, 0.5f, 0.25f, 0xFFFF0000),
            render_Vertex(SIZE - 0.5f, SIZE - 0.5f, 0.5f, 0.25f, 0xFFFF0000), render_Vertex(-0.5f, SIZE - 0.5f, 0.5f, 1.0f, 0xFF000000),
        };
        static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
        CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw");
    }
    render_EndAndRead();
    for ( int x = 8; x < SIZE; x += 24 )
    {
        // Screen fraction t; the attribute interpolates as a/w, normalised by 1/w.
        double t    = (x + 0.5) / SIZE;
        double want = (255.0 * t * 0.25) / ((1.0 - t) * 1.0 + t * 0.25);
        int    got  = (int)(render_Pixel(x, 32) >> 16);
        CHECK(fabs(got - want) <= 2.0, "perspective at x=%d: red %d, expected %.1f (linear would be %.1f)", x, got, want,
            255.0 * t);
    }

    // Depth outside 0..1 is clamped, not clipped; lines and points.
    render_Begin();
    render_Quad(0, 0, 8, 8, 1.5f, 0xFFFFFF00);
    render_Quad(8, 0, 16, 8, -0.5f, 0xFF00FFFF);
    {
        J3DRenderVertex aLine[2] = { render_Vertex(20.0f, 30.0f, 0.5f, 1.0f, 0xFFFF0000), render_Vertex(40.0f, 30.0f, 0.5f, 1.0f, 0xFFFF0000) };
        CHECK(J3DRender_Draw(J3DRENDER_LINE_STRIP, aLine, 2, NULL, 0) == 0, "line");
        J3DRenderVertex aPoints[2] = { render_Vertex(50.0f, 50.0f, 0.5f, 1.0f, 0xFF00FF00), render_Vertex(52.0f, 50.0f, 0.5f, 1.0f, 0xFF00FF00) };
        CHECK(J3DRender_Draw(J3DRENDER_POINTS, aPoints, 2, NULL, 0) == 0, "points");
    }
    render_EndAndRead();
    CHECK(render_Pixel(4, 4) == 0xFFFF00, "depth 1.5 clamped: %06X", render_Pixel(4, 4));
    CHECK(render_Pixel(12, 4) == 0x00FFFF, "depth -0.5 clamped: %06X", render_Pixel(12, 4));
    CHECK(render_Pixel(30, 30) == 0xFF0000 && render_Pixel(30, 29) == 0 && render_Pixel(30, 31) == 0,
        "line on row 30: %06X above %06X below %06X", render_Pixel(30, 30), render_Pixel(30, 29), render_Pixel(30, 31));
    CHECK(render_Pixel(50, 50) == 0x00FF00 && render_Pixel(52, 50) == 0x00FF00 && render_Pixel(51, 50) == 0,
        "points at (50,50) and (52,50): %06X %06X, between %06X", render_Pixel(50, 50), render_Pixel(52, 50),
        render_Pixel(51, 50));

    // A ground plane reaching behind the eye, projected as rdroid projects unclipped world faces (rdClip_FaceToPlane):
    // sx = x f / y + cx, sy = -z f / y + cy, rhw = 1 / (32 y), y forward. The vertices behind the eye (y < 0) have
    // negative rhw and mirrored positions; Direct3D's homogeneous clip cut that part off and drew the rest. The ground
    // must cover the bottom of the screen and nothing above the horizon.
    render_Begin();
    {
        const float f = 32.0f, c = SIZE / 2.0f;
        const float aWorld[4][3] = { { -10, -5, -1 }, { 10, -5, -1 }, { 10, 50, -1 }, { -10, 50, -1 } }; // x, y (forward), z (up)
        J3DRenderVertex aVerts[4];
        for ( int i = 0; i < 4; ++i )
        {
            const float invY = 1.0f / aWorld[i][1];
            aVerts[i] = render_Vertex(aWorld[i][0] * f * invY + c, -aWorld[i][2] * f * invY + c, 0.5f, invY * 0.03125f, 0xFF00FF00);
        }
        static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
        CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw ground");
    }
    render_EndAndRead();
    CHECK(render_Pixel(32, 60) == 0x00FF00 && render_Pixel(2, 62) == 0x00FF00 && render_Pixel(61, 50) == 0x00FF00,
        "ground below the horizon: %06X %06X %06X", render_Pixel(32, 60), render_Pixel(2, 62), render_Pixel(61, 50));
    CHECK(render_Pixel(32, 10) == 0 && render_Pixel(5, 30) == 0 && render_Pixel(60, 2) == 0,
        "nothing above the horizon: %06X %06X %06X", render_Pixel(32, 10), render_Pixel(5, 30), render_Pixel(60, 2));

    // Render state (P2.13) and fog (P2.14).
    render_Begin();
    J3DRender_SetDepthWrite(0);
    render_Quad(0, 0, 8, 8, 0.2f, 0xFFFF0000);    // near, depth not written
    J3DRender_SetDepthWrite(1);
    render_Quad(0, 0, 8, 8, 0.8f, 0xFF0000FF);    // far: drawn over it
    J3DRender_SetAlphaRef(0xA0);
    render_Quad(16, 0, 24, 8, 0.5f, 0xA0FFFFFF);  // alpha 0xA0: not greater than the reference
    render_Quad(24, 0, 32, 8, 0.5f, 0xA1FFFFFF);  // 0xA1: drawn
    J3DRender_SetAlphaRef(0);
    {
        // Fog from depth 1 (clear) to 3 (fog colour); a quad at w = 2 is halfway.
        J3DRender_SetFog(1, 1.0f, 0.0f, 1.0f, 1.0f, 3.0f);
        J3DRenderVertex aVerts[4] = {
            render_Vertex(39.5f, -0.5f, 0.5f, 0.5f, 0xFF000000), render_Vertex(47.5f, -0.5f, 0.5f, 0.5f, 0xFF000000),
            render_Vertex(47.5f, 7.5f, 0.5f, 0.5f, 0xFF000000), render_Vertex(39.5f, 7.5f, 0.5f, 0.5f, 0xFF000000),
        };
        static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
        CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw fogged");
        J3DRender_SetFog(0, 0, 0, 0, 0, 0);
        render_Quad(48, 0, 56, 8, 0.5f, 0xFF000000); // fog off again: black
    }
    render_EndAndRead();
    CHECK(render_Pixel(4, 4) == 0x0000FF, "no depth write: %06X", render_Pixel(4, 4));
    CHECK(render_Pixel(20, 4) == 0 && render_Near(render_Pixel(28, 4), 0xA1A1A1, 1), "alpha reference 0xA0: %06X, 0xA1 (blended over black): %06X",
        render_Pixel(20, 4), render_Pixel(28, 4));
    CHECK(render_Near(render_Pixel(44, 4), 0x800080, 2), "fog halfway: %06X", render_Pixel(44, 4));
    CHECK(render_Pixel(52, 4) == 0, "fog off: %06X", render_Pixel(52, 4));

    // Textures (P2.12). A 2x2 texture: blue, green / red, white (texel bytes B, G, R, A), sampled nearest.
    {
        const uint8_t aTexels[2 * 2 * 4] = { 0xFF, 0, 0, 0xFF, 0, 0xFF, 0, 0xFF, /**/ 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        J3DRenderTexture* pTex = J3DRender_CreateTexture(2, 2, 1, 1);
        CHECK(pTex != NULL, "CreateTexture");
        J3DRender_UploadTextureLevel(pTex, 0, aTexels, 8);

        // RGB (no alpha): the same texels with alpha 0 must still draw, as opaque.
        uint8_t aNoAlpha[sizeof(aTexels)];
        memcpy(aNoAlpha, aTexels, sizeof(aTexels));
        for ( int i = 3; i < (int)sizeof(aNoAlpha); i += 4 ) aNoAlpha[i] = 0;
        J3DRenderTexture* pRGB = J3DRender_CreateTexture(2, 2, 1, 0);
        J3DRender_UploadTextureLevel(pRGB, 0, aNoAlpha, 8);
        // RGBA with those alpha-0 texels: nothing draws.
        J3DRenderTexture* pClear = J3DRender_CreateTexture(2, 2, 1, 1);
        J3DRender_UploadTextureLevel(pClear, 0, aNoAlpha, 8);

        J3DRender_SetSampler(0, 0, 0, J3DRENDER_MIP_NONE);
        render_Begin();
        struct { J3DRenderTexture* pTex; int x0, y0; uint32_t color; float uv; } aDraws[] = {
            { pTex, 0, 0, 0xFFFFFFFF, 1.0f },    // the texture once over 16x16
            { pTex, 16, 0, 0xFF808080, 1.0f },   // modulated by grey: half
            { pRGB, 32, 0, 0xFFFFFFFF, 1.0f },   // RGB: opaque
            { pClear, 48, 0, 0xFFFFFFFF, 1.0f }, // alpha 0: discarded
            { pTex, 0, 16, 0xFFFFFFFF, 2.0f },   // wrapped twice
        };
        for ( size_t i = 0; i < sizeof(aDraws) / sizeof(aDraws[0]); ++i )
        {
            J3DRender_SetTexture(aDraws[i].pTex);
            float x0 = aDraws[i].x0 - 0.5f, y0 = aDraws[i].y0 - 0.5f, uv = aDraws[i].uv;
            J3DRenderVertex aVerts[4] = {
                { x0, y0, 0.5f, 1.0f, aDraws[i].color, 0xFF000000, 0.0f, 0.0f },
                { x0 + 16, y0, 0.5f, 1.0f, aDraws[i].color, 0xFF000000, uv, 0.0f },
                { x0 + 16, y0 + 16, 0.5f, 1.0f, aDraws[i].color, 0xFF000000, uv, uv },
                { x0, y0 + 16, 0.5f, 1.0f, aDraws[i].color, 0xFF000000, 0.0f, uv },
            };
            static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
            CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw textured");
        }
        J3DRender_SetTexture(NULL);
        render_EndAndRead();

        CHECK(render_Pixel(4, 4) == 0x0000FF && render_Pixel(12, 4) == 0x00FF00 && render_Pixel(4, 12) == 0xFF0000
            && render_Pixel(12, 12) == 0xFFFFFF, "texels: %06X %06X %06X %06X", render_Pixel(4, 4), render_Pixel(12, 4),
            render_Pixel(4, 12), render_Pixel(12, 12));
        CHECK(render_Near(render_Pixel(28, 12), 0x808080, 1), "modulated white texel: %06X", render_Pixel(28, 12));
        CHECK(render_Pixel(36, 4) == 0x0000FF && render_Pixel(44, 12) == 0xFFFFFF, "RGB texture opaque: %06X %06X",
            render_Pixel(36, 4), render_Pixel(44, 12));
        CHECK(render_Pixel(52, 4) == 0 && render_Pixel(60, 12) == 0, "alpha-0 texels discarded: %06X %06X",
            render_Pixel(52, 4), render_Pixel(60, 12));
        CHECK(render_Pixel(2, 18) == 0x0000FF && render_Pixel(6, 18) == 0x00FF00 && render_Pixel(10, 18) == 0x0000FF,
            "wrapped: %06X %06X %06X", render_Pixel(2, 18), render_Pixel(6, 18), render_Pixel(10, 18));

        J3DRender_ReleaseTexture(pTex);
        J3DRender_ReleaseTexture(pRGB);
        J3DRender_ReleaseTexture(pClear);
    }

    // Mip levels: an 8x8 texture, level 0 red and level 1 green, drawn over 4x4 pixels (two texels a pixel) picks
    // level 1 with a mip filter and level 0 without.
    {
        uint8_t aLevel0[8 * 8 * 4], aLevel1[4 * 4 * 4];
        for ( int i = 0; i < 8 * 8; ++i ) { aLevel0[i * 4] = 0; aLevel0[i * 4 + 1] = 0; aLevel0[i * 4 + 2] = 0xFF; aLevel0[i * 4 + 3] = 0xFF; }
        for ( int i = 0; i < 4 * 4; ++i ) { aLevel1[i * 4] = 0; aLevel1[i * 4 + 1] = 0xFF; aLevel1[i * 4 + 2] = 0; aLevel1[i * 4 + 3] = 0xFF; }
        J3DRenderTexture* pMip = J3DRender_CreateTexture(8, 8, 2, 1);
        J3DRender_UploadTextureLevel(pMip, 0, aLevel0, 32);
        J3DRender_UploadTextureLevel(pMip, 1, aLevel1, 16);

        render_Begin();
        J3DRender_SetTexture(pMip);
        for ( int pass = 0; pass < 2; ++pass )
        {
            J3DRender_SetSampler(0, 1, 1, pass == 0 ? J3DRENDER_MIP_NEAREST : J3DRENDER_MIP_NONE);
            float x0 = pass * 8 - 0.5f, y0 = 40 - 0.5f;
            J3DRenderVertex aVerts[4] = {
                { x0, y0, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 0.0f }, { x0 + 4, y0, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 0.0f },
                { x0 + 4, y0 + 4, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 1.0f }, { x0, y0 + 4, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 1.0f },
            };
            static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
            CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw mip");
        }
        J3DRender_SetTexture(NULL);
        J3DRender_SetSampler(1, 0, 0, J3DRENDER_MIP_NEAREST);
        render_EndAndRead();
        J3DRender_ReleaseTexture(pMip);
        CHECK(render_Pixel(1, 41) == 0x00FF00, "mip filter on: level 1 (green), got %06X", render_Pixel(1, 41));
        CHECK(render_Pixel(9, 41) == 0xFF0000, "mip filter off: level 0 (red), got %06X", render_Pixel(9, 41));
    }

    // Taking the scene back inside a scene (the save menu's thumbnail, P2.17): the copy has what was drawn so far, and
    // the scene goes on, depth included.
    render_Begin();
    render_Quad(0, 0, 8, 8, 0.2f, 0xFF00FF00);
    {
        uint8_t* pMid = (uint8_t*)calloc(SIZE * SIZE, 4);
        CHECK(J3DRender_TakeScene(pMid, SIZE, SIZE, SIZE * 4) == 0, "TakeScene inside a scene");
        CHECK(pMid && pMid[(4 * SIZE + 4) * 4 + 1] == 0xFF && pMid[(4 * SIZE + 20) * 4 + 1] == 0, "mid-scene copy");
        free(pMid);
    }
    render_Quad(0, 0, 8, 8, 0.8f, 0xFFFF0000);   // behind the green: the depth survived
    render_Quad(16, 0, 24, 8, 0.5f, 0xFF0000FF); // drawn after the take
    render_EndAndRead();
    CHECK(render_Pixel(4, 4) == 0x00FF00 && render_Pixel(20, 4) == 0x0000FF, "after the mid-scene take: %06X %06X",
        render_Pixel(4, 4), render_Pixel(20, 4));

    // A scene larger than the 4 MB vertex and index arena: 50,000 draws of about 140 bytes each, the last one white.
    // Every draw must land, and the scene after it must start over in the arena.
    render_Begin();
    for ( int i = 0; i < 50000; ++i )
    {
        render_Quad(60, 60, 64, 64, 0.5f, i == 49999 ? 0xFFFFFFFF : 0xFF000000 | (uint32_t)i);
    }
    render_EndAndRead();
    CHECK(render_Pixel(62, 62) == 0xFFFFFF, "the last of 50,000 draws: %06X", render_Pixel(62, 62));
    render_Begin();
    render_Quad(0, 0, 4, 4, 0.5f, 0xFF123456);
    render_EndAndRead();
    CHECK(render_Pixel(1, 1) == 0x123456, "the scene after the overflow: %06X", render_Pixel(1, 1));

    // Phase 9: a white triangle whose long edge runs diagonally across the pixels. Without multisampling every pixel
    // is black or white; with it the pixels the edge crosses are in between.
    {
        render_Begin();
        J3DRenderVertex aVerts[3] = {
            render_Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFF), render_Vertex(48.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFF),
            render_Vertex(0.0f, 31.0f, 0.5f, 1.0f, 0xFFFFFFFF),
        };
        static const uint16_t aIndices[3] = { 0, 1, 2 };
        CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 3, aIndices, 3) == 0, "Draw edge");
        render_EndAndRead();
        int numBetween = 0;
        for ( int y = 0; y < 32; ++y )
        {
            for ( int x = 0; x < 48; ++x )
            {
                const uint32_t g = (render_Pixel(x, y) >> 8) & 0xFF;
                numBetween += g > 0 && g < 0xFF;
            }
        }
        if ( J3DRender_GetSampleCount() > 1 )
        {
            CHECK(numBetween >= 20, "multisampled edge: %d pixel(s) in between", numBetween);
        }
        else
        {
            CHECK(numBetween == 0, "edge without multisampling: %d pixel(s) in between", numBetween);
        }
        CHECK(render_Pixel(2, 2) == 0xFFFFFF && render_Pixel(40, 28) == 0, "edge inside and out: %06X %06X",
            render_Pixel(2, 2), render_Pixel(40, 28));
    }

    // Phase 9: a texture written to a PNG and read back, as a high-resolution replacement is. Texels: opaque colours,
    // an alpha-0 texel (discarded) and a half-transparent red (blended over the black).
    {
        static const uint8_t aTexels[4 * 4 * 4] = {
            // B, G, R, A
            0x00, 0x00, 0xFF, 0xFF,  0x00, 0xFF, 0x00, 0xFF,  0xFF, 0x00, 0x00, 0xFF,  0x40, 0x80, 0xC0, 0xFF,
            0x11, 0x22, 0x33, 0xFF,  0xFF, 0xFF, 0xFF, 0x00,  0x00, 0x00, 0xC8, 0x80,  0x00, 0x00, 0x00, 0xFF,
            0x00, 0x00, 0xFF, 0xFF,  0x00, 0xFF, 0x00, 0xFF,  0xFF, 0x00, 0x00, 0xFF,  0x40, 0x80, 0xC0, 0xFF,
            0x11, 0x22, 0x33, 0xFF,  0xFF, 0xFF, 0xFF, 0x00,  0x00, 0x00, 0xC8, 0x80,  0x00, 0x00, 0x00, 0xFF,
        };
        J3DRenderTexture* pSource = J3DRender_CreateTexture(4, 4, 1, 1);
        J3DRender_UploadTextureLevel(pSource, 0, aTexels, 16);
        char aPath[1024];
        const char* pTmp = getenv("TMPDIR");
        snprintf(aPath, sizeof(aPath), "%s/j3d-render-%d.png", pTmp && *pTmp ? pTmp : "/tmp", (int)getpid());
        CHECK(J3DRender_WriteTextureImage(pSource, aPath, 1) == 0, "WriteTextureImage %s", aPath);
        J3DRender_ReleaseTexture(pSource);

        uint8_t* pFile = NULL;
        size_t   fileSize = 0;
        FILE*    pStream = fopen(aPath, "rb");
        if ( pStream )
        {
            fseek(pStream, 0, SEEK_END);
            fileSize = (size_t)ftell(pStream);
            fseek(pStream, 0, SEEK_SET);
            pFile = (uint8_t*)malloc(fileSize);
            fileSize = pFile ? fread(pFile, 1, fileSize, pStream) : 0;
            fclose(pStream);
        }
        remove(aPath);

        uint32_t width = 0, height = 0;
        J3DRenderTexture* pImage = J3DRender_CreateTextureFromImage(pFile, fileSize, 1, 1, &width, &height);
        // The same image for a material without alpha: opaque, the half-transparent texel at its full colour
        J3DRenderTexture* pOpaque = J3DRender_CreateTextureFromImage(pFile, fileSize, 0, 0, &width, &height);
        free(pFile);
        CHECK(pImage && width == 4 && height == 4, "CreateTextureFromImage: %ux%u", width, height);
        CHECK(!J3DRender_CreateTextureFromImage("not an image", 12, 1, 0, &width, &height), "a bad image is refused");
        if ( pImage )
        {
            render_Begin();
            J3DRender_SetTexture(pImage);
            J3DRender_SetSampler(0, 1, 1, J3DRENDER_MIP_NONE);
            J3DRenderVertex aVerts[4] = {
                { -0.5f, -0.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 0.0f }, { 15.5f, -0.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 0.0f },
                { 15.5f, 15.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 1.0f }, { -0.5f, 15.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 1.0f },
            };
            static const uint16_t aIndices[6] = { 0, 1, 2, 0, 2, 3 };
            CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aVerts, 4, aIndices, 6) == 0, "Draw image");
            // Minified 4:1 with the generated mip chain: the 2x2 level averages the texel pairs
            J3DRender_SetSampler(1, 1, 1, J3DRENDER_MIP_NEAREST);
            J3DRenderVertex aSmall[4] = {
                { 31.5f, -0.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 0.0f }, { 32.5f, -0.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 0.0f },
                { 32.5f, 0.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 1.0f }, { 31.5f, 0.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 1.0f },
            };
            CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aSmall, 4, aIndices, 6) == 0, "Draw image small");
            if ( pOpaque )
            {
                J3DRender_SetTexture(pOpaque);
                J3DRender_SetSampler(0, 1, 1, J3DRENDER_MIP_NONE);
                J3DRenderVertex aOpaque[4] = {
                    { 15.5f, 31.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 0.0f }, { 31.5f, 31.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 0.0f },
                    { 31.5f, 47.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 1.0f, 1.0f }, { 15.5f, 47.5f, 0.5f, 1.0f, 0xFFFFFFFF, 0xFF000000, 0.0f, 1.0f },
                };
                CHECK(J3DRender_Draw(J3DRENDER_TRIANGLES, aOpaque, 4, aIndices, 6) == 0, "Draw opaque image");
            }
            J3DRender_SetTexture(NULL);
            J3DRender_SetSampler(1, 0, 0, J3DRENDER_MIP_NEAREST);
            render_EndAndRead();
            J3DRender_ReleaseTexture(pImage);
            J3DRender_ReleaseTexture(pOpaque);

            CHECK(render_Pixel(1, 1) == 0xFF0000 && render_Pixel(5, 1) == 0x00FF00 && render_Pixel(9, 1) == 0x0000FF
                && render_Pixel(13, 1) == 0xC08040 && render_Pixel(1, 5) == 0x332211,
                "image texels: %06X %06X %06X %06X %06X", render_Pixel(1, 1), render_Pixel(5, 1), render_Pixel(9, 1),
                render_Pixel(13, 1), render_Pixel(1, 5));
            CHECK(render_Pixel(5, 5) == 0, "image alpha-0 texel discarded: %06X", render_Pixel(5, 5));
            CHECK(render_Near(render_Pixel(9, 5), 0x640000, 2), "image half-transparent texel: %06X", render_Pixel(9, 5));
            CHECK(render_Pixel(32, 0) != 0, "image minified through its mip chain: %06X", render_Pixel(32, 0));
            CHECK(pOpaque && render_Near(render_Pixel(25, 37), 0xC80000, 1) && render_Pixel(17, 33) == 0xFF0000,
                "image for an opaque material: %06X %06X", render_Pixel(25, 37), render_Pixel(17, 33));
        }
    }

    free(render_pScene);
    J3DRender_Close();
    printf("render: %d failure(s)\n", render_numFailures);
    return render_numFailures;
}
