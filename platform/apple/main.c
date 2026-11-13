/*
 * Process entry point for the macOS build. It is compiled into both game binaries: jones3d, with the backends J3D_PLATFORM
 * selects, and in an apple build also jones3d-null, with the null backends, which the boot test runs (CMakeLists.txt).
 *
 * The decomp's entry is WinMain (Jones3D/Main/JonesMain.c), which takes the single-instance mutex and hands off to
 * wkernel_Run. That call chain is kept: main() rebuilds the command line WinMain expects and calls it.
 *
 * Port options are taken out of argv before the rest is joined, so the engine's own parser never sees them:
 *   --data <dir>   game data root, the folder that holds RESOURCE/ (sets J3D_DATA; see wuRegistry_file.c)
 *   --headless     run on the virtual clock with no window (apple build: SDL's dummy driver) (sets J3D_HEADLESS; see
 *                  stdDisplay_headless.c and stdDisplay_sdl.c)
 *   --frames <n>   quit after n passes of the game loop (sets J3D_FRAMES; see wkernel_null.c and wkernel_sdl.c)
 *   --screenshot-at <n>[,<n>...]  write the back buffer after those passes (sets J3D_SCREENSHOT_AT; host_screenshot.h)
 *   --screenshot-file <path>      the screenshot file name, "{frame}" replaced (sets J3D_SCREENSHOT_FILE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

#include <platform/common/host_dirs.h>
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

        if ( strcmp(argv[i], "--screenshot-at") == 0 || strcmp(argv[i], "--screenshot-file") == 0 )
        {
            if ( i + 1 >= argc )
            {
                fprintf(stderr, "%s: %s needs a value\n", argv[0], argv[i]);
                free(pCmdLine);
                return EXIT_FAILURE;
            }
            setenv(strcmp(argv[i], "--screenshot-at") == 0 ? "J3D_SCREENSHOT_AT" : "J3D_SCREENSHOT_FILE", argv[i + 1], 1);
            ++i;
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

    // Saves, screenshots and keysets go to a folder of the user's, never the game data (host_dirs.h).
    J3DHost_PrepareUserDirs();

    int result = WinMain(NULL, NULL, pCmdLine, SW_SHOWNORMAL);
    free(pCmdLine);

    // wkernel_Run returns -1 when startup fails, 1 when the game quits through its window (WM_QUIT; retail's result for
    // every normal exit) and 0 when a --frames run reaches its count. A --frames run that ends any other way, a level
    // that fails to load closing the game for example, has failed.
    if ( result < 0 || (getenv("J3D_FRAMES") && result != 0) )
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
