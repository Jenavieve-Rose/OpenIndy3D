/*
 * wkernel without a window (PORT-PLAN.md Phase 0). wkernel_Run keeps the original control flow (startup, then the
 * JonesMain_Process loop until it returns 1 or -1) minus window creation. The event pump has no OS queue to read; it
 * only plays out the WM_CLOSE -> JonesMain_Shutdown -> WM_DESTROY -> quit sequence that wkernel_MainWndProc gave a
 * posted WM_CLOSE, so the game's own exit path (JonesMain_CloseWindow) still ends the loop.
 *
 * J3D_FRAMES=<n> (jones3d --frames <n>) ends the run after n passes of that loop, through the same shutdown a closed
 * window gets. Once a level is open a pass is one game frame, so this is how a headless run finishes (PORT-PLAN.md P1.18).
 *
 * Replaced by the SDL3 event loop in Phase 2.
 */
#include <stdlib.h>

#include <wkernel/wkernel.h>
#include <Jones3D/Main/JonesMain.h>
#include <std/Win95/stdWin95.h>

#include <platform/common/host_screenshot.h>

#include "null_platform.h"

static const GUID wkernel_guid = { 0x82CE4DA0, 0x9CBF, 0x11D1, { 0x90, 0x85, 0x00, 0x60, 0x97, 0x76, 0x0EA, 0x02 } };
static WKERNELWNDPROC wkernel_pfWndProc = NULL;

static int wkernel_PumpNull(void)
{
    if ( null_TakeCloseRequest() )
    {
        // wkernel_MainWndProc's WM_CLOSE case, then DefWindowProc -> DestroyWindow -> WM_DESTROY -> PostQuitMessage.
        JonesMain_Shutdown();
        null_RequestQuit();
    }
    return null_IsQuitRequested() ? 1 : 0;
}

int J3DAPI wkernel_Run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd, LPCSTR lpWindowName)
{
    int result = 0;
    (void)hPrevInstance; (void)nShowCmd; (void)lpWindowName;

    stdWin95_SetWindow(NULL);
    stdWin95_SetInstance(hInstance);
    stdWin95_SetGuid(&wkernel_guid);

    if ( JonesMain_Startup(lpCmdLine) )
    {
        JonesMain_Shutdown();
        return -1;
    }

    const char* pFrames   = getenv("J3D_FRAMES");
    unsigned long maxFrames = pFrames ? strtoul(pFrames, NULL, 10) : 0; // 0: no limit
    unsigned long numFrames = 0;

    while ( 1 )
    {
        switch ( result )
        {
            case 0:
                if ( maxFrames && numFrames == maxFrames )
                {
                    // wkernel_MainWndProc's WM_CLOSE: shut the game down, then leave the loop with WM_QUIT's 0.
                    JonesMain_Shutdown();
                    return 0;
                }

                result = JonesMain_Process();
                ++numFrames;
                J3DHost_AfterFrame(numFrames); // --screenshot-at
                break;

            case -1:
            case 1:
                return result;
        }
    }
}

void J3DAPI wkernel_SetWindowStyle(LONG dwNewLong)
{
    (void)dwNewLong;
}

BOOL J3DAPI wkernel_SetWindowSize(int width, int height)
{
    (void)width; (void)height;
    return FALSE;
}

void J3DAPI wkernel_SetWindowProc(WKERNELWNDPROC pfProc)
{
    wkernel_pfWndProc = pfProc;
}

int J3DAPI wkernel_PeekProcessEvents(void)
{
    return wkernel_PumpNull();
}

int J3DAPI wkernel_ProcessEvents(void)
{
    return wkernel_PumpNull();
}
