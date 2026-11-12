/*
 * Process entry point for the macOS build.
 *
 * The decomp's entry is WinMain (Jones3D/Main/JonesMain.c), which takes the single-instance mutex and hands off to
 * wkernel_Run. That call chain is kept: main() rebuilds the command line WinMain expects and calls it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);

int main(int argc, char** argv)
{
    // Join argv[1..] with spaces, as the Windows loader hands WinMain everything after the program name.
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
        if ( i > 1 )
        {
            strcat(pCmdLine, " ");
        }
        strcat(pCmdLine, argv[i]);
    }

    int result = WinMain(NULL, NULL, pCmdLine, SW_SHOWNORMAL);
    free(pCmdLine);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
