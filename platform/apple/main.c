/*
 * Process entry point for the macOS build. It is compiled into both game binaries: jones3d, with the backends J3D_PLATFORM
 * selects, and in an apple build also jones3d-null, with the null backends, which the boot test runs (CMakeLists.txt).
 *
 * The decomp's entry is WinMain (Jones3D/Main/JonesMain.c), which takes the single-instance mutex and hands off to
 * wkernel_Run. That call chain is kept: main() rebuilds the command line WinMain expects and calls it.
 *
 * Port options are taken out of argv before the rest is joined, so the engine's own parser never sees them:
 *   --data <dir>   game data root, the folder that holds RESOURCE/ (sets J3D_DATA; see wuRegistry_file.c)
 *   --level <n>    start a new game at level n, 1 to 17, without the launcher (P8.6): sets J3D_REG_Start_Mode=0 and
 *                  hands n to the engine, which reads a level number from its command line in that mode
 *   --windowed, --fullscreen   play in a window or full screen this time (sets J3D_REG_InWindow; P8.6)
 *   --help         lists these options
 *   --headless     run on the virtual clock with no window (apple build: SDL's dummy driver) (sets J3D_HEADLESS; see
 *                  stdDisplay_headless.c and stdDisplay_sdl.c)
 *   --frames <n>   quit after n passes of the game loop (sets J3D_FRAMES; see wkernel_null.c and wkernel_sdl.c)
 *   --screenshot-at <n>[,<n>...]  write the back buffer after those passes (sets J3D_SCREENSHOT_AT; host_screenshot.h)
 *   --screenshot-file <path>      the screenshot file name, "{frame}" replaced (sets J3D_SCREENSHOT_FILE)
 *   --record <file>       record the run's input and clock (sets J3D_RECORD; host_record.h)
 *   --replay <file>       play a recording back, then quit (sets J3D_REPLAY)
 *   --input-script <file> feed scripted input, for tests (sets J3D_INPUT_SCRIPT)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

#include <platform/common/host_crash.h>
#include <platform/common/host_dirs.h>
#include <platform/common/host_product.h>
#include <platform/common/host_record.h>
#if J3D_PLATFORM_APPLE
#include <platform/apple/host_apple.h>
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);

int main(int argc, char** argv)
{
    // Join the remaining argv[1..] with spaces, as the Windows loader hands WinMain everything after the program name.
    size_t len = 1;
    for ( int i = 1; i < argc; i++ )
    {
        len += strlen(argv[i]) + 1;
    }

    char* pCmdLine = calloc(len, 1);
    if ( !pCmdLine )
    {
        return EXIT_FAILURE;
    }

    for ( int i = 1; i < argc; i++ )
    {
        if ( strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 )
        {
            printf("usage: %s [options]\n"
                   "  --data <folder>   the game's folder, the one that holds Resource\n"
                   "  --level <n>       start a new game at level n (1 to 17), without the launcher\n"
                   "  --windowed        play in a window\n"
                   "  --fullscreen      play full screen\n"
                   "  --outfit <o>      Indy's outfit for this run: game (as the level chooses), jacket or nojacket\n"
                   "Holding Option as the app starts asks for the game's folder again.\n", argv[0]);
            free(pCmdLine);
            return EXIT_SUCCESS;
        }

        if ( strcmp(argv[i], "--level") == 0 )
        {
            char* pEnd = NULL;
            const long level = i + 1 < argc ? strtol(argv[i + 1], &pEnd, 10) : 0;
            if ( !pEnd || *pEnd || level < 1 || level > 17 )
            {
                fprintf(stderr, "%s: --level needs a level number from 1 to 17\n", argv[0]);
                free(pCmdLine);
                return EXIT_FAILURE;
            }
            setenv("J3D_REG_Start_Mode", "0", 1);
            ++i;
            // The level number is the engine's own argument; it goes on to WinMain below
        }

        if ( strcmp(argv[i], "--windowed") == 0 || strcmp(argv[i], "--fullscreen") == 0 )
        {
            setenv("J3D_REG_InWindow", strcmp(argv[i], "--windowed") == 0 ? "1" : "0", 1);
            continue;
        }

        // Indy's outfit for this run (the "Indy Outfit" setting, docs/BUILDING.md); the settings file is not changed
        if ( strcmp(argv[i], "--outfit") == 0 )
        {
            static const char* const aOutfits[] = { "game", "jacket", "nojacket" };
            const char* pValue = NULL;
            for ( int k = 0; i + 1 < argc && k < 3; k++ )
            {
                if ( strcmp(argv[i + 1], aOutfits[k]) == 0 || (argv[i + 1][0] == '0' + k && argv[i + 1][1] == 0) )
                {
                    pValue = k == 0 ? "0" : k == 1 ? "1" : "2";
                }
            }
            if ( !pValue )
            {
                fprintf(stderr, "%s: --outfit needs game, jacket or nojacket\n", argv[0]);
                free(pCmdLine);
                return EXIT_FAILURE;
            }
            setenv("J3D_REG_Indy_Outfit", pValue, 1);
            ++i;
            continue;
        }

        if ( strcmp(argv[i], "--data") == 0 )
        {
            if ( i + 1 >= argc )
            {
                fprintf(stderr, "%s: --data needs a directory\n", argv[0]);
                free(pCmdLine);
                return EXIT_FAILURE;
            }
            setenv("J3D_DATA", argv[++i], 1);
            continue;
        }

        if ( strcmp(argv[i], "--headless") == 0 )
        {
            setenv("J3D_HEADLESS", "1", 1);
            continue;
        }

        if ( strcmp(argv[i], "--frames") == 0 )
        {
            if ( i + 1 >= argc )
            {
                fprintf(stderr, "%s: --frames needs a count\n", argv[0]);
                free(pCmdLine);
                return EXIT_FAILURE;
            }
            setenv("J3D_FRAMES", argv[++i], 1);
            continue;
        }

        static const struct { const char* pOption; const char* pVariable; } aValueOptions[] = {
            { "--screenshot-at", "J3D_SCREENSHOT_AT" }, { "--screenshot-file", "J3D_SCREENSHOT_FILE" },
            { "--record", "J3D_RECORD" },               { "--replay", "J3D_REPLAY" },
            { "--input-script", "J3D_INPUT_SCRIPT" },
        };
        int bTaken = 0;
        for ( size_t opt = 0; opt < sizeof(aValueOptions) / sizeof(aValueOptions[0]); opt++ )
        {
            if ( strcmp(argv[i], aValueOptions[opt].pOption) == 0 )
            {
                if ( i + 1 >= argc )
                {
                    fprintf(stderr, "%s: %s needs a value\n", argv[0], argv[i]);
                    free(pCmdLine);
                    return EXIT_FAILURE;
                }
                setenv(aValueOptions[opt].pVariable, argv[++i], 1);
                bTaken = 1;
                break;
            }
        }
        if ( bTaken )
        {
            continue;
        }

        if ( pCmdLine[0] )
        {
            strcat(pCmdLine, " ");
        }
        strcat(pCmdLine, argv[i]);
    }

#if J3D_PLATFORM_APPLE
    J3DApple_LogHostInfo();
#endif

    // Saves, screenshots and keysets go to a folder of the user's, never the game data (host_dirs.h). The game data is
    // found, or asked for in a run someone is at (P8.4); holding Option as the app starts asks again.
    J3DHostAskData pfAskData = NULL;
    int bChooseData = 0;
#if J3D_PLATFORM_APPLE
    if ( !getenv("J3D_HEADLESS") && !getenv("J3D_FRAMES") && !getenv("J3D_REPLAY") && !getenv("J3D_INPUT_SCRIPT") )
    {
        pfAskData = J3DApple_AskForData;
        bChooseData = J3DApple_IsOptionHeld();
    }
#endif
    if ( J3DHost_PrepareUserDirs(pfAskData, bChooseData) )
    {
        free(pCmdLine);
        return EXIT_SUCCESS; // the player quit rather than choose the game's folder
    }

    // A crash writes the engine's last output to the log folder (P8.11); the next run says where it is
    J3DHost_InstallCrashHandler();
    char aCrashLog[1100];
    if ( J3DHost_TakeLastCrash(aCrashLog, sizeof(aCrashLog)) )
    {
        fprintf(stderr, J3D_PRODUCT_NAME ": the last run crashed; its log is %s\n", aCrashLog);
#if J3D_PLATFORM_APPLE
        if ( pfAskData )
        {
            J3DApple_ShowLastCrash(aCrashLog);
        }
#endif
    }

    // Input recording, replay and scripts (PORT-PLAN.md P3.10); this also picks the clock for them.
    if ( J3DRecord_Startup() )
    {
        free(pCmdLine);
        return EXIT_FAILURE;
    }

    int result = WinMain(NULL, NULL, pCmdLine, SW_SHOWNORMAL);
    free(pCmdLine);

    const int bReplayFailed = J3DRecord_Failed();
    J3DRecord_Shutdown();
    if ( bReplayFailed )
    {
        return EXIT_FAILURE;
    }

    // wkernel_Run returns -1 when startup fails, 1 when the game quits through its window (WM_QUIT; retail's result for
    // every normal exit) and 0 when a --frames run reaches its count or a --replay run plays to its end. A --frames run
    // that ends any other way, a level that fails to load closing the game for example, has failed.
    if ( result < 0 || (getenv("J3D_FRAMES") && result != 0) )
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
