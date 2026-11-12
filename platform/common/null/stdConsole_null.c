/*
 * stdConsole without a Win32 console: text goes to stdout. The attributes are ignored. This is the console the
 * "Debug Mode = console" log setting writes to (JonesMain_Startup), so it is real rather than silent.
 */
#include <stdio.h>
#include <std/Win95/stdConsole.h>

HANDLE stdConsole_hOutput = NULL;

int J3DAPI stdConsole_Startup(const char* pTitleText, int attributes, int bShowMinimized)
{
    (void)attributes; (void)bShowMinimized;
    printf("==== %s ====\n", pTitleText ? pTitleText : "console");
    return 1;
}

void J3DAPI stdConsole_Shutdown(void)
{
    fflush(stdout);
}

int J3DAPI stdConsole_SetAttributes(WORD wAttributes)
{
    (void)wAttributes;
    return 1;
}

int J3DAPI stdConsole_SetConsoleTextAttribute(WORD wAttributes)
{
    (void)wAttributes;
    return 1;
}

int J3DAPI stdConsole_InitOutputConsole(void)
{
    return 1;
}

int J3DAPI stdConsole_WriteConsole(const char* pText, WORD textAttribute)
{
    (void)textAttribute;
    fputs(pText, stdout);
    fflush(stdout);
    return 1;
}
