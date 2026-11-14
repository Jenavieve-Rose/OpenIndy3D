/*
 * insane_dsnd for the port (PORT-PLAN.md P5.9): INSANE's `SNDDRIVER=insane_dsnd` service, in place of retail's
 * DirectSound driver Libs/smush/insane/Sound/Dsnd.c (excluded from the build). Shared by every build.
 *
 * Retail kept a looping DirectSound buffer of 3 blocks of 0x600 stereo 16-bit frames at 22,050 Hz. A one-shot timer
 * callback, re-armed every ~48 ms, mixed the next block whenever the play cursor had left it: it cleared the mixer
 * (mixer_Begin), had the sound engine feed every playing track into it (the engine's fill callback, with mixer_Feed),
 * clipped the result to 16 bits (mixer_Mix) and copied it into the buffer.
 *
 * Here the same three calls fill blocks of the stream sound_stream.h describes, which SoundDriver_mix.c plays on the
 * phase 4 output device. The engine counts what it mixes and syncs INSANE's frame pacer to it (Engine.c's
 * sound_Advance, Timer's Sync), so how far mixing runs ahead of what is heard is how far the picture leads the sound.
 * Retail ran two to three 70 ms blocks ahead; this driver mixes 0x200-frame (23 ms) blocks every timer tick (1/60 s)
 * and keeps three of them (70 ms) ahead of the play cursor, enough to ride out a late tick.
 *
 * Kept from retail: registration, the configuration tags, the subscriptions, the volume table (DirectSound's
 * hundredths of a decibel, here turned into the stream's gain), the 22,050 Hz output whatever rate is asked for.
 * The DirectSound object SmushPlay hands over (tag 5) is not used: there is one output, the sound driver's.
 */
#include <smush/insane/lec.h>
#include <smush/insane/Timer/Timer.h>
#include <smush/insane/Sound/Dsnd.h>
#include <smush/insane/Sound/Mixer.h>

#include <platform/common/sound_stream.h>

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define DSND_RATE   22050
#define DSND_BLOCK  0x200           // frames per mix
#define DSND_AHEAD  (3 * DSND_BLOCK) // frames kept mixed ahead of the play cursor
#define DSND_MAXFILL 4              // blocks one callback may mix (catching up after a late tick)

extern int sound_Null(void);

int dsnd_Register(LecShare* pShare, void* pParam);
int dsnd_Configure(int tag, ...);
static void dsnd_Unregister(void);
static int dsnd_Startup(void);
static void dsnd_Shutdown(void);
static int dsnd_Start(int rate, int a2, void (*pfnFill)(int, SoundFeed));
static void dsnd_Stop(void);
static void dsnd_SetVolume(int volume);
static int dsnd_GetVolume(void);
static int dsnd_GetRate(void);
static void dsnd_Callback(int index, intptr_t param);

static LecModule dsnd_module = { dsnd_Register, dsnd_Unregister, dsnd_Startup, dsnd_Shutdown };
static SndDriver dsnd_interface = { dsnd_Start, dsnd_Stop, dsnd_SetVolume, dsnd_GetVolume, (int (*)(int))sound_Null, dsnd_GetRate };

static LecShare* dsnd_pShare = NULL;
static void* dsnd_pParam = NULL;
static TimerInterface* dsnd_pTimer = NULL;
static struct MemoryDebugInterface* dsnd_pMixerTable = NULL;
static void (*dsnd_pfnFill)(int, SoundFeed) = NULL;
static int dsnd_hTimer = 0;
static int dsnd_volume = 127;
static int dsnd_volTable[128];
static int dsnd_tablesReady = 0;
static int dsnd_cfg1 = 0;
static int dsnd_cfg2 = 0;
static int dsnd_cfg3 = 0;
static int dsnd_cfg4 = 0;
static int16_t dsnd_scratch[DSND_BLOCK * 2];

int dsnd_Register(LecShare* pShare, void* pParam)
{
    dsnd_pShare = pShare;
    dsnd_pParam = pParam;
    pShare->Publish("LECMODULE=insane_dsnd", &dsnd_module);
    dsnd_pShare->Publish("SNDDRIVER=insane_dsnd", &dsnd_interface);
    dsnd_cfg1 = -1;
    dsnd_cfg3 = -1;
    dsnd_cfg2 = -1;
    dsnd_cfg4 = -1;
    return 0;
}

int dsnd_Configure(int tag, ...)
{
    va_list args;

    va_start(args, tag);
    while (tag) {
        switch (tag) {
            case 4:
                dsnd_cfg4 = va_arg(args, int);
                break;
            case 2:
                dsnd_cfg2 = va_arg(args, int);
                break;
            case 3:
                dsnd_cfg3 = va_arg(args, int);
                break;
            case 1:
                dsnd_cfg1 = va_arg(args, int);
                break;
            case 5:
                (void)va_arg(args, void*); // retail: the game's DirectSound object
                break;
            default:
                va_end(args);
                return -1;
        }
        tag = va_arg(args, int);
    }
    va_end(args);
    return 0;
}

static void dsnd_Unregister(void)
{
    dsnd_pShare->Unpublish("SNDDRIVER=insane_dsnd");
    dsnd_pShare->Unpublish("LECMODULE=insane_dsnd");
}

static int dsnd_Startup(void)
{
    dsnd_pTimer = dsnd_pShare->Subscribe("TIMER=clock", dsnd_pParam);
    if (dsnd_pTimer == NULL) {
        dsnd_pShare->ReportError("DirectSoundDriver Module: Cannot allocate timer services.");
        return -1;
    }
    dsnd_pMixerTable = dsnd_pShare->Subscribe("MEMORY.mixertable", dsnd_pParam);
    if (dsnd_pMixerTable == NULL) {
        dsnd_pShare->ReportError("DirectSoundDriver Module: Cannot allocate timer services."); // retail's text
        return -1;
    }
    return 0;
}

static void dsnd_Shutdown(void)
{
    dsnd_pShare->Unsubscribe(dsnd_pTimer);
    dsnd_pShare->Unsubscribe(dsnd_pMixerTable);
}

static int dsnd_Start(int rate, int a2, void (*pfnFill)(int, SoundFeed))
{
    (void)a2;
    (void)rate; // retail raised it to at least 22,050 and timed its callback by it; the output is 22,050 Hz either way
    if (mixer_Init(dsnd_pMixerTable, 4, 0x400, 0, 1, 1)) {
        return -1;
    }
    dsnd_pfnFill = pfnFill;
    J3DSoundStream_Open(DSND_RATE);
    dsnd_SetVolume(dsnd_volume);
    dsnd_hTimer = dsnd_pTimer->Alloc(dsnd_Callback, dsnd_pTimer->GetTickLen(), 2, 0);
    return 0;
}

static void dsnd_Stop(void)
{
    if (dsnd_hTimer) {
        dsnd_pTimer->Free(dsnd_hTimer); // once this returns the callback is not running (insane_timer.c)
    }
    dsnd_hTimer = 0;
    J3DSoundStream_Close();
    mixer_Done(dsnd_pMixerTable);
}

static void dsnd_SetVolume(int volume)
{
    if (!dsnd_tablesReady) { // retail's table, in DirectSound's hundredths of a decibel
        int* pEntry;
        int v;

        dsnd_volTable[0] = -10000;
        dsnd_volTable[1] = -7000;
        dsnd_volTable[2] = -6000;
        dsnd_volTable[3] = -5200;
        dsnd_volTable[4] = -5000;
        dsnd_volTable[5] = -4500;
        dsnd_volTable[6] = -4200;
        dsnd_volTable[7] = -4100;
        dsnd_volTable[8] = -4000;
        pEntry = &dsnd_volTable[8];
        for (v = 0; v < 48000; v += 2000) {
            *pEntry++ = v / 23 - 4000;
        }
        pEntry = &dsnd_volTable[32];
        for (v = 0; v < 32000; v += 1000) {
            *pEntry++ = v / 31 - 2000;
        }
        pEntry = &dsnd_volTable[64];
        for (v = 0; v < 64000; v += 1000) {
            *pEntry++ = v / 63 - 1000;
        }
        dsnd_tablesReady = 1;
    }
    dsnd_volume = volume & 0x7F;
    J3DSoundStream_SetGain(powf(10.0f, (float)dsnd_volTable[dsnd_volume] / 2000.0f));
}

static int dsnd_GetVolume(void)
{
    return dsnd_volume;
}

static int dsnd_GetRate(void)
{
    return DSND_RATE;
}

// On the timer thread, every tick: mix blocks until DSND_AHEAD frames are queued ahead of the play cursor.
static void dsnd_Callback(int index, intptr_t param)
{
    uint64_t played = J3DSoundStream_GetPlayed();
    uint64_t written = J3DSoundStream_GetWritten();
    int blocks;

    (void)index;
    (void)param;
    if (written < played) {
        written = played; // an underrun; J3DSoundStream_Write resumes at the cursor
    }
    for (blocks = 0; blocks < DSND_MAXFILL && written - played < DSND_AHEAD; ++blocks) {
        mixer_Begin(DSND_BLOCK);
        dsnd_pfnFill(DSND_BLOCK, mixer_Feed);
        mixer_Mix(dsnd_scratch, DSND_BLOCK);
        J3DSoundStream_Write(dsnd_scratch, DSND_BLOCK);
        written += DSND_BLOCK;
    }
    dsnd_pTimer->SetInterval(dsnd_hTimer, dsnd_pTimer->GetTickLen());
}
