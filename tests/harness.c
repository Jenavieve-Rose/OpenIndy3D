#include "harness.h"

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Main/JonesFile.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Play/jonesCog.h>

#include <rdroid/Main/rdroid.h>
#include <sith/Devices/sithSound.h>
#include <sith/Main/sithMain.h>
#include <sith/Main/sithString.h>
#include <std/General/std.h>
#include <std/General/stdEffect.h>
#include <std/General/stdPlatform.h>
#include <w32util/wuRegistry.h>

static tHostServices harness_hs;

// A crash prints a backtrace (backtrace_symbols_fd does not allocate, which is enough for a test).
static void harness_Crash(int sig)
{
    void* aFrames[48];
    const char* pMsg = sig == SIGBUS ? "SIGBUS\n" : "SIGSEGV\n";
    write(2, pMsg, strlen(pMsg));
    backtrace_symbols_fd(aFrames, backtrace(aFrames, 48), 2);
    _exit(128 + sig);
}

// Engine asserts print a backtrace and abort, so a failure points at the code, not only the assert text.
static void J3DAPI harness_Assert(const char* pErrorStr, const char* pFilename, int linenum)
{
    void* aFrames[48];
    fprintf(stderr, "ASSERT %s(%d): %s\n", pFilename, linenum, pErrorStr);
    backtrace_symbols_fd(aFrames, backtrace(aFrames, 48), 2);
    abort();
}

void harness_InstallHandlers(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGSEGV, harness_Crash);
    signal(SIGBUS, harness_Crash);
}

int harness_Startup(const char* pDataRoot)
{
    char aRoot[512];
    size_t len = strlen(pDataRoot);
    snprintf(aRoot, sizeof(aRoot), "%s%s", pDataRoot, len && pDataRoot[len - 1] == '/' ? "" : "/");
    setenv("J3D_DATA", aRoot, 1);

    // Keep the player's settings file out of it.
    if ( !getenv("J3D_SETTINGS") )
    {
        setenv("J3D_SETTINGS", "/dev/null", 1);
    }

    stdPlatform_InitServices(&harness_hs);
    harness_hs.pAssert = harness_Assert;
    stdStartup(&harness_hs);
    rdSetServices(&harness_hs);
    sithSetServices(&harness_hs);
    sithSound_Initialize(&harness_hs);
    JonesFile_Startup(&harness_hs);
    if ( wuRegistry_Startup(NULL, NULL) )
    {
        fprintf(stderr, "harness: settings startup failed\n");
        return 1;
    }

    JonesFile_Open(&harness_hs, aRoot, aRoot);
    if ( jonesString_Startup() || sithString_Startup() )
    {
        fprintf(stderr, "harness: string tables failed to load (is %s the folder holding RESOURCE/?)\n", aRoot);
        return 1;
    }

    stdEffect_Startup();
    rdStartup();
    if ( sithStartup() )
    {
        fprintf(stderr, "harness: sithStartup failed\n");
        return 1;
    }

    // Registers the game's COG verbs; without them the level scripts do not parse.
    if ( jonesCog_Startup() )
    {
        fprintf(stderr, "harness: jonesCog_Startup failed\n");
        return 1;
    }
    return 0;
}
