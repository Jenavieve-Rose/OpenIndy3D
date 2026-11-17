/*
 * present (apple builds): the Metal present path (P2.6) through stdDisplay, in a real window. Four coloured quadrants
 * are drawn into the back buffer, stdDisplay_Update presents them, and the drawable is read back
 * (J3DMetal_ArmCapture) and sampled at the centre of each quadrant: once with the window at the back buffer's size,
 * once with it at twice that, so the presenter scales as it does fullscreen.
 *
 * With the window made twice as wide as tall, the 4:3 picture must sit between black bars and mouse positions map
 * through the same rectangle (P2.20).
 *
 * Then the renderer and the frame order (P2.9, P2.17): std3D opens on the Metal device, a scene begins (taking the back
 * buffer's pattern), clears depth and ends; then one quadrant is filled grey on the CPU, as retail's CPU writes after a
 * scene landed on the scene's picture. The present must show the scene's picture with the grey quadrant on it. A
 * second, empty scene must start from that picture, not from back-buffer pixels nobody announced (P2.22). Also checks that the drawable covers the window in
 * pixels and that the memory queries report the Metal device. Needs no game data.
 *
 * It needs a window server: without one (an SSH session, for example) it is skipped (exit 77), as it is when the
 * window gets no drawable. Exit status otherwise: the number of failed checks.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <std/General/std.h>
#include <std/Win95/std3D.h>
#include <std/Win95/stdDisplay.h>
#include <std/Win95/stdWin95.h>

#include <platform/apple/display_metal.h>
#include <platform/apple/stdDisplay_sdl.h>

#include "harness.h"

#define PRESENT_SKIP 77

static int present_numFailures = 0;

#define CHECK(cond, ...)                                    \
    do                                                      \
    {                                                       \
        if ( !(cond) )                                      \
        {                                                   \
            ++present_numFailures;                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while ( 0 )

#define BB_WIDTH  160
#define BB_HEIGHT 120

static int present_Near(int a, int b)
{
    return abs(a - b) <= 2;
}

// Lets the window settle, presents twice (the second captured; bOnce: only the captured one) and samples the centre of each quadrant. Returns 0, or
// -1 when the window gave no drawable.
static int present_CaptureAndCheck(SDL_Window* pWindow, const StdRect* aQuadrants, const uint32_t* aColors,
    uint32_t* pWidth, uint32_t* pHeight, int bOnce)
{
    for ( int i = 0; i < 10; ++i )
    {
        SDL_Event event;
        while ( SDL_PollEvent(&event) ) {}
        SDL_Delay(10);
    }
    if ( !bOnce )
    {
        CHECK(stdDisplay_Update() == 0, "Update");
    }
    J3DMetal_ArmCapture();
    CHECK(stdDisplay_Update() == 0, "Update");

    uint8_t* pPixels = NULL;
    uint32_t width = 0, height = 0;
    if ( J3DMetal_TakeCapture(&pPixels, &width, &height) )
    {
        return -1;
    }

    int pixelWidth = 0, pixelHeight = 0;
    SDL_GetWindowSizeInPixels(pWindow, &pixelWidth, &pixelHeight);
    CHECK((int)width == pixelWidth && (int)height == pixelHeight, "drawable %ux%u, window %dx%d pixels", width, height,
        pixelWidth, pixelHeight);

    for ( int i = 0; i < 4; ++i )
    {
        // The quadrant's centre, scaled to the drawable.
        uint32_t x = (uint32_t)((aQuadrants[i].left + aQuadrants[i].right / 2) * (int64_t)width / BB_WIDTH);
        uint32_t y = (uint32_t)((aQuadrants[i].top + aQuadrants[i].bottom / 2) * (int64_t)height / BB_HEIGHT);
        const uint8_t* p = pPixels + ((size_t)y * width + x) * 4; // BGRA
        int wantR = (int)(aColors[i] >> 16 & 0xFF), wantG = (int)(aColors[i] >> 8 & 0xFF), wantB = (int)(aColors[i] & 0xFF);
        CHECK(present_Near(p[2], wantR) && present_Near(p[1], wantG) && present_Near(p[0], wantB) && p[3] == 0xFF,
            "%ux%u: quadrant %d at (%u,%u): RGBA %u %u %u %u, expected %d %d %d 255", width, height, i, x, y, p[2], p[1],
            p[0], p[3], wantR, wantG, wantB);
    }
    free(pPixels);

    *pWidth  = width;
    *pHeight = height;
    return 0;
}

int main(void)
{
    harness_InstallHandlers();
    if ( harness_StartupStd() )
    {
        return 1;
    }

    if ( !getenv("SDL_VIDEO_DRIVER") )
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "cocoa");
    }
    if ( !SDL_Init(SDL_INIT_VIDEO) )
    {
        printf("present: skipped, no window server (%s)\n", SDL_GetError());
        return PRESENT_SKIP;
    }

    SDL_Window* pWindow = SDL_CreateWindow("OpenIndy3D present test", BB_WIDTH, BB_HEIGHT, SDL_WINDOW_METAL);
    if ( !pWindow )
    {
        printf("present: skipped, no window (%s)\n", SDL_GetError());
        SDL_Quit();
        return PRESENT_SKIP;
    }
    stdWin95_SetWindow((HWND)pWindow);

    CHECK(stdDisplay_Startup() == 1, "Startup");
    CHECK(stdDisplay_Open(0) == 1, "Open");
    stdDisplay_SetDefaultResolution(BB_WIDTH, BB_HEIGHT);
    CHECK(stdDisplay_SetMode(0, 0, 1) == 0, "SetMode");
    CHECK(J3DMetal_IsPresenterOpen(), "the presenter opened with the mode");

    size_t total = 0, freeMem = 0;
    CHECK(stdDisplay_GetTextureMemory(&total, &freeMem) == 0 && total > 0 && freeMem <= total,
        "texture memory %zu of %zu", freeMem, total);

    // Red, green, blue and white quadrants (the back buffer is BGRX: 0x00RRGGBB).
    const StdRect aQuadrants[4] = {
        { 0, 0, BB_WIDTH / 2, BB_HEIGHT / 2 }, { BB_WIDTH / 2, 0, BB_WIDTH / 2, BB_HEIGHT / 2 },
        { 0, BB_HEIGHT / 2, BB_WIDTH / 2, BB_HEIGHT / 2 }, { BB_WIDTH / 2, BB_HEIGHT / 2, BB_WIDTH / 2, BB_HEIGHT / 2 },
    };
    const uint32_t aColors[4] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF };
    for ( int i = 0; i < 4; ++i )
    {
        stdDisplay_BackBufferFill(aColors[i], &aQuadrants[i]);
    }

    // At the back buffer's size, then with the window at twice that, so the presenter has to scale (as fullscreen does).
    uint32_t width = 0, height = 0;
    int res = present_CaptureAndCheck(pWindow, aQuadrants, aColors, &width, &height, 0);
    if ( res == 0 )
    {
        SDL_SetWindowSize(pWindow, 2 * BB_WIDTH, 2 * BB_HEIGHT);
        uint32_t scaledWidth = 0, scaledHeight = 0;
        res = present_CaptureAndCheck(pWindow, aQuadrants, aColors, &scaledWidth, &scaledHeight, 0);
        CHECK(res != 0 || (scaledWidth >= 2 * width && scaledHeight >= 2 * height), "scaled drawable %ux%u", scaledWidth,
            scaledHeight);
        width = scaledWidth;
        height = scaledHeight;

        // Pillarbox (P2.20, the default): a window twice as wide as it is tall shows the 4:3 picture centred between
        // black bars, and mouse positions map through the same rectangle.
        SDL_SetWindowSize(pWindow, 2 * BB_WIDTH * 2, BB_HEIGHT * 2);
        for ( int i = 0; i < 10; ++i ) { SDL_Event event; while ( SDL_PollEvent(&event) ) {} SDL_Delay(10); }
        CHECK(stdDisplay_Update() == 0, "Update");
        J3DMetal_ArmCapture();
        CHECK(stdDisplay_Update() == 0, "Update");
        uint8_t* pWide = NULL;
        uint32_t wideWidth = 0, wideHeight = 0;
        if ( J3DMetal_TakeCapture(&pWide, &wideWidth, &wideHeight) == 0 )
        {
            float x, y, w, h;
            J3DMetal_PictureRect(BB_WIDTH, BB_HEIGHT, (float)wideWidth, (float)wideHeight, 1, &x, &y, &w, &h);
            CHECK(w < wideWidth && x > 0.0f, "pillarboxed picture %gx%g at %g in %ux%u", w, h, x, wideWidth, wideHeight);
            const uint8_t* pBar = pWide + ((size_t)(wideHeight / 2) * wideWidth + (size_t)(x / 2)) * 4;
            CHECK(pBar[0] == 0 && pBar[1] == 0 && pBar[2] == 0, "left bar is black: %u %u %u", pBar[2], pBar[1], pBar[0]);
            const uint8_t* pRed = pWide + ((size_t)(y + h / 4) * wideWidth + (size_t)(x + w / 4)) * 4; // quadrant 0 centre
            CHECK(present_Near(pRed[2], 0xFF) && present_Near(pRed[1], 0) && present_Near(pRed[0], 0),
                "quadrant 0 inside the bars: %u %u %u", pRed[2], pRed[1], pRed[0]);
            free(pWide);

            int pointsW = 0, pointsH = 0;
            SDL_GetWindowSize(pWindow, &pointsW, &pointsH);
            float px, py, pw, ph;
            J3DMetal_PictureRect(BB_WIDTH, BB_HEIGHT, (float)pointsW, (float)pointsH, 1, &px, &py, &pw, &ph);
            float mouseX = px + 100.0f * pw / BB_WIDTH, mouseY = py + 30.0f * ph / BB_HEIGHT;
            J3DApple_WindowToBackBuffer(&mouseX, &mouseY);
            CHECK(fabsf(mouseX - 100.0f) < 0.01f && fabsf(mouseY - 30.0f) < 0.01f, "pillarboxed mouse maps to (%g,%g)", mouseX, mouseY);
        }
        SDL_SetWindowSize(pWindow, 2 * BB_WIDTH, 2 * BB_HEIGHT);

        // Mouse positions (P2.15): the window is now twice the back buffer, in points.
        float mouseX = 2.0f * 100.0f, mouseY = 2.0f * 30.0f;
        J3DApple_WindowToBackBuffer(&mouseX, &mouseY);
        CHECK(mouseX == 100.0f && mouseY == 30.0f, "window (200,60) maps to back buffer (%g,%g), expected (100,30)", mouseX, mouseY);
    }
    if ( res < 0 )
    {
        printf("present: skipped, the window gave no drawable\n");
        stdDisplay_Close();
        stdDisplay_Shutdown();
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
        return present_numFailures ? present_numFailures : PRESENT_SKIP;
    }

    // P2.9: a scene's picture is what gets presented.
    if ( res == 0 )
    {
        CHECK(std3D_Startup() == 1, "std3D_Startup");
        CHECK(std3D_GetNumDevices() == 1 && strstr(std3D_GetAllDevices()[0].deviceDescription, "HAL") != NULL,
            "one Metal HAL device");
        CHECK(std3D_Open(0) == 1, "std3D_Open");
        CHECK(std3D_StartScene() == 0, "StartScene");
        std3D_ClearZBuffer(); // within the scene: a second pass
        std3D_EndScene();
        stdDisplay_BackBufferFill(0x00808080, &aQuadrants[3]); // after the scene: lands on its picture
        const uint32_t aAfter[4] = { aColors[0], aColors[1], aColors[2], 0x00808080 };
        uint32_t sceneWidth = 0, sceneHeight = 0;
        res = present_CaptureAndCheck(pWindow, aQuadrants, aAfter, &sceneWidth, &sceneHeight, 1);
        CHECK(res == 0, "the scene frame was presented");

        // P2.22: a scene starts from the previous scene's picture unless the CPU touched the back buffer through
        // stdDisplay. Paint the back buffer directly (no stdDisplay call, so nothing marks it changed), run an empty
        // scene: the picture must be unchanged.
        CHECK(std3D_StartScene() == 0, "StartScene"); // takes the back buffer the fill above changed
        std3D_EndScene();
        CHECK(stdDisplay_Update() == 0, "Update");
        memset(stdDisplay_backBuffer.pPixels, 0x40, stdDisplay_backBuffer.rasterInfo.size);
        CHECK(std3D_StartScene() == 0, "StartScene");
        std3D_EndScene();
        res = present_CaptureAndCheck(pWindow, aQuadrants, aAfter, &sceneWidth, &sceneHeight, 1);
        CHECK(res == 0, "the second scene frame was presented");
        std3D_Close();
        std3D_Shutdown();
    }

    stdDisplay_Close();
    CHECK(!J3DMetal_IsPresenterOpen(), "the presenter closed with the display");
    stdDisplay_Shutdown();
    SDL_DestroyWindow(pWindow);
    SDL_Quit();

    printf("present: %d failure(s), drawable %ux%u\n", present_numFailures, width, height);
    return present_numFailures;
}
