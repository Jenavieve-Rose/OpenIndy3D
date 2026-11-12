/*
 * Process entry point for the macOS build.
 *
 * The decomp's entry is WinMain (Jones3D/Main/JonesMain.c), which takes the single-instance mutex and hands off to
 * wkernel_Run. That call chain is kept: main() rebuilds the command line WinMain expects and calls it.
 *
 * Port options are taken out of argv before the rest is joined, so the engine's own parser never sees them:
 *   --data <dir>   game data root, the folder that holds RESOURCE/ (sets J3D_DATA; see wuRegistry_file.c)
 *   --headless     run with the in-memory display and no window (sets J3D_HEADLESS; see stdDisplay_headless.c)
 *   --frames <n>   quit after n passes of the game loop (sets J3D_FRAMES; see wkernel_null.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

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

        if ( pCmdLine[0] )
        {
            strcat(pCmdLine, " ");
        }
        strcat(pCmdLine, argv[i]);
    }

    int result = WinMain(NULL, NULL, pCmdLine, SW_SHOWNORMAL);
    free(pCmdLine);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
