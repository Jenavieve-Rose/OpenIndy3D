/*
 * wkernel without a window (PORT-PLAN.md Phase 0). wkernel_Run keeps the original control flow (startup, then the
 * JonesMain_Process loop until it returns 1 or -1) minus window creation. The event pump has no OS queue to read; it
 * only plays out the WM_CLOSE -> JonesMain_Shutdown -> WM_DESTROY -> quit sequence that wkernel_MainWndProc gave a
 * posted WM_CLOSE, so the game's own exit path (JonesMain_CloseWindow) still ends the loop.
 *
 * Replaced by the SDL3 event loop in Phase 2.
 */
#include <wkernel/wkernel.h>
#include <Jones3D/Main/JonesMain.h>
#include <std/Win95/stdWin95.h>

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

    while ( 1 )
    {
        switch ( result )
        {
            case 0:
                result = JonesMain_Process();
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
