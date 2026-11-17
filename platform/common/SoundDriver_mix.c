/*
 * SoundDriver for the port (PORT-PLAN.md P4.3 to P4.6): Libs/sound/Driver.h over a software mixer, in place of
 * retail's DirectSound driver (Libs/sound/Driver.c, excluded from the build).
 *
 * What it emulates. Retail's driver had two paths, and this one has both (PORT-PLAN.md decision 19):
 *
 *   - Without 3D sound (the "3D sound" option off, retail's default): the driver asks the game for each channel's
 *     volume and pan, sithSoundMixer_CalcCameraRelativeSoundMix, and sets them on plain buffers, mixed in software.
 *   - With 3D sound on: mono positioned sounds are 3D voices, as DirectSound 3D buffers were. The driver applies
 *     DirectSound 3D's distance model (inverse distance between each voice's minimum and maximum distance, with the
 *     game's rolloff) and doppler (with the game's doppler and distance factors, from each sound's velocity and the
 *     camera's), and the device places the voice around the listener (HRTF through AVAudioEnvironmentNode on the
 *     apple build). The driver reports 3D hardware in every build, so the game's channel bookkeeping, and with it every
 *     save, is the same with or without a device.
 *
 * Two clocks. Everything the sound module can observe about a voice (playing or not, its play cursor) is computed from
 * SoundDriver_GetTimeMsec, the engine clock: the position advances by frequency x elapsed time while the voice plays.
 * The output device renders from a cursor of its own, in real time, and is never consulted. A run on the virtual or
 * frame clock (headless, --record, --replay) therefore sees exactly the same sound state every time, which keeps saves
 * and recorded runs byte-identical, and the null build, which has no device, runs the sound module unchanged.
 *
 * Doppler and the 3D distance gain change only what is rendered, never the engine-clock state, so a 3D voice's play
 * cursor can lead or trail what is heard by the doppler shift (a few percent at most).
 *
 * Threads. The game thread calls the driver; the device's threads call SoundDriver_Render and SoundDriver_RenderSlot.
 * SoundDriver_lock guards the voice list, the slot table and every field the renderers read; the game thread holds it
 * only for a few stores at a time.
 *
 * Retail code kept unchanged: the decibel table, the pan table, the four conversion functions (P4.5),
 * CalcListeneSoundMix, CalcDistToListener, and Update3DSound and CreateAndPlay's handling of the channel flags.
 */
#include <sound/Driver.h>
#include <sound/AudioLib.h>

#include <rdroid/Math/rdVector.h>
#include <std/General/stdMath.h>

#include <platform/common/host_audio.h>
#include <platform/common/sound_stream.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <Windows.h> // timeGetTime, Sleep (crt_posix.c)

#define SOUNDDRIVER_FARVOLUMETHRESHOLD 0.01

// DirectSound's limits (dsound.h), in hundredths of a decibel and in hertz.
#define SOUNDDRIVER_VOLUME_MIN    (-10000)
#define SOUNDDRIVER_VOLUME_MAX    0
#define SOUNDDRIVER_PAN_MAX       10000
#define SOUNDDRIVER_FREQUENCY_MIN 100
#define SOUNDDRIVER_FREQUENCY_MAX 100000

// DirectSound 3D: the speed of sound in metres per second; the game's distance factor turns its units into metres.
#define SOUNDDRIVER_SPEEDOFSOUND  343.3

// The PCM of one buffer, shared by a buffer and its duplicates (DuplicateSoundBuffer shared memory too).
typedef struct SoundVoiceData
{
    int refCount;
    size_t numFrames;
    int16_t aSamples[]; // numFrames x numChannels, 16-bit signed whatever the source was
} SoundVoiceData;

struct SoundVoice
{
    SoundVoice* pNext;
    SoundVoiceData* pData;
    uint32_t numChannels;
    uint32_t bitsPerSample; // of the source; GetCurrentPosition answers in its bytes
    uint32_t originalFrequency;
    tSoundChannelFlag caps; // SOUND_CHANNEL_GLOBALFOCUS

    // What the game sees, on the engine clock (game thread only).
    int32_t volume;     // hundredths of a dB, SOUNDDRIVER_VOLUME_MIN to 0
    int32_t pan;        // hundredths of a dB of attenuation on the far side, negative = left
    uint32_t frequency; // Hz
    int bPlaying;
    int bLoop;
    double position;    // in frames, at positionTime
    uint32_t positionTime;

    // 3D (game thread): a DirectSound 3D buffer's state.
    int b3D;            // made with DSBCAPS_CTRL3D: mono, 3D sound on, not a 2D sound
    int bHeadRelative;  // DS3DMODE_DISABLE: no placement, heard at the listener
    int bPlaced;        // a position has been set; until then it is placed at once instead of at the next commit
    rdVector3 pos3D;
    rdVector3 vel3D;
    float minDistance;
    float maxDistance;

    // What the renderers read (under SoundDriver_lock).
    int slot;           // the device's 3D slot, or -1: then (or with no 3D) it is mixed into the stereo bus
    int bRender;
    double renderPosition;
    double renderStep;  // source frames per output frame
    float gainLeft;
    float gainRight;
    float gain3D;       // DirectSound volume x 3D distance gain
    float gain3DDistance;
    double doppler;     // render-rate factor from doppler
};

static float SoundDriver_maxVolume = 1.0f;
static float SoundDriver_defaultDistanceFactor = 0.3f;
static float SoundDriver_minDistance = 15.0f;
static float SoundDriver_maxDistance = 10000.0f;
static float SoundDriver_defaultRolloffFactor = 1.0f;
static float SoundDriver_defaultDopplerFactor = 1.0f;

static tPrintfFunc SoundDriver_pfLogError = 0;
static int SoundDriver_aPanTable[256] = { 0 };
static float SoundDriver_frontOrientATan = 0.0f;
static int SoundDriver_bNoDSound3D = 0;
static int SoundDriver_bUseGlobalFocusBuf = 0;
static SoundCalcListenerSoundMixFunc SoundDriver_pfCalcListenerSoundMix = 0;
static SoundDriverGetSoundBufferDataFunc SoundDriver_pfGetSoundBufferData = 0;
static rdVector3 SoundDriver_curListenerPos = { 0 };

// Port: the driver's own state
static int SoundDriver_bOpen = 0;
static struct SoundDevice { int unused; } SoundDriver_device; // the handle GetDSound hands out while open
static pthread_mutex_t SoundDriver_lock = PTHREAD_MUTEX_INITIALIZER;
static SoundVoice* SoundDriver_pVoices = NULL; // every live voice, rendered or not
static int SoundDriver_bHasDevice = 0;
static uint32_t SoundDriver_outputRate = 48000;
static volatile int SoundDriver_bFocused = 1; // read by the renderer without the lock: a stale value costs one block

// 3D (decision 19). SoundDriver_bUse3DCaps is retail's: 3D voices are made while it is set.
static int SoundDriver_bUse3DCaps = 0;
static int SoundDriver_bReverse = 0;          // the game's "Reverse sound": Sound_Update's vectors are not negated
static SoundVoice* SoundDriver_apSlots[J3DAUDIO_NUM3DSLOTS] = { 0 };
static rdVector3 SoundDriver_listenerFront = { 0.0f, 1.0f, 0.0f };
static rdVector3 SoundDriver_listenerUp = { 0.0f, 0.0f, 1.0f };
static rdVector3 SoundDriver_listenerVel = { 0 };
static int SoundDriver_bListenerVelGiven = 0;  // the game passed a velocity (it passes NULL; then it is derived)
static rdVector3 SoundDriver_prevListenerPos = { 0 };
static uint32_t SoundDriver_prevListenerTime = 0;
static int SoundDriver_bHasPrevListener = 0;

// The movie stream (sound_stream.h, P5.9). Under SoundDriver_lock.
static struct
{
    int bOpen;
    uint32_t rate;
    uint64_t written;          // frames
    double renderPosition;     // frames the device has consumed (with a device)
    uint64_t openTimeNs;       // host monotonic time when opened (without one)
    float gain;
    int16_t aRing[J3DSOUNDSTREAM_CAPACITY * 2];
} SoundDriver_stream;

static const int SoundDriver_aDBTable[256] = {
    -9640, -4810, -4210, -3850, -3600, -3410, -3250, -3120, -3000, -2900, -2810, -2730, -2650, -2580, -2520, -2460,
    -2400, -2350, -2300, -2250, -2210, -2160, -2120, -2080, -2050, -2010, -1980, -1950, -1910, -1880, -1850, -1830,
    -1800, -1770, -1750, -1720, -1700, -1670, -1650, -1630, -1600, -1580, -1560, -1540, -1520, -1500, -1480, -1460,
    -1450, -1430, -1410, -1390, -1380, -1360, -1340, -1330, -1310, -1300, -1280, -1270, -1250, -1240, -1220, -1210,
    -1200, -1180, -1170, -1160, -1140, -1130, -1120, -1110, -1090, -1080, -1070, -1060, -1050, -1040, -1020, -1010,
    -1000,  -990,  -980,  -970,  -960,  -950,  -940,  -930,  -920,  -910,  -900,  -890,  -880,  -870,  -860,  -850,
     -840,  -830,  -830,  -820,  -810,  -800,  -790,  -780,  -770,  -770,  -760,  -750,  -740,  -730,  -730,  -720,
     -710,  -700,  -690,  -690,  -680,  -670,  -660,  -660,  -650,  -640,  -640,  -630,  -620,  -610,  -610,  -600,
     -590,  -590,  -580,  -570,  -570,  -560,  -550,  -550,  -540,  -530,  -530,  -520,  -520,  -510,  -500,  -500,
     -490,  -490,  -480,  -470,  -470,  -460,  -460,  -450,  -440,  -440,  -430,  -430,  -420,  -420,  -410,  -410,
     -400,  -390,  -390,  -380,  -380,  -370,  -370,  -360,  -360,  -350,  -350,  -340,  -340,  -330,  -330,  -320,
     -320,  -310,  -310,  -300,  -300,  -290,  -290,  -280,  -280,  -270,  -270,  -260,  -260,  -260,  -250,  -250,
     -240,  -240,  -230,  -230,  -220,  -220,  -210,  -210,  -210,  -200,  -200,  -190,  -190,  -180,  -180,  -180,
     -170,  -170,  -160,  -160,  -160,  -150,  -150,  -140,  -140,  -140,  -130,  -130,  -120,  -120,  -120,  -110,
     -110,  -100,  -100,  -100,   -90,   -90,   -80,   -80,   -80,   -70,   -70,   -70,   -60,   -60,   -50,   -50,
      -50,   -40,   -40,   -40,   -30,   -30,   -30,   -20,   -20,   -20,   -10,   -10,   -10,   -10,   -10,   -10
};

void J3DAPI SoundDriver_InitPanTable(void);
void J3DAPI SoundDriver_CalcListeneSoundMix(float x, float y, float z, float volume, float pitch, float* newVolume, float* pan, float* newPitch, float minRadius, float maxRadius, SoundEnvFlags envflags);
double J3DAPI SoundDriver_CalcDistToListener(float x1, float y1, float z1, float x2, float y2, float z2);

static void SoundDriver_Render(float* pLeft, float* pRight, uint32_t frameCount, void* pUser);
static int SoundDriver_RenderSlot(int slot, float* pMono, uint32_t frameCount, void* pUser);
static void SoundDriver_Place3DVoice(SoundVoice* pVoice);

/* ---- Voices ------------------------------------------------------------------------------------------------------- */

// Brings the engine-clock position up to now: a voice that ran off its end stops, as a DirectSound buffer did, with its
// cursor back at the start.
static void SoundDriver_AdvanceVoice(SoundVoice* pVoice)
{
    uint32_t now = SoundDriver_GetTimeMsec();
    if ( pVoice->bPlaying )
    {
        double numFrames = (double)pVoice->pData->numFrames;
        pVoice->position += (double)(uint32_t)(now - pVoice->positionTime) * (double)pVoice->frequency / 1000.0;
        if ( pVoice->position >= numFrames )
        {
            if ( pVoice->bLoop && numFrames > 0.0 )
            {
                pVoice->position = fmod(pVoice->position, numFrames);
            }
            else
            {
                pVoice->bPlaying = 0;
                pVoice->position = 0.0;
            }
        }
    }

    pVoice->positionTime = now;
}

// Recomputes what the renderer needs from the voice's volume, pan and frequency. Caller holds the lock.
static void SoundDriver_UpdateRenderParams(SoundVoice* pVoice)
{
    // DirectSound: volume and pan are attenuations in hundredths of a decibel; the minimum is silence.
    float gain = pVoice->volume <= SOUNDDRIVER_VOLUME_MIN ? 0.0f : powf(10.0f, (float)pVoice->volume / 2000.0f);
    float left  = gain;
    float right = gain;
    if ( pVoice->pan > 0 )
    {
        left *= pVoice->pan >= SOUNDDRIVER_PAN_MAX ? 0.0f : powf(10.0f, -(float)pVoice->pan / 2000.0f);
    }
    else if ( pVoice->pan < 0 )
    {
        right *= pVoice->pan <= -SOUNDDRIVER_PAN_MAX ? 0.0f : powf(10.0f, (float)pVoice->pan / 2000.0f);
    }

    if ( pVoice->b3D )
    {
        // A DirectSound 3D buffer has no pan; its distance gain comes from SoundDriver_Place3DVoice.
        pVoice->gain3D    = gain * pVoice->gain3DDistance;
        pVoice->gainLeft  = pVoice->gain3D; // used only when no slot was free: heard centred
        pVoice->gainRight = pVoice->gain3D;
        pVoice->renderStep = (double)pVoice->frequency * pVoice->doppler / (double)SoundDriver_outputRate;
        return;
    }

    pVoice->gainLeft   = left;
    pVoice->gainRight  = right;
    pVoice->renderStep = (double)pVoice->frequency / (double)SoundDriver_outputRate;
}

static SoundVoice* SoundDriver_NewVoice(SoundVoiceData* pData, uint32_t numChannels, uint32_t bitsPerSample, uint32_t frequency)
{
    SoundVoice* pVoice = (SoundVoice*)calloc(1, sizeof(SoundVoice));
    if ( !pVoice )
    {
        return NULL;
    }

    pVoice->pData             = pData;
    pVoice->numChannels       = numChannels;
    pVoice->bitsPerSample     = bitsPerSample;
    pVoice->originalFrequency = frequency;
    pVoice->frequency         = frequency;
    pVoice->volume            = SOUNDDRIVER_VOLUME_MAX;
    pVoice->positionTime      = SoundDriver_GetTimeMsec();
    pVoice->slot              = -1;
    pVoice->gain3DDistance    = 1.0f;
    pVoice->doppler           = 1.0;
    pVoice->minDistance       = -1.0f;
    pVoice->maxDistance       = -1.0f;
    ++pData->refCount;

    pthread_mutex_lock(&SoundDriver_lock);
    SoundDriver_UpdateRenderParams(pVoice);
    pVoice->pNext = SoundDriver_pVoices;
    SoundDriver_pVoices = pVoice;
    pthread_mutex_unlock(&SoundDriver_lock);
    return pVoice;
}

// Converts a buffer's PCM (8-bit unsigned or 16-bit signed, as DirectSound took it) to the mixer's 16-bit samples.
static SoundVoiceData* SoundDriver_NewVoiceData(const uint8_t* pPCM, size_t dataSize, uint32_t numChannels, uint32_t bitsPerSample)
{
    size_t bytesPerSample = bitsPerSample == 8 ? 1 : 2;
    size_t numFrames = dataSize / (bytesPerSample * numChannels);
    size_t numSamples = numFrames * numChannels;
    SoundVoiceData* pData = (SoundVoiceData*)malloc(sizeof(SoundVoiceData) + numSamples * sizeof(int16_t));
    if ( !pData )
    {
        return NULL;
    }

    pData->refCount  = 0;
    pData->numFrames = numFrames;
    if ( bytesPerSample == 1 )
    {
        for ( size_t i = 0; i < numSamples; ++i )
        {
            pData->aSamples[i] = (int16_t)(((int)pPCM[i] - 128) << 8);
        }
    }
    else
    {
        memcpy(pData->aSamples, pPCM, numSamples * sizeof(int16_t)); // little-endian on both ends
    }

    return pData;
}

static int SoundDriver_IsSupportedFormat(int samplesPerSec, int nBitsPerSample, int numChannels)
{
    return samplesPerSec > 0 && (nBitsPerSample == 8 || nBitsPerSample == 16) && (numChannels == 1 || numChannels == 2);
}

/* ---- Device level (P4.3) ------------------------------------------------------------------------------------------ */

int J3DAPI SoundDriver_Open(int bNoSound3D, int bGlobalFocus, HWND hwnd, SoundDevice* pDirectSound, SoundCalcListenerSoundMixFunc pfCalcListenerSoundMix, SoundDriverGetSoundBufferDataFunc pfGetSoundBufferData, tPrintfFunc pfLogError)
{
    (void)hwnd;
    (void)pDirectSound; // SOUNDOPEN_HWNDISDIRECTSOUND: the game never passes its own device

    SoundDriver_pfLogError         = pfLogError;
    SoundDriver_bNoDSound3D        = bNoSound3D;
    SoundDriver_bUseGlobalFocusBuf = bGlobalFocus;
    SoundDriver_bUse3DCaps         = !bNoSound3D; // retail: when the hardware has 3D buffers, which the port always has
    SoundDriver_bHasPrevListener   = 0;
    rdVector_Set3(&SoundDriver_listenerVel, 0.0f, 0.0f, 0.0f);

    SoundDriver_InitPanTable();

    if ( !SoundDriver_bOpen )
    {
        SoundDriver_outputRate = 48000;
        SoundDriver_bHasDevice = J3DAudio_OpenDevice(SoundDriver_Render, SoundDriver_RenderSlot, NULL, &SoundDriver_outputRate);
        J3DAudio_SetMirror(SoundDriver_bReverse);
        if ( !SoundDriver_bHasDevice || SoundDriver_outputRate == 0 )
        {
            SoundDriver_outputRate = 48000; // voices keep their render parameters consistent; nothing pulls them
        }

        SoundDriver_bOpen = 1;
    }

    SoundDriver_pfCalcListenerSoundMix = pfCalcListenerSoundMix;
    SoundDriver_pfGetSoundBufferData   = pfGetSoundBufferData;
    return 1; // without an output device the driver still keeps every voice on the engine clock, silently
}

void J3DAPI SoundDriver_Close(void)
{
    SoundVoice* pVoice;

    if ( !SoundDriver_bOpen )
    {
        return;
    }

    J3DAudio_CloseDevice();
    SoundDriver_bHasDevice = 0;

    // Sound_Close releases its channels first; free anything left so a reopen (Sound_Set3DHWState) starts clean.
    pVoice = SoundDriver_pVoices;
    SoundDriver_pVoices = NULL;
    memset(SoundDriver_apSlots, 0, sizeof(SoundDriver_apSlots));
    while ( pVoice )
    {
        SoundVoice* pNext = pVoice->pNext;
        if ( --pVoice->pData->refCount == 0 )
        {
            free(pVoice->pData);
        }

        free(pVoice);
        pVoice = pNext;
    }

    SoundDriver_bOpen = 0;
}

void J3DAPI SoundDriver_SetMaxVolume(float volume)
{
    SoundDriver_maxVolume = volume;
}

// The port always has 3D voices (decision 19): with no device they are kept but not heard, so the channel flags, and
// every save, do not depend on whether a device is open.
int J3DAPI SoundDriver_Has3DHW(void)
{
    return 1;
}

int J3DAPI SoundDriver_Use3DCaps(void)
{
    return SoundDriver_bUse3DCaps;
}

void J3DAPI SoundDriver_SetReverseSound(int bReverse)
{
    SoundDriver_bReverse = bReverse != 0;
    J3DAudio_SetMirror(SoundDriver_bReverse);
}

SoundDevice* J3DAPI SoundDriver_GetDSound(void)
{
    return SoundDriver_bOpen ? &SoundDriver_device : NULL;
}

// The DirectSound object and primary buffer have no counterpart: the output device belongs to SoundDriver_Open.
SoundDevice* J3DAPI SoundDriver_CreateDirectSound(int bNoSound3D)
{
    (void)bNoSound3D;
    return SoundDriver_GetDSound();
}

void J3DAPI SoundDriver_ReleaseDirectSound(SoundDevice* pDSound)
{
    (void)pDSound;
}

void J3DAPI SoundDriver_CreateListener(void)
{
}

void J3DAPI SoundDriver_ReleaseListener(void)
{
}

// Retail asked for a 22,050 Hz 16-bit stereo primary buffer. The mixer works in float at the device's rate; there is
// nothing to set.
void J3DAPI SoundDriver_SetOuputFormat(SoundDevice* pDSound, uint32_t nSamplesPerSec, uint32_t nBitsPerSample, uint32_t numChannels)
{
    (void)pDSound;
    (void)nSamplesPerSec;
    (void)nBitsPerSample;
    (void)numChannels;
}

SoundVoice* J3DAPI SoundDriver_GetPrimaryDSBuffer(SoundDevice* pDSound)
{
    (void)pDSound;
    return NULL;
}

void J3DAPI SoundDriver_ReleasePrimaryDSBuffer(void)
{
}

uint32_t J3DAPI SoundDriver_GetTimeMsec(void)
{
    return timeGetTime();
}

void J3DAPI SoundDriver_Sleep(unsigned int dwMilliseconds)
{
    Sleep(dwMilliseconds);
}

/* ---- Voices (P4.4) ------------------------------------------------------------------------------------------------ */

// Makes a voice a 3D voice and gives it a free device slot (none free: it is heard centred on the stereo bus).
static void SoundDriver_Make3D(SoundVoice* pVoice, int b3D)
{
    pthread_mutex_lock(&SoundDriver_lock);
    pVoice->b3D = b3D != 0;
    if ( pVoice->b3D )
    {
        for ( int i = 0; i < J3DAUDIO_NUM3DSLOTS; ++i )
        {
            if ( !SoundDriver_apSlots[i] )
            {
                SoundDriver_apSlots[i] = pVoice;
                pVoice->slot = i;
                break;
            }
        }
    }

    SoundDriver_UpdateRenderParams(pVoice);
    pthread_mutex_unlock(&SoundDriver_lock);
}

SoundVoice* J3DAPI SoundDriver_CreateAndPlay(int samplesPerSec, int nBitsPerSample, int numChannels, const uint8_t* pSoundData, size_t dataSize, tSoundChannelFlag* pFlags, SoundVoice* pSBuffer)
{
    SoundVoice* pVoice;

    if ( !SoundDriver_bOpen )
    {
        return NULL;
    }

    if ( pSBuffer )
    {
        // DuplicateSoundBuffer: shares the PCM and takes the original's format, volume, pan and frequency, from the
        // start. (Sound_Play clears the buffer it would duplicate before calling, in retail too, so this is not reached.)
        pVoice = SoundDriver_NewVoice(pSBuffer->pData, pSBuffer->numChannels, pSBuffer->bitsPerSample, pSBuffer->originalFrequency);
        if ( !pVoice )
        {
            return NULL;
        }

        pVoice->caps      = pSBuffer->caps;
        pVoice->volume    = pSBuffer->volume;
        pVoice->pan       = pSBuffer->pan;
        pVoice->frequency = pSBuffer->frequency;
        SoundDriver_Make3D(pVoice, pSBuffer->b3D);
        if ( SoundDriver_bUse3DCaps && pVoice->b3D )
        {
            *pFlags |= SOUND_CHANNEL_HWMIXING;
        }

        *pFlags |= SOUND_CHANNEL_UNKNOWN_800000;
    }
    else
    {
        SoundVoiceData* pData;

        if ( !pSoundData || !SoundDriver_IsSupportedFormat(samplesPerSec, nBitsPerSample, numChannels) )
        {
            return NULL;
        }

        if ( (*pFlags & SOUND_CHANNEL_COMPRESSED) != 0 )
        {
            // tAudioCompressedData: the uncompressed size, then the stream; read by offset, without assuming alignment.
            const uint8_t* pStream = pSoundData + offsetof(tAudioCompressedData, compressedData);
            uint32_t uncompressedSize;
            uint8_t* pUncompressed;
            memcpy(&uncompressedSize, pSoundData + offsetof(tAudioCompressedData, uncompressedSize), sizeof(uncompressedSize));
            pUncompressed = (uint8_t*)malloc(uncompressedSize ? uncompressedSize : 1);
            tAudioCompressorState compressor;
            if ( !pUncompressed )
            {
                return NULL;
            }

            AudioLib_ResetCompressor(&compressor);
            AudioLib_Uncompress(&compressor, pUncompressed, pStream, uncompressedSize);
            pData = SoundDriver_NewVoiceData(pUncompressed, uncompressedSize, (uint32_t)numChannels, (uint32_t)nBitsPerSample);
            free(pUncompressed);
        }
        else
        {
            pData = SoundDriver_NewVoiceData(pSoundData, dataSize, (uint32_t)numChannels, (uint32_t)nBitsPerSample);
        }

        if ( !pData )
        {
            return NULL;
        }

        pVoice = SoundDriver_NewVoice(pData, (uint32_t)numChannels, (uint32_t)nBitsPerSample, (uint32_t)samplesPerSec);
        if ( !pVoice )
        {
            free(pData);
            return NULL;
        }

        if ( SoundDriver_bUseGlobalFocusBuf || (*pFlags & SOUND_CHANNEL_GLOBALFOCUS) != 0 )
        {
            pVoice->caps |= SOUND_CHANNEL_GLOBALFOCUS;
            *pFlags |= SOUND_CHANNEL_GLOBALFOCUS;
        }

        // Retail: with 3D sound, every sound the game positions is a 3D buffer, except a stereo one, which cannot be
        // and is marked so (UNKNOWN_8000000). Sounds played as 2D carry SOUND_CHANNEL_USE3DCAPS (Sound_Play).
        if ( SoundDriver_bUse3DCaps && (*pFlags & SOUND_CHANNEL_USE3DCAPS) == 0 )
        {
            *pFlags |= SOUND_CHANNEL_3DSOUND;
            if ( numChannels == 2 )
            {
                *pFlags |= SOUND_CHANNEL_UNKNOWN_8000000;
            }
            else
            {
                SoundDriver_Make3D(pVoice, 1);
            }
        }

        // Retail required every buffer to be in hardware with 3D sound on, and set SOUND_CHANNEL_HWMIXING on all of them,
        // which keeps Update3DSound from setting a volume. Port: only a 3D voice is marked so. A stereo positioned sound,
        // which hardware could not place, would otherwise play at full volume at any distance; it gets the game's mix,
        // as without 3D sound.
        if ( SoundDriver_bUse3DCaps && pVoice->b3D )
        {
            *pFlags |= SOUND_CHANNEL_HWMIXING;
        }
        else
        {
            *pFlags &= ~SOUND_CHANNEL_HWMIXING;
        }

        // Retail: a channel being restarted from far away starts at the bottom of the scale (its volume is set again
        // by the next Update3DSound); every other buffer starts at full volume.
        pVoice->volume = (int32_t)(SoundDriver_maxVolume * (double)((*pFlags & SOUND_CHANNEL_RESTART) != 0 ? SOUNDDRIVER_VOLUME_MIN : SOUNDDRIVER_VOLUME_MAX));
        pVoice->volume = pVoice->volume < SOUNDDRIVER_VOLUME_MIN ? SOUNDDRIVER_VOLUME_MIN : pVoice->volume > SOUNDDRIVER_VOLUME_MAX ? SOUNDDRIVER_VOLUME_MAX : pVoice->volume;
        pthread_mutex_lock(&SoundDriver_lock);
        SoundDriver_UpdateRenderParams(pVoice);
        pthread_mutex_unlock(&SoundDriver_lock);
    }

    if ( (*pFlags & SOUND_CHANNEL_PLAYING) != 0 )
    {
        SoundDriver_Play(pVoice, (*pFlags & (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)) == (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING));
    }

    return pVoice;
}

void J3DAPI SoundDriver_Release(SoundVoice* pDSBuf)
{
    SoundVoice** ppLink;

    if ( !pDSBuf )
    {
        return;
    }

    pthread_mutex_lock(&SoundDriver_lock);
    if ( pDSBuf->slot >= 0 )
    {
        SoundDriver_apSlots[pDSBuf->slot] = NULL;
    }

    for ( ppLink = &SoundDriver_pVoices; *ppLink; ppLink = &(*ppLink)->pNext )
    {
        if ( *ppLink == pDSBuf )
        {
            *ppLink = pDSBuf->pNext;
            break;
        }
    }
    pthread_mutex_unlock(&SoundDriver_lock);

    if ( --pDSBuf->pData->refCount == 0 )
    {
        free(pDSBuf->pData);
    }

    free(pDSBuf);
}

// DirectSound's Play: starts from the current position, which Stop leaves where it was (Sound_Pause and Sound_Resume
// rely on that); bLoop replaces the looping state.
void J3DAPI SoundDriver_Play(SoundVoice* pDSBuf, int bLoop)
{
    if ( !pDSBuf )
    {
        return;
    }

    SoundDriver_AdvanceVoice(pDSBuf);
    pDSBuf->bPlaying = 1;

    pthread_mutex_lock(&SoundDriver_lock);
    pDSBuf->bLoop = bLoop != 0; // the renderer reads it
    pDSBuf->renderPosition = pDSBuf->position;
    pDSBuf->bRender = 1;
    pthread_mutex_unlock(&SoundDriver_lock);
}

void J3DAPI SoundDriver_Stop(SoundVoice* pDSBuf)
{
    if ( !pDSBuf )
    {
        return;
    }

    SoundDriver_AdvanceVoice(pDSBuf);
    pDSBuf->bPlaying = 0;

    pthread_mutex_lock(&SoundDriver_lock);
    pDSBuf->bRender = 0;
    pthread_mutex_unlock(&SoundDriver_lock);
}

size_t J3DAPI SoundDriver_GetCurrentPosition(SoundVoice* pDSoundBuf)
{
    if ( !pDSoundBuf )
    {
        return 0;
    }

    SoundDriver_AdvanceVoice(pDSoundBuf);
    return (size_t)pDSoundBuf->position * pDSoundBuf->numChannels * (pDSoundBuf->bitsPerSample / 8);
}

tSoundChannelFlag J3DAPI SoundDriver_GetStatusAndCaps(SoundVoice* pDSBuffer)
{
    tSoundChannelFlag flags = 0;

    if ( !pDSBuffer )
    {
        return 0;
    }

    SoundDriver_AdvanceVoice(pDSBuffer);
    if ( pDSBuffer->bPlaying )
    {
        flags |= pDSBuffer->bLoop ? SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING : SOUND_CHANNEL_PLAYING;
    }

    if ( pDSBuffer->b3D )
    {
        flags |= SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_HWMIXING; // DSBCAPS_CTRL3D, DSBCAPS_LOCHARDWARE
    }
    else
    {
        flags |= SOUND_CHANNEL_SWMIXING;
    }

    return flags | (pDSBuffer->caps & SOUND_CHANNEL_GLOBALFOCUS);
}

/* ---- Volume, pan, frequency (P4.5) -------------------------------------------------------------------------------- */

void J3DAPI SoundDriver_SetVolume(SoundVoice* pDSBuf, float volume)
{
    if ( !pDSBuf )
    {
        return;
    }

    pthread_mutex_lock(&SoundDriver_lock);
    pDSBuf->volume = SoundDriver_GetDecibelVolume(volume);
    SoundDriver_UpdateRenderParams(pDSBuf);
    pthread_mutex_unlock(&SoundDriver_lock);
}

float J3DAPI SoundDriver_GetVolume(SoundVoice* pDSBuf)
{
    if ( !pDSBuf )
    {
        return 1.0f;
    }

    return SoundDriver_GetVolumeFromDecibels(pDSBuf->volume);
}

void J3DAPI SoundDriver_SetPan(SoundVoice* pDSoundBuf, float pan)
{
    if ( !pDSoundBuf )
    {
        return;
    }

    pthread_mutex_lock(&SoundDriver_lock);
    pDSoundBuf->pan = SoundDriver_GetIntPan(pan);
    SoundDriver_UpdateRenderParams(pDSoundBuf);
    pthread_mutex_unlock(&SoundDriver_lock);
}

// Retail's linker discarded this function and its only caller; its body was a placeholder returning 0.0f, kept.
float J3DAPI SoundDriver_GetPan(SoundVoice* pDSBuf)
{
    (void)pDSBuf;
    return 0.0f;
}

void J3DAPI SoundDriver_SetFrequency(SoundVoice* pDSBuf, float freq)
{
    int32_t frequency = (int32_t)freq;

    if ( !pDSBuf )
    {
        return;
    }

    // DirectSound: 0 restores the buffer's own rate; outside 100 Hz to 100 kHz the call failed and changed nothing.
    if ( frequency == 0 )
    {
        frequency = (int32_t)pDSBuf->originalFrequency;
    }
    else if ( frequency < SOUNDDRIVER_FREQUENCY_MIN || frequency > SOUNDDRIVER_FREQUENCY_MAX )
    {
        return;
    }

    SoundDriver_AdvanceVoice(pDSBuf); // the position so far was made at the old rate
    pthread_mutex_lock(&SoundDriver_lock);
    pDSBuf->frequency = (uint32_t)frequency;
    SoundDriver_UpdateRenderParams(pDSBuf);
    pthread_mutex_unlock(&SoundDriver_lock);
}

float J3DAPI SoundDriver_GetFrequency(SoundVoice* pDSBuf)
{
    if ( !pDSBuf )
    {
        return 22050.0f;
    }

    return (float)(int)pDSBuf->frequency;
}

int J3DAPI SoundDriver_GetDecibelVolume(float volume)
{
    int idx = (int)(volume * 255.0);
    if ( idx < 0 )
    {
        idx = 0;
    }

    return idx > 255 ? 0 : SoundDriver_aDBTable[idx];
}

int J3DAPI SoundDriver_GetIntPan(float pan)
{
    int idx = (int)((pan * 128.0) + 128.0);
    if ( idx < 0 )
    {
        idx = 0;
    }

    return idx > 255 ? 0 : SoundDriver_aPanTable[idx];
}

float J3DAPI SoundDriver_GetVolumeFromDecibels(int dB)
{
    int idx = 256;
    while ( idx-- && SoundDriver_aDBTable[idx] > dB )
    {
        ;
    }

    return ((float)(idx * 1.0 / 255.0));
}

float J3DAPI SoundDriver_GetPanFromTable(int pan)
{
    return (float)(pan / 128.0);
}

void J3DAPI SoundDriver_InitPanTable(void)
{
    int i;
    for ( i = 0; i < 128; ++i )
    {
        SoundDriver_aPanTable[i + 128] = -SoundDriver_aDBTable[255 - 2 * i];
        SoundDriver_aPanTable[127 - i] = SoundDriver_aDBTable[255 - 2 * i];
    }
}

/* ---- Listener and placement (P4.6) -------------------------------------------------------------------------------- */

void J3DAPI SoundDriver_SetListenerPosition(const rdVector3* pPos, const rdVector3* pVelocity, const rdVector3* pTopOrient, const rdVector3* pFrontOrient, const rdVector3* pRightOrient)
{
    (void)pRightOrient;

    if ( pPos )
    {
        SoundDriver_curListenerPos.x = pPos->x;
        SoundDriver_curListenerPos.y = pPos->y;
        SoundDriver_curListenerPos.z = pPos->z;
    }

    // The 3D listener (applied at ListenerCommitDeferred, as DirectSound's deferred settings were). sithSoundMixer_Update
    // passes the camera's up vector and its NEGATED look vector, and Sound_Update then negates both unless "Reverse sound"
    // is set. So the driver gets (front, top) = (lvec, -uvec) normally and (-lvec, uvec) reversed: retail's DirectSound
    // listener faced the camera's way with top flipped, which in its left-handed frame put the camera's right on the
    // right ear. The port's frame is right-handed, so it places the listener exactly as the camera is, (lvec, uvec), in
    // both cases; "Reverse sound" is the device's mirror instead (decision 19).
    if ( pFrontOrient && pTopOrient )
    {
        float frontSign = SoundDriver_bReverse ? -1.0f : 1.0f;
        float upSign    = -frontSign;
        rdVector_Set3(&SoundDriver_listenerFront, frontSign * pFrontOrient->x, frontSign * pFrontOrient->y, frontSign * pFrontOrient->z);
        rdVector_Set3(&SoundDriver_listenerUp, upSign * pTopOrient->x, upSign * pTopOrient->y, upSign * pTopOrient->z);
    }

    SoundDriver_bListenerVelGiven = pVelocity != NULL;
    if ( pVelocity )
    {
        SoundDriver_listenerVel = *pVelocity;
    }

    if ( !SoundDriver_pfCalcListenerSoundMix && pTopOrient && pFrontOrient )
    {
        if ( pFrontOrient->x != 0.0f )
        {
            SoundDriver_frontOrientATan = (float)atan2(pFrontOrient->z, pFrontOrient->x);
            if ( errno == EDOM )
            {
                SoundDriver_frontOrientATan = 0.0f;
            }
        }
        else
        {
            SoundDriver_frontOrientATan = 0.0f;
        }
    }
}

// Applies a frame's listener and 3D voice changes together, as IDirectSound3DListener::CommitDeferredSettings did.
// The game passes no listener velocity (sithSoundMixer_Update gives NULL), so for doppler the port derives it from the
// camera's movement on the engine clock: a jump faster than half the speed of sound (a cut or a teleport) counts as
// standing still, and the estimate is smoothed over frames.
void J3DAPI SoundDriver_ListenerCommitDeferred(void)
{
    uint32_t now = SoundDriver_GetTimeMsec();
    float aPos[3], aFront[3], aUp[3];
    SoundVoice* pVoice;

    if ( !SoundDriver_bUse3DCaps )
    {
        SoundDriver_bHasPrevListener = 0;
        return;
    }

    if ( !SoundDriver_bListenerVelGiven )
    {
        if ( SoundDriver_bHasPrevListener && now != SoundDriver_prevListenerTime )
        {
            double dt = (double)(uint32_t)(now - SoundDriver_prevListenerTime) / 1000.0;
            rdVector3 v;
            double speed;
            rdVector_Set3(&v,
                (float)((SoundDriver_curListenerPos.x - SoundDriver_prevListenerPos.x) / dt),
                (float)((SoundDriver_curListenerPos.y - SoundDriver_prevListenerPos.y) / dt),
                (float)((SoundDriver_curListenerPos.z - SoundDriver_prevListenerPos.z) / dt));
            speed = sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z) * SoundDriver_defaultDistanceFactor;
            if ( speed > SOUNDDRIVER_SPEEDOFSOUND / 2.0 )
            {
                rdVector_Set3(&v, 0.0f, 0.0f, 0.0f);
            }

            rdVector_Set3(&SoundDriver_listenerVel,
                0.5f * (SoundDriver_listenerVel.x + v.x),
                0.5f * (SoundDriver_listenerVel.y + v.y),
                0.5f * (SoundDriver_listenerVel.z + v.z));
        }

        if ( !SoundDriver_bHasPrevListener || now != SoundDriver_prevListenerTime )
        {
            SoundDriver_prevListenerPos  = SoundDriver_curListenerPos;
            SoundDriver_prevListenerTime = now;
            SoundDriver_bHasPrevListener = 1;
        }
    }

    aPos[0]   = SoundDriver_curListenerPos.x; aPos[1]   = SoundDriver_curListenerPos.y; aPos[2]   = SoundDriver_curListenerPos.z;
    aFront[0] = SoundDriver_listenerFront.x;  aFront[1] = SoundDriver_listenerFront.y;  aFront[2] = SoundDriver_listenerFront.z;
    aUp[0]    = SoundDriver_listenerUp.x;     aUp[1]    = SoundDriver_listenerUp.y;     aUp[2]    = SoundDriver_listenerUp.z;
    J3DAudio_SetListener(aPos, aFront, aUp);

    for ( pVoice = SoundDriver_pVoices; pVoice; pVoice = pVoice->pNext ) // only the game thread changes the list
    {
        if ( pVoice->b3D && pVoice->bPlaced )
        {
            SoundDriver_Place3DVoice(pVoice);
        }
    }
}

// DirectSound 3D's model for one voice, against the current listener: the inverse distance law between the voice's
// minimum and maximum distance (the gain holds at the maximum distance, it does not fall to silence), and doppler from
// the voice's and the listener's velocity along the line between them. A head-relative voice (DS3DMODE_DISABLE) is
// heard at the listener, unattenuated and unshifted.
static void SoundDriver_Place3DVoice(SoundVoice* pVoice)
{
    float gain = 1.0f;
    double doppler = 1.0;
    float aPos[3];

    if ( pVoice->bHeadRelative )
    {
        aPos[0] = SoundDriver_curListenerPos.x; aPos[1] = SoundDriver_curListenerPos.y; aPos[2] = SoundDriver_curListenerPos.z;
    }
    else
    {
        double dx = pVoice->pos3D.x - SoundDriver_curListenerPos.x;
        double dy = pVoice->pos3D.y - SoundDriver_curListenerPos.y;
        double dz = pVoice->pos3D.z - SoundDriver_curListenerPos.z;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        double minDist = pVoice->minDistance < 0.0f ? SoundDriver_minDistance : pVoice->minDistance;
        double maxDist = pVoice->maxDistance < 0.0f ? SoundDriver_maxDistance : pVoice->maxDistance;

        if ( maxDist < minDist )
        {
            maxDist = minDist;
        }

        if ( dist > minDist && minDist > 0.0 )
        {
            double d = dist > maxDist ? maxDist : dist;
            gain = (float)(minDist / (minDist + SoundDriver_defaultRolloffFactor * (d - minDist)));
        }

        if ( dist > 1e-4 && SoundDriver_defaultDopplerFactor > 0.0f )
        {
            double scale = SoundDriver_defaultDistanceFactor * SoundDriver_defaultDopplerFactor / dist; // to m/s along the line
            double c  = SOUNDDRIVER_SPEEDOFSOUND;
            double vl = (SoundDriver_listenerVel.x * dx + SoundDriver_listenerVel.y * dy + SoundDriver_listenerVel.z * dz) * scale;
            double vs = (pVoice->vel3D.x * dx + pVoice->vel3D.y * dy + pVoice->vel3D.z * dz) * scale;
            vl = vl > c / 2.0 ? c / 2.0 : vl < -c / 2.0 ? -c / 2.0 : vl;
            vs = vs > c / 2.0 ? c / 2.0 : vs < -c / 2.0 ? -c / 2.0 : vs;
            doppler = (c + vl) / (c + vs); // listener toward the source raises it; the source moving away lowers it
        }

        aPos[0] = pVoice->pos3D.x; aPos[1] = pVoice->pos3D.y; aPos[2] = pVoice->pos3D.z;
    }

    pthread_mutex_lock(&SoundDriver_lock);
    pVoice->gain3DDistance = gain;
    pVoice->doppler = doppler;
    SoundDriver_UpdateRenderParams(pVoice);
    pthread_mutex_unlock(&SoundDriver_lock);

    if ( pVoice->slot >= 0 )
    {
        J3DAudio_SetSlotPosition(pVoice->slot, aPos);
    }
}

// Sets a 3D buffer's position, velocity and distances (DirectSound: deferred to the next commit). A voice that has never
// been placed is placed at once, so it does not start at the origin.
void J3DAPI SoundDriver_SetPosAndVelocity(SoundVoice* pBuffer, float x, float y, float z, float velX, float velY, float velZ, float minDistance, float maxDistance, SoundEnvFlags envflags)
{
    (void)envflags;

    if ( !pBuffer || !SoundDriver_bUse3DCaps || !pBuffer->b3D )
    {
        return;
    }

    rdVector_Set3(&pBuffer->pos3D, x, y, z);
    rdVector_Set3(&pBuffer->vel3D, velX, velY, velZ);
    pBuffer->minDistance = minDistance < 0.0f ? SoundDriver_minDistance : minDistance;
    pBuffer->maxDistance = maxDistance < 0.0f ? SoundDriver_maxDistance : maxDistance;
    if ( !pBuffer->bPlaced )
    {
        pBuffer->bPlaced = 1;
        SoundDriver_Place3DVoice(pBuffer);
    }
}

// Retail's body; the DirectSound 3D buffer mode switch sets the voice's head-relative flag.
int J3DAPI SoundDriver_Update3DSound(SoundVoice** ppDSBuf, float x, float y, float z, float volume, float pitch, tSoundChannelFlag* pChannelFlags, float minRadius, float maxRadius, SoundEnvFlags envflags)
{
    int b3DControl;
    float newVolume;
    float pan;
    float newPitch;

    SoundVoice* pDSBuf = ppDSBuf ? *ppDSBuf : NULL;

    b3DControl = 0;
    if ( (*pChannelFlags & SOUND_CHANNEL_USE3DCAPS) != 0 )
    {
        b3DControl = 1;
    }
    else if ( SoundDriver_Use3DCaps() && (*pChannelFlags & SOUND_CHANNEL_UNKNOWN_8000000) == 0 )
    {
        float distSquare = ((SoundDriver_curListenerPos.x - x) * (SoundDriver_curListenerPos.x - x))
            + ((SoundDriver_curListenerPos.y - y) * (SoundDriver_curListenerPos.y - y))
            + ((SoundDriver_curListenerPos.z - z) * (SoundDriver_curListenerPos.z - z));
        b3DControl = distSquare <= 0.25f;
    }

    if ( (*pChannelFlags & SOUND_CHANNEL_DISABLEHWMIXING) != 0 )
    {
        b3DControl = 0;
    }

    if ( !b3DControl )
    {
        // Port: the game's mix callback returns early, leaving these unset, when there is no camera
        // (sithSoundMixer_CalcCameraRelativeSoundMix); retail then used stack garbage. Start from the unplaced values.
        newVolume = volume;
        pan       = 0.0f;
        newPitch  = 1.0f;
        SoundDriver_CalcListeneSoundMix(x, y, z, volume, pitch, &newVolume, &pan, &newPitch, minRadius, maxRadius, envflags);
        if ( newVolume < SOUNDDRIVER_FARVOLUMETHRESHOLD
            && (*pChannelFlags & (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)) != (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING) )
        {
            return 0;
        }
    }
    else
    {
        newVolume = volume;
        pan = 0.0f;
        newPitch = pitch;
    }

    // Retail's condition, kept as written: the inner && makes it (flags & 1) == 0, so the mode is switched only for a
    // channel without SOUND_CHANNEL_PLAYING. DS3DMODE_DISABLE plays a sound at the listener (b3DControl: it is there).
    if ( SoundDriver_Use3DCaps()
        && (*pChannelFlags & (SOUND_CHANNEL_USE3DCAPS && (*pChannelFlags & SOUND_CHANNEL_UNKNOWN_8000000) == 0)) == 0
        && pDSBuf && pDSBuf->b3D )
    {
        pDSBuf->bHeadRelative = b3DControl;
        if ( pDSBuf->bPlaced )
        {
            SoundDriver_Place3DVoice(pDSBuf); // DS3D_IMMEDIATE
        }
    }

    // Note: No need to check for null pDSBuf because all underlying functions do that
    if ( (*pChannelFlags & SOUND_CHANNEL_HWMIXING) == 0 || (*pChannelFlags & SOUND_CHANNEL_DISABLEHWMIXING) != 0 )
    {
        SoundDriver_SetVolume(pDSBuf, newVolume * SoundDriver_maxVolume);
        SoundDriver_SetPan(pDSBuf, pan);
        if ( newPitch != 1.0 )
        {
            float freq = SoundDriver_GetFrequency(pDSBuf);
            freq *= newPitch;
            freq /= pitch;
            SoundDriver_SetFrequency(pDSBuf, freq);
        }
    }

    if ( (*pChannelFlags & SOUND_CHANNEL_FAR) != 0 )
    {
        if ( newVolume > SOUNDDRIVER_FARVOLUMETHRESHOLD )
        {
            *pChannelFlags |= SOUND_CHANNEL_RESTART;
        }
    }
    else if ( (*pChannelFlags & (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)) == (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)
        && (*pChannelFlags & SOUND_CHANNEL_3DSOUND) == SOUND_CHANNEL_3DSOUND
        && newVolume < SOUNDDRIVER_FARVOLUMETHRESHOLD )
    {
        SoundDriver_Stop(pDSBuf);
        if ( pDSBuf )
        {
            SoundDriver_Release(pDSBuf);
            *ppDSBuf = NULL;
        }

        *pChannelFlags &= ~(SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING);
        *pChannelFlags |= SOUND_CHANNEL_FAR;
    }

    return 1;
}

void J3DAPI SoundDriver_SetGlobals(float distanceFactor, float minDistance, float maxDistance, float rolloffFactor, float dopplerFactor)
{
    // The 3D voices' defaults (DirectSound 3D's listener and buffer settings). The game sets 10, 0.5, 2.5, 1, 1.
    SoundDriver_defaultDistanceFactor = distanceFactor / 10.0f;
    SoundDriver_minDistance = minDistance;
    SoundDriver_maxDistance = maxDistance;
    SoundDriver_defaultRolloffFactor = rolloffFactor;
    SoundDriver_defaultDopplerFactor = dopplerFactor;
}

void J3DAPI SoundDriver_CalcListeneSoundMix(float x, float y, float z, float volume, float pitch, float* newVolume, float* pan, float* newPitch, float minRadius, float maxRadius, SoundEnvFlags envflags)
{
    float curdist;
    float dx;
    float curfactor;
    float dz;
    float atan;

    if ( SoundDriver_pfCalcListenerSoundMix )
    {
        SoundSpatialInfo spatialInfo;
        memset(&spatialInfo, 0, sizeof(spatialInfo)); // Port: the three unknown fields were left unset
        spatialInfo.pos.x     = x;
        spatialInfo.pos.y     = y;
        spatialInfo.pos.z     = z;
        spatialInfo.volume    = volume;
        spatialInfo.pitch     = pitch;
        spatialInfo.minRadius = minRadius;
        spatialInfo.maxRadius = maxRadius;
        spatialInfo.flags     = envflags;

        if ( SoundDriver_curListenerPos.x >= 100000.0 )
        {
            if ( newVolume )
            {
                *newVolume = 0.0f;
            }

            if ( pan )
            {
                *pan = 0.0f;
            }
        }
        else
        {
            SoundDriver_pfCalcListenerSoundMix(&spatialInfo, newVolume, pan, newPitch);
        }
    }
    else
    {
        if ( !newVolume )
        {
            return;
        }

        *newVolume = volume;
        if ( pan )
        {
            *pan = 0.0f;
        }

        if ( newPitch )
        {
            *newPitch = 1.0f;
        }

        curdist = (float)SoundDriver_CalcDistToListener(x, y, z, SoundDriver_curListenerPos.x, SoundDriver_curListenerPos.y, SoundDriver_curListenerPos.z);
        if ( curdist > 500.0f )
        {
            *newVolume = 0.0f;
            return;
        }

        if ( curdist < 200.0f )
        {
            if ( curdist >= 30.0f )
            {
                curdist = J3DMIN(curdist, 200.0f);
                curdist -= 30.0f;
                curdist = 170.0f - curdist;
                curfactor = curdist / 170.0f;
                *newVolume = (*newVolume - 0.0f) * curfactor + 0.0f;
            }
            else
            {
                *newVolume = 1.0f;
                return;
            }
        }
        else
        {
            *newVolume = 0.0f;
        }

        if ( !pan )
        {
            return;
        }

        dx = x - SoundDriver_curListenerPos.x;
        dz = z - SoundDriver_curListenerPos.z;
        if ( dx > -0.5 && dx < 0.5 )
        {
            return;
        }

        if ( dz > -0.5 && dz < 0.5 )
        {
            *pan = dx > 0.0 ? 1.0f : -1.0f;
            return;
        }

        atan = (float)atan2(dz, dx);
        if ( errno == EDOM )
        {
            atan = 0.0f;
        }

        atan -= SoundDriver_frontOrientATan;
        *pan = (float)-sin(atan);
    }
}

double J3DAPI SoundDriver_CalcDistToListener(float x1, float y1, float z1, float x2, float y2, float z2)
{
    if ( (x2 = x2 - x1) < 0.0f )
    {
        x2 = -x2;
    }

    z2 = z2 - z1;
    if ( z2 < 0.0f )
    {
        z2 = -z2;
    }

    x1 = x2;
    if ( x2 > z2 )
    {
        x2 = z2;
    }

    x2 = x2 / 2.0f;
    return x1 + z2 - x2 + (y2 - y1 < 0.0f ? -(y2 - y1) : y2 - y1);
}

/* ---- Rendering (device threads) ----------------------------------------------------------------------------------- */

void J3DAudio_SetFocus(int bFocused)
{
    SoundDriver_bFocused = bFocused != 0;
}

// Adds one voice to pLeft and pRight (or, with pRight NULL, to the mono pLeft, stereo sources averaged): linear
// interpolation between source frames, the given gains. A voice that runs off its end stops rendering; its engine-clock
// state is unaffected. Caller holds the lock.
static void SoundDriver_MixVoice(SoundVoice* pVoice, float* pLeft, float* pRight, uint32_t frameCount, float gainLeft, float gainRight)
{
    const int16_t* pSamples = pVoice->pData->aSamples;
    size_t numFrames = pVoice->pData->numFrames;
    double pos = pVoice->renderPosition;
    double step = pVoice->renderStep;
    uint32_t i;

    for ( i = 0; i < frameCount; ++i )
    {
        size_t frame = (size_t)pos;
        size_t next;
        float frac;
        float l, r;

        if ( frame >= numFrames )
        {
            if ( !pVoice->bLoop )
            {
                pVoice->bRender = 0;
                break;
            }

            pos = fmod(pos, (double)numFrames);
            frame = (size_t)pos;
        }

        next = frame + 1;
        if ( next >= numFrames )
        {
            next = pVoice->bLoop ? 0 : frame;
        }

        frac = (float)(pos - (double)frame);
        if ( pVoice->numChannels == 1 )
        {
            float a = pSamples[frame];
            l = r = a + (pSamples[next] - a) * frac;
        }
        else
        {
            float al = pSamples[2 * frame];
            float ar = pSamples[2 * frame + 1];
            l = al + (pSamples[2 * next] - al) * frac;
            r = ar + (pSamples[2 * next + 1] - ar) * frac;
        }

        if ( pRight )
        {
            pLeft[i]  += l * gainLeft;
            pRight[i] += r * gainRight;
        }
        else
        {
            pLeft[i] += 0.5f * (l + r) * gainLeft;
        }

        pos += step;
    }

    pVoice->renderPosition = pos;
}

/* ---- The movie stream (sound_stream.h, PORT-PLAN.md P5.9) ---------------------------------------------------------- */

static uint64_t SoundDriver_HostNs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

// The play cursor. Caller holds the lock.
static uint64_t SoundDriver_StreamPlayed(void)
{
    if ( SoundDriver_bHasDevice )
    {
        return (uint64_t)SoundDriver_stream.renderPosition;
    }

    // no device: real time at the stream's rate
    return (SoundDriver_HostNs() - SoundDriver_stream.openTimeNs) * SoundDriver_stream.rate / 1000000000u;
}

void J3DSoundStream_Open(uint32_t sampleRate)
{
    pthread_mutex_lock(&SoundDriver_lock);
    SoundDriver_stream.bOpen          = 1;
    SoundDriver_stream.rate           = sampleRate ? sampleRate : 22050;
    SoundDriver_stream.written        = 0;
    SoundDriver_stream.renderPosition = 0.0;
    SoundDriver_stream.openTimeNs     = SoundDriver_HostNs();
    SoundDriver_stream.gain           = 1.0f;
    pthread_mutex_unlock(&SoundDriver_lock);
}

void J3DSoundStream_Close(void)
{
    pthread_mutex_lock(&SoundDriver_lock);
    SoundDriver_stream.bOpen = 0;
    pthread_mutex_unlock(&SoundDriver_lock);
}

uint64_t J3DSoundStream_GetPlayed(void)
{
    uint64_t played;
    pthread_mutex_lock(&SoundDriver_lock);
    played = SoundDriver_stream.bOpen ? SoundDriver_StreamPlayed() : 0;
    pthread_mutex_unlock(&SoundDriver_lock);
    return played;
}

uint64_t J3DSoundStream_GetWritten(void)
{
    uint64_t written;
    pthread_mutex_lock(&SoundDriver_lock);
    written = SoundDriver_stream.written;
    pthread_mutex_unlock(&SoundDriver_lock);
    return written;
}

void J3DSoundStream_Write(const int16_t* pFrames, uint32_t frameCount)
{
    uint64_t played;
    uint32_t i;

    pthread_mutex_lock(&SoundDriver_lock);
    if ( SoundDriver_stream.bOpen )
    {
        played = SoundDriver_StreamPlayed();
        if ( SoundDriver_stream.written < played )
        {
            SoundDriver_stream.written = played; // underrun: what was missed is gone, as with a looping buffer
        }

        for ( i = 0; i < frameCount; ++i )
        {
            size_t slot = (size_t)((SoundDriver_stream.written + i) % J3DSOUNDSTREAM_CAPACITY);
            SoundDriver_stream.aRing[2 * slot]     = pFrames[2 * i];
            SoundDriver_stream.aRing[2 * slot + 1] = pFrames[2 * i + 1];
        }

        SoundDriver_stream.written += frameCount;
    }
    pthread_mutex_unlock(&SoundDriver_lock);
}

void J3DSoundStream_SetGain(float gain)
{
    pthread_mutex_lock(&SoundDriver_lock);
    SoundDriver_stream.gain = gain;
    pthread_mutex_unlock(&SoundDriver_lock);
}

// Adds the stream to the stereo bus at the device's rate: linear interpolation between source frames, silence past
// what was written. The cursor advances either way. Caller holds the lock.
static void SoundDriver_MixStream(float* pLeft, float* pRight, uint32_t frameCount, float gain)
{
    const double step = (double)SoundDriver_stream.rate / (double)SoundDriver_outputRate;
    const uint64_t written = SoundDriver_stream.written;
    double pos = SoundDriver_stream.renderPosition;
    uint32_t i;

    for ( i = 0; i < frameCount; ++i )
    {
        uint64_t frame = (uint64_t)pos;
        if ( frame < written && gain != 0.0f )
        {
            size_t a    = (size_t)(frame % J3DSOUNDSTREAM_CAPACITY);
            size_t b    = frame + 1 < written ? (size_t)((frame + 1) % J3DSOUNDSTREAM_CAPACITY) : a;
            float  frac = (float)(pos - (double)frame);
            float  l    = SoundDriver_stream.aRing[2 * a] + (SoundDriver_stream.aRing[2 * b] - SoundDriver_stream.aRing[2 * a]) * frac;
            float  r    = SoundDriver_stream.aRing[2 * a + 1] + (SoundDriver_stream.aRing[2 * b + 1] - SoundDriver_stream.aRing[2 * a + 1]) * frac;
            pLeft[i]  += l * gain;
            pRight[i] += r * gain;
        }

        pos += step;
    }

    SoundDriver_stream.renderPosition = pos;
}

// The stereo bus: every rendering voice without a 3D slot, with DirectSound's volume and pan as gains.
static void SoundDriver_Render(float* pLeft, float* pRight, uint32_t frameCount, void* pUser)
{
    const float scale = 1.0f / 32768.0f;
    int bFocused = SoundDriver_bFocused;
    SoundVoice* pVoice;

    (void)pUser;
    memset(pLeft, 0, (size_t)frameCount * sizeof(float));
    memset(pRight, 0, (size_t)frameCount * sizeof(float));

    pthread_mutex_lock(&SoundDriver_lock);
    for ( pVoice = SoundDriver_pVoices; pVoice; pVoice = pVoice->pNext )
    {
        float gainLeft  = pVoice->gainLeft * scale;
        float gainRight = pVoice->gainRight * scale;

        if ( !pVoice->bRender || pVoice->slot >= 0 || pVoice->pData->numFrames == 0 )
        {
            continue;
        }

        if ( !bFocused && (pVoice->caps & SOUND_CHANNEL_GLOBALFOCUS) == 0 )
        {
            gainLeft = gainRight = 0.0f; // muted in the background, still moving
        }

        SoundDriver_MixVoice(pVoice, pLeft, pRight, frameCount, gainLeft, gainRight);
    }

    if ( SoundDriver_stream.bOpen )
    {
        SoundDriver_MixStream(pLeft, pRight, frameCount, bFocused ? SoundDriver_stream.gain * scale : 0.0f);
    }
    pthread_mutex_unlock(&SoundDriver_lock);
}

// One 3D slot, mono, for the device to place: the voice's volume times its distance gain, at its doppler-shifted rate.
static int SoundDriver_RenderSlot(int slot, float* pMono, uint32_t frameCount, void* pUser)
{
    const float scale = 1.0f / 32768.0f;
    int bAudible = 0;
    SoundVoice* pVoice;

    (void)pUser;
    memset(pMono, 0, (size_t)frameCount * sizeof(float));
    if ( slot < 0 || slot >= J3DAUDIO_NUM3DSLOTS )
    {
        return 0;
    }

    pthread_mutex_lock(&SoundDriver_lock);
    pVoice = SoundDriver_apSlots[slot];
    if ( pVoice && pVoice->bRender && pVoice->pData->numFrames != 0 )
    {
        float gain = pVoice->gain3D * scale;
        if ( !SoundDriver_bFocused && (pVoice->caps & SOUND_CHANNEL_GLOBALFOCUS) == 0 )
        {
            gain = 0.0f;
        }

        SoundDriver_MixVoice(pVoice, pMono, NULL, frameCount, gain, gain);
        bAudible = gain != 0.0f;
    }
    pthread_mutex_unlock(&SoundDriver_lock);
    return bAudible;
}

// Test entry points (host_audio.h).
void J3DAudio_Render(float* pOut, uint32_t frameCount)
{
    float aLeft[512], aRight[512];
    while ( frameCount )
    {
        uint32_t n = frameCount > 512 ? 512 : frameCount;
        SoundDriver_Render(aLeft, aRight, n, NULL);
        for ( uint32_t i = 0; i < n; ++i )
        {
            pOut[2 * i]     = aLeft[i];
            pOut[2 * i + 1] = aRight[i];
        }

        pOut += 2 * n;
        frameCount -= n;
    }
}

int J3DAudio_RenderSlot(int slot, float* pMono, uint32_t frameCount)
{
    return SoundDriver_RenderSlot(slot, pMono, frameCount, NULL);
}
