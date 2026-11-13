/*
 * Input recording and replay by frame, and scripted input (PORT-PLAN.md P3.10). See host_record.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <Windows.h> // VK_*

#include "host_clock.h"
#include "host_keys.h"
#include "host_record.h"
#include "stdDisplay_common.h" // J3DDISPLAY_HEADLESS_FRAME_MSEC

#define RECORD_MAGIC   "J3DREC\x01\n"
#define RECORD_MAXSIZE 4096 // the largest INPUT payload accepted

typedef enum RecordTag
{
    RECORD_CLOCK      = 'C',
    RECORD_PUMP       = 'P',
    RECORD_MSG        = 'M',
    RECORD_INPUT      = 'I',
    RECORD_INPUT_SAME = 'i', // the same snapshot as the previous INPUT
} RecordTag;

static FILE*    record_pOut      = NULL; // recording
static FILE*    record_pIn       = NULL; // replay
static int      record_bFailed   = 0;
static int      record_bFinished = 0;
static uint32_t record_frame     = 0;

static uint8_t  record_aLastInput[RECORD_MAXSIZE];
static size_t   record_lastInputSize = 0;

static int      record_peekTag = -1; // replay: the tag read ahead, or -1

static J3DScriptEvent* record_aScript       = NULL;
static size_t          record_numScript     = 0;
static size_t          record_aScriptPos[2] = { 0, 0 };
static int             record_bScriptPad    = 0;

/* ---- Script ------------------------------------------------------------------------------------------------------- */

static int record_ParseUpDown(const char* pWord, int32_t* pValue)
{
    if ( strcasecmp(pWord, "down") == 0 )
    {
        *pValue = 1;
        return 1;
    }
    if ( strcasecmp(pWord, "up") == 0 )
    {
        *pValue = 0;
        return 1;
    }
    return 0;
}

static int32_t record_ParseVirtualKey(const char* pName)
{
    if ( (pName[0] == 'F' || pName[0] == 'f') && pName[1] >= '1' && pName[1] <= '9' )
    {
        const int n = atoi(pName + 1);
        if ( n >= 1 && n <= 12 )
        {
            return VK_F1 + n - 1;
        }
    }

    static const struct { const char* pName; int32_t vk; } aKeys[] = {
        { "ESCAPE", VK_ESCAPE }, { "RETURN", VK_RETURN }, { "SPACE", VK_SPACE }, { "TAB", VK_TAB },
        { "BACK", VK_BACK },     { "UP", VK_UP },         { "DOWN", VK_DOWN },   { "LEFT", VK_LEFT },
        { "RIGHT", VK_RIGHT },
    };
    for ( size_t i = 0; i < sizeof(aKeys) / sizeof(aKeys[0]); i++ )
    {
        if ( strcasecmp(aKeys[i].pName, pName) == 0 )
        {
            return aKeys[i].vk;
        }
    }

    if ( pName[0] && !pName[1] && ((pName[0] >= 'A' && pName[0] <= 'Z') || (pName[0] >= '0' && pName[0] <= '9')) )
    {
        return pName[0];
    }
    return 0;
}

static int record_ParseScriptLine(char* pLine, J3DScriptEvent* pEvent)
{
    char* pSave = NULL;
    char* apWords[6];
    size_t numWords = 0;
    for ( char* pWord = strtok_r(pLine, " \t\r\n", &pSave); pWord && numWords < 6; pWord = strtok_r(NULL, " \t\r\n", &pSave) )
    {
        apWords[numWords++] = pWord;
    }

    if ( numWords < 3 )
    {
        return 0;
    }

    memset(pEvent, 0, sizeof(*pEvent));
    pEvent->frame = (uint32_t)strtoul(apWords[0], NULL, 10);
    const char* pType = apWords[1];

    if ( strcasecmp(pType, "key") == 0 && numWords == 4 )
    {
        pEvent->type = J3DSCRIPT_KEY;
        pEvent->a    = J3DHost_DikFromName(apWords[2]);
        return pEvent->a != 0 && record_ParseUpDown(apWords[3], &pEvent->b);
    }
    if ( strcasecmp(pType, "mouse") == 0 && numWords >= 4 )
    {
        pEvent->type = J3DSCRIPT_MOUSE;
        pEvent->a    = atoi(apWords[2]);
        pEvent->b    = atoi(apWords[3]);
        pEvent->c    = numWords >= 5 ? atoi(apWords[4]) : 0;
        return 1;
    }
    if ( (strcasecmp(pType, "button") == 0 || strcasecmp(pType, "pad") == 0) && numWords == 4 )
    {
        pEvent->type = strcasecmp(pType, "button") == 0 ? J3DSCRIPT_BUTTON : J3DSCRIPT_PAD;
        pEvent->a    = atoi(apWords[2]);
        const int32_t maxButton = pEvent->type == J3DSCRIPT_BUTTON ? 4 : 32;
        return pEvent->a >= 1 && pEvent->a <= maxButton && record_ParseUpDown(apWords[3], &pEvent->b);
    }
    if ( strcasecmp(pType, "axis") == 0 && numWords == 4 )
    {
        static const char* const apAxes[] = { "x", "y", "z", "rx", "ry", "rz" };
        pEvent->type = J3DSCRIPT_AXIS;
        pEvent->a    = -1;
        for ( int32_t i = 0; i < 6; i++ )
        {
            if ( strcasecmp(apWords[2], apAxes[i]) == 0 )
            {
                pEvent->a = i;
            }
        }
        pEvent->b = atoi(apWords[3]);
        return pEvent->a >= 0 && pEvent->b >= -32768 && pEvent->b <= 32767;
    }
    if ( strcasecmp(pType, "hat") == 0 && numWords == 3 )
    {
        static const char* const apDirs[] = { "center", "up", "right", "down", "left" };
        pEvent->type = J3DSCRIPT_HAT;
        pEvent->a    = -1;
        for ( int32_t i = 0; i < 5; i++ )
        {
            if ( strcasecmp(apWords[2], apDirs[i]) == 0 )
            {
                pEvent->a = i;
            }
        }
        return pEvent->a >= 0;
    }
    if ( strcasecmp(pType, "vkey") == 0 && numWords == 3 )
    {
        pEvent->type = J3DSCRIPT_VKEY;
        pEvent->a    = record_ParseVirtualKey(apWords[2]);
        return pEvent->a != 0;
    }

    return 0;
}

static int record_LoadScript(const char* pPath)
{
    FILE* pFile = fopen(pPath, "r");
    if ( !pFile )
    {
        fprintf(stderr, "input script: cannot open %s\n", pPath);
        return 1;
    }

    size_t capacity = 0;
    char   aLine[512];
    int    lineNum = 0;
    while ( fgets(aLine, sizeof(aLine), pFile) )
    {
        ++lineNum;
        char* pComment = strchr(aLine, '#');
        if ( pComment )
        {
            *pComment = '\0';
        }

        const char* p = aLine;
        while ( *p == ' ' || *p == '\t' )
        {
            ++p;
        }
        if ( !*p || *p == '\n' || *p == '\r' )
        {
            continue;
        }

        J3DScriptEvent event;
        if ( !record_ParseScriptLine(aLine, &event) )
        {
            fprintf(stderr, "input script: %s:%d: not understood\n", pPath, lineNum);
            fclose(pFile);
            return 1;
        }

        if ( record_numScript && event.frame < record_aScript[record_numScript - 1].frame )
        {
            fprintf(stderr, "input script: %s:%d: frames must not go backwards\n", pPath, lineNum);
            fclose(pFile);
            return 1;
        }

        if ( record_numScript == capacity )
        {
            capacity = capacity ? capacity * 2 : 64;
            J3DScriptEvent* pNew = realloc(record_aScript, capacity * sizeof(*pNew));
            if ( !pNew )
            {
                fclose(pFile);
                return 1;
            }
            record_aScript = pNew;
        }

        if ( event.type == J3DSCRIPT_PAD || event.type == J3DSCRIPT_AXIS || event.type == J3DSCRIPT_HAT )
        {
            record_bScriptPad = 1;
        }
        record_aScript[record_numScript++] = event;
    }

    fclose(pFile);
    return 0;
}

int J3DRecord_TakeScriptEvent(int bMessages, J3DScriptEvent* pEvent)
{
    size_t* pPos = &record_aScriptPos[bMessages ? 1 : 0];
    while ( *pPos < record_numScript && record_aScript[*pPos].frame <= record_frame )
    {
        const J3DScriptEvent* pCur = &record_aScript[(*pPos)++];
        if ( (pCur->type == J3DSCRIPT_VKEY) == (bMessages != 0) )
        {
            *pEvent = *pCur;
            return 1;
        }
    }
    return 0;
}

int J3DRecord_ScriptHasPad(void)
{
    return record_bScriptPad;
}

int J3DRecord_HasScript(void)
{
    return record_numScript != 0;
}

/* ---- Recording ---------------------------------------------------------------------------------------------------- */

static void record_Write(const void* pData, size_t size)
{
    if ( record_pOut && fwrite(pData, 1, size, record_pOut) != size )
    {
        fprintf(stderr, "record: write failed; recording stopped\n");
        fclose(record_pOut);
        record_pOut = NULL;
    }
}

static void record_WriteTag(RecordTag tag)
{
    const uint8_t b = (uint8_t)tag;
    record_Write(&b, 1);
}

/* ---- Replay ------------------------------------------------------------------------------------------------------- */

static void record_StopReplay(const char* pReason, int bFailed)
{
    if ( !record_pIn )
    {
        return;
    }

    if ( bFailed )
    {
        fprintf(stderr, "replay: %s at frame %u; replay stopped\n", pReason, record_frame);
        record_bFailed = 1;
    }
    else
    {
        fprintf(stderr, "replay: finished at frame %u\n", record_frame);
    }

    fclose(record_pIn);
    record_pIn       = NULL;
    record_bFinished = 1;
}

static int record_Read(void* pData, size_t size)
{
    if ( !record_pIn || fread(pData, 1, size, record_pIn) != size )
    {
        record_StopReplay("the recording ends in the middle of a record", 1);
        return 0;
    }
    return 1;
}

// The next record's tag, without taking it; -1 at the end of the recording.
static int record_PeekTag(void)
{
    if ( record_peekTag < 0 && record_pIn )
    {
        const int c = fgetc(record_pIn);
        record_peekTag = c == EOF ? -1 : c;
    }
    return record_peekTag;
}

// Takes the next record's tag, which must be one of the two given. Returns the tag, or -1 (replay stopped).
static int record_ExpectTag(RecordTag tag, RecordTag alt)
{
    if ( !record_pIn )
    {
        return -1;
    }

    const int next = record_PeekTag();
    if ( next < 0 )
    {
        record_StopReplay(NULL, 0);
        return -1;
    }

    record_peekTag = -1;
    if ( next != (int)tag && next != (int)alt )
    {
        char aReason[96];
        snprintf(aReason, sizeof(aReason), "out of step (the game asked for '%c', the recording has '%c')", (char)tag,
                 (char)next);
        record_StopReplay(aReason, 1);
        return -1;
    }
    return next;
}

/* ---- Interface ---------------------------------------------------------------------------------------------------- */

int J3DRecord_Startup(void)
{
    const char* pRecord = getenv("J3D_RECORD");
    const char* pReplay = getenv("J3D_REPLAY");
    const char* pScript = getenv("J3D_INPUT_SCRIPT");

    if ( pRecord && *pRecord && pReplay && *pReplay )
    {
        fprintf(stderr, "record: --record and --replay cannot be combined\n");
        return 1;
    }

    if ( pScript && *pScript && record_LoadScript(pScript) )
    {
        return 1;
    }

    if ( pRecord && *pRecord )
    {
        record_pOut = fopen(pRecord, "wb");
        if ( !record_pOut )
        {
            fprintf(stderr, "record: cannot create %s\n", pRecord);
            return 1;
        }
        record_Write(RECORD_MAGIC, sizeof(RECORD_MAGIC) - 1);

        // A headless recording (a test) keeps the headless clock's fixed frame time; a played one follows the host's.
        const char* pHeadless = getenv("J3D_HEADLESS");
        if ( !(pHeadless && *pHeadless && *pHeadless != '0') )
        {
            J3DHost_UseFrameClock();
        }
        else
        {
            J3DHost_UseVirtualClock();
        }

        // The clock the run starts with.
        const uint64_t now = J3DHost_GetClock();
        record_WriteTag(RECORD_CLOCK);
        record_Write(&now, sizeof(now));
    }
    else if ( pReplay && *pReplay )
    {
        char aMagic[sizeof(RECORD_MAGIC) - 1];
        record_pIn = fopen(pReplay, "rb");
        if ( !record_pIn )
        {
            fprintf(stderr, "replay: cannot open %s\n", pReplay);
            return 1;
        }
        if ( fread(aMagic, 1, sizeof(aMagic), record_pIn) != sizeof(aMagic) || memcmp(aMagic, RECORD_MAGIC, sizeof(aMagic)) != 0 )
        {
            fprintf(stderr, "replay: %s is not a recording of this build's format\n", pReplay);
            fclose(record_pIn);
            record_pIn = NULL;
            return 1;
        }

        J3DHost_UseVirtualClock();
        uint64_t start;
        if ( record_ExpectTag(RECORD_CLOCK, RECORD_CLOCK) < 0 || !record_Read(&start, sizeof(start)) )
        {
            return 1;
        }
        J3DHost_SetClock(start);
    }

    return 0;
}

void J3DRecord_Shutdown(void)
{
    if ( record_pOut )
    {
        fclose(record_pOut);
        record_pOut = NULL;
    }
    if ( record_pIn )
    {
        fclose(record_pIn);
        record_pIn = NULL;
    }
    free(record_aScript);
    record_aScript   = NULL;
    record_numScript = 0;
}

int J3DRecord_IsRecording(void)
{
    return record_pOut != NULL;
}

int J3DRecord_IsReplaying(void)
{
    return record_pIn != NULL;
}

int J3DRecord_ShouldQuit(void)
{
    return record_bFinished;
}

int J3DRecord_Failed(void)
{
    return record_bFailed;
}

uint32_t J3DRecord_GetFrame(void)
{
    return record_frame;
}

void J3DRecord_Present(int bHeadless)
{
    ++record_frame;

    if ( record_pIn )
    {
        uint64_t now;
        if ( record_ExpectTag(RECORD_CLOCK, RECORD_CLOCK) >= 0 && record_Read(&now, sizeof(now)) )
        {
            J3DHost_SetClock(now);
        }
        return;
    }

    if ( bHeadless )
    {
        J3DHost_AdvanceClock(J3DDISPLAY_HEADLESS_FRAME_MSEC);
    }
    else
    {
        J3DHost_SyncFrameClock();
    }

    if ( record_pOut )
    {
        const uint64_t now = J3DHost_GetClock();
        record_WriteTag(RECORD_CLOCK);
        record_Write(&now, sizeof(now));
    }
}

int J3DRecord_Input(void* pState, size_t size)
{
    if ( size > RECORD_MAXSIZE )
    {
        return 0;
    }

    if ( record_pOut )
    {
        if ( size == record_lastInputSize && memcmp(pState, record_aLastInput, size) == 0 )
        {
            record_WriteTag(RECORD_INPUT_SAME);
        }
        else
        {
            const uint32_t size32 = (uint32_t)size;
            record_WriteTag(RECORD_INPUT);
            record_Write(&size32, sizeof(size32));
            record_Write(pState, size);
            memcpy(record_aLastInput, pState, size);
            record_lastInputSize = size;
        }
        return 0;
    }

    if ( !record_pIn )
    {
        return 0;
    }

    const int tag = record_ExpectTag(RECORD_INPUT, RECORD_INPUT_SAME);
    if ( tag == RECORD_INPUT )
    {
        uint32_t size32;
        if ( !record_Read(&size32, sizeof(size32)) )
        {
            return 0;
        }
        if ( size32 != size )
        {
            record_StopReplay("an input snapshot of another size (another build's recording)", 1);
            return 0;
        }
        if ( !record_Read(record_aLastInput, size) )
        {
            return 0;
        }
        record_lastInputSize = size;
    }
    else if ( tag != RECORD_INPUT_SAME || record_lastInputSize != size )
    {
        if ( tag == RECORD_INPUT_SAME )
        {
            record_StopReplay("a repeated input snapshot with none before it", 1);
        }
        return 0;
    }

    memcpy(pState, record_aLastInput, size);
    return 1;
}

void J3DRecord_Pump(void)
{
    if ( record_pOut )
    {
        record_WriteTag(RECORD_PUMP);
    }
    else if ( record_pIn )
    {
        record_ExpectTag(RECORD_PUMP, RECORD_PUMP);
    }
}

void J3DRecord_Message(uint32_t msg, uint64_t wParam, int64_t lParam)
{
    if ( !record_pOut )
    {
        return;
    }
    record_WriteTag(RECORD_MSG);
    record_Write(&msg, sizeof(msg));
    record_Write(&wParam, sizeof(wParam));
    record_Write(&lParam, sizeof(lParam));
}

int J3DRecord_NextMessage(uint32_t* pMsg, uint64_t* pWParam, int64_t* pLParam)
{
    if ( !record_pIn || record_PeekTag() != RECORD_MSG )
    {
        return 0;
    }

    record_peekTag = -1;
    return record_Read(pMsg, sizeof(*pMsg)) && record_Read(pWParam, sizeof(*pWParam)) && record_Read(pLParam, sizeof(*pLParam));
}
