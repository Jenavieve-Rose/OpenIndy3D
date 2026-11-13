/*
 * Screenshots by frame number (PORT-PLAN.md P2.7); see host_screenshot.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <std/Win95/stdDisplay.h>

#include <platform/common/host_screenshot.h>

#define SCREENSHOT_MAXFRAMES 64

static int           screenshot_bParsed = 0;
static size_t        screenshot_numFrames = 0;
static unsigned long screenshot_aFrames[SCREENSHOT_MAXFRAMES];

static void screenshot_Parse(void)
{
    screenshot_bParsed = 1;

    const char* pList = getenv("J3D_SCREENSHOT_AT");
    while ( pList && *pList && screenshot_numFrames < SCREENSHOT_MAXFRAMES )
    {
        char* pEnd = NULL;
        unsigned long frame = strtoul(pList, &pEnd, 10);
        if ( pEnd == pList )
        {
            fprintf(stderr, "J3D_SCREENSHOT_AT: expected a frame number at '%s'\n", pList);
            return;
        }
        screenshot_aFrames[screenshot_numFrames++] = frame;
        pList = *pEnd == ',' ? pEnd + 1 : pEnd;
    }
}

// pPattern with every "{frame}" replaced by the frame number.
static void screenshot_MakePath(char* pPath, size_t size, const char* pPattern, unsigned long frame)
{
    char aFrame[24];
    snprintf(aFrame, sizeof(aFrame), "%lu", frame);

    size_t len = 0;
    while ( *pPattern && len + 1 < size )
    {
        if ( strncmp(pPattern, "{frame}", 7) == 0 )
        {
            len += (size_t)snprintf(pPath + len, size - len, "%s", aFrame);
            len = len < size ? len : size - 1;
            pPattern += 7;
            continue;
        }
        pPath[len++] = *pPattern++;
    }
    pPath[len] = '\0';
}

void J3DHost_AfterFrame(unsigned long numFrames)
{
    if ( !screenshot_bParsed )
    {
        screenshot_Parse();
    }

    for ( size_t i = 0; i < screenshot_numFrames; ++i )
    {
        if ( screenshot_aFrames[i] != numFrames )
        {
            continue;
        }

        if ( !stdDisplay_bModeSet )
        {
            printf("Screenshot at frame %lu: no display mode set, nothing written\n", numFrames);
            return;
        }

        const char* pPattern = getenv("J3D_SCREENSHOT_FILE");
        char aPath[1024];
        screenshot_MakePath(aPath, sizeof(aPath), pPattern && *pPattern ? pPattern : "jones3d-{frame}.bmp", numFrames);
        if ( stdDisplay_SaveScreen(aPath) )
        {
            printf("Screenshot at frame %lu: could not write %s\n", numFrames, aPath);
        }
        else
        {
            printf("Screenshot at frame %lu: %s\n", numFrames, aPath);
        }
        return;
    }
}
