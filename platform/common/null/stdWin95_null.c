/*
 * stdWin95 keeps the process's window, instance and GUID for the rest of the engine. The getters and setters are
 * platform-neutral, so this is the original behaviour; the GDI helpers in the original file are not used.
 */
#include <std/Win95/stdWin95.h>

static HWND      stdWin95_hwnd      = NULL;
static HINSTANCE stdWin95_hInstance = NULL;
static GUID      stdWin95_guid;

void J3DAPI stdWin95_SetWindow(HWND hwnd)
{
    stdWin95_hwnd = hwnd;
}

HWND J3DAPI stdWin95_GetWindow(void)
{
    return stdWin95_hwnd;
}

void J3DAPI stdWin95_SetInstance(HINSTANCE hinstance)
{
    stdWin95_hInstance = hinstance;
}

HINSTANCE J3DAPI stdWin95_GetInstance(void)
{
    return stdWin95_hInstance;
}

void J3DAPI stdWin95_SetGuid(const GUID* pGuid)
{
    if ( pGuid )
    {
        stdWin95_guid = *pGuid;
    }
}

const GUID* J3DAPI stdWin95_GetGuid(void)
{
    return &stdWin95_guid;
}
