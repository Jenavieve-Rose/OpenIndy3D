/*
 * sounddriver: the sound driver (platform/common/SoundDriver_mix.c, PORT-PLAN.md P4.3 to P4.6). Needs no game data and no
 * audio device: it runs on the virtual clock, and renders through J3DAudio_Render as a device would.
 *
 *   - the driver opens with no device, reports no 3D hardware, and keeps voices on the engine clock: a one-shot plays
 *     for its length and stops with its cursor back at 0; a loop wraps; Stop holds the position and Play resumes it;
 *     a frequency change applies from the moment it is made
 *   - retail's conversion tables: GetDecibelVolume, GetIntPan, GetVolumeFromDecibels give retail's values
 *   - the mix: DirectSound's volume and pan as gains, 8-bit samples centred, a one-shot stops rendering at its end,
 *     a loop keeps going, a background window mutes all but global-focus voices
 *   - with 3D sound on: retail's channel flags for mono, stereo and 2D sounds; mono positioned sounds get a 3D slot;
 *     DirectSound 3D's inverse distance law between the minimum and maximum distance; doppler from the source's
 *     velocity and from the listener's, derived from its movement on the engine clock; a head-relative voice is
 *     unattenuated
 *   - a compressed sound (retail's WVSM codec, the one Sound.c uses) decompresses to what was compressed, within the
 *     codec's error
 *   - AudioLib_UncompressBlock, the ADPCM decoder that retail wrote in x86 assembly and the port translated to C
 *     (P4.8), decodes bit for bit what an independent decoder written from the encoder's arithmetic decodes, and ends
 *     in the state the encoder ended in, over silence, tones, noise and full-scale steps that force escapes, mono and
 *     stereo. The encoder tracks the decoder's reconstruction sample by sample, so it is an exact oracle.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

#include <platform/common/host_audio.h>
#include <platform/common/host_clock.h>
#include <sound/AudioLib.h>
#include <sound/Driver.h>
#include <rdroid/types.h>

static int sound_numFailed = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !(cond) )                                                                                                 \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++sound_numFailed;                                                                                         \
        }                                                                                                              \
    } while ( 0 )

#define CLOSE_TO(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

static int J3DAPI sound_LogError(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

// 100 ms of 16-bit mono at 22,050 Hz, all one value.
#define SOUND_RATE   22050
#define SOUND_FRAMES 2205
static int16_t sound_aDC[SOUND_FRAMES];

static SoundVoice* sound_Play(tSoundChannelFlag flags, const void* pData, size_t size, int bits)
{
    tSoundChannelFlag f = flags;
    return SoundDriver_CreateAndPlay(SOUND_RATE, bits, 1, (const uint8_t*)pData, size, &f, NULL);
}

static void sound_TestClock(void)
{
    SoundVoice* pVoice;

    // One-shot: 100 ms long.
    pVoice = sound_Play(SOUND_CHANNEL_PLAYING, sound_aDC, sizeof(sound_aDC), 16);
    CHECK(pVoice != NULL);
    CHECK(SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_PLAYING);
    CHECK((SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_LOOP) == 0);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 0);
    J3DHost_AdvanceClock(50);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 1102 * 2); // 50 ms x 22.05 frames/ms, in bytes
    J3DHost_AdvanceClock(60);
    CHECK((SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_PLAYING) == 0);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 0);

    // Play again from the start; Stop holds the position; Play resumes from it.
    SoundDriver_Play(pVoice, 0);
    J3DHost_AdvanceClock(20);
    SoundDriver_Stop(pVoice);
    CHECK((SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_PLAYING) == 0);
    J3DHost_AdvanceClock(1000);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 441 * 2);
    SoundDriver_Play(pVoice, 0);
    J3DHost_AdvanceClock(20);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 882 * 2);

    // Twice the frequency: the next 20 ms cover 40 ms of sound.
    SoundDriver_SetFrequency(pVoice, 2.0f * SOUND_RATE);
    CHECK(SoundDriver_GetFrequency(pVoice) == 2.0f * SOUND_RATE);
    J3DHost_AdvanceClock(20);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 1764 * 2);
    SoundDriver_SetFrequency(pVoice, 50.0f); // below DirectSound's minimum: refused, unchanged
    CHECK(SoundDriver_GetFrequency(pVoice) == 2.0f * SOUND_RATE);
    SoundDriver_SetFrequency(pVoice, 0.0f); // the buffer's own rate
    CHECK(SoundDriver_GetFrequency(pVoice) == (float)SOUND_RATE);
    SoundDriver_Release(pVoice);

    // Loop: 250 ms into a 100 ms loop is 50 ms in.
    pVoice = sound_Play(SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP, sound_aDC, sizeof(sound_aDC), 16);
    CHECK((SoundDriver_GetStatusAndCaps(pVoice) & (SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP)) == (SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP));
    J3DHost_AdvanceClock(250);
    CHECK(SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_PLAYING);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 1102 * 2);
    SoundDriver_Release(pVoice);

    // Created without the playing flag: silent and stopped until Play.
    pVoice = sound_Play(0, sound_aDC, sizeof(sound_aDC), 16);
    J3DHost_AdvanceClock(50);
    CHECK((SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_PLAYING) == 0);
    CHECK(SoundDriver_GetCurrentPosition(pVoice) == 0);
    SoundDriver_Release(pVoice);

    // Formats DirectSound would not take are refused.
    {
        tSoundChannelFlag f = SOUND_CHANNEL_PLAYING;
        CHECK(SoundDriver_CreateAndPlay(SOUND_RATE, 12, 1, (const uint8_t*)sound_aDC, sizeof(sound_aDC), &f, NULL) == NULL);
        CHECK(SoundDriver_CreateAndPlay(SOUND_RATE, 16, 3, (const uint8_t*)sound_aDC, sizeof(sound_aDC), &f, NULL) == NULL);
    }
}

static void sound_TestTables(void)
{
    // Retail's values (Driver.c's table and InitPanTable).
    CHECK(SoundDriver_GetDecibelVolume(1.0f) == -10);
    CHECK(SoundDriver_GetDecibelVolume(0.0f) == -9640);
    CHECK(SoundDriver_GetDecibelVolume(-1.0f) == -9640);
    CHECK(SoundDriver_GetDecibelVolume(2.0f) == 0); // past the table: full volume, as retail
    CHECK(SoundDriver_GetIntPan(0.0f) == 10);
    CHECK(SoundDriver_GetIntPan(-1.0f) == -4810);
    CHECK(SoundDriver_GetIntPan(0.99f) == 3850);
    CHECK(SoundDriver_GetIntPan(1.0f) == 0); // retail: hard right is index 256, past the table, so centred (docs/PORTING.md)
    CHECK(CLOSE_TO(SoundDriver_GetVolumeFromDecibels(-10), 1.0, 1e-6));
    CHECK(CLOSE_TO(SoundDriver_GetVolumeFromDecibels(-9640), 0.0, 1e-6));
    CHECK(CLOSE_TO(SoundDriver_GetPanFromTable(64), 0.5, 1e-6));
}

// Renders n frames; returns the first frame's left and right.
static void sound_Render(float* pBuf, uint32_t n)
{
    J3DAudio_Render(pBuf, n);
}

static void sound_TestMix(void)
{
    static float aOut[2 * 4800];
    static uint8_t aDC8[SOUND_FRAMES];
    SoundVoice* pVoice;
    double g = pow(10.0, -10.0 / 2000.0); // full volume is -10 (hundredths of a dB) in retail's table
    double half = 16384.0 / 32768.0;

    // Centred: GetIntPan(0) is 10, a tenth of a dB off the left.
    pVoice = sound_Play(SOUND_CHANNEL_PLAYING, sound_aDC, sizeof(sound_aDC), 16);
    SoundDriver_SetVolume(pVoice, 1.0f);
    SoundDriver_SetPan(pVoice, 0.0f);
    sound_Render(aOut, 100);
    CHECK(CLOSE_TO(aOut[0], half * g * g, 1e-4));
    CHECK(CLOSE_TO(aOut[1], half * g, 1e-4));

    // Hard left: the right side 48.1 dB down.
    SoundDriver_SetPan(pVoice, -1.0f);
    sound_Render(aOut, 100);
    CHECK(CLOSE_TO(aOut[0], half * g, 1e-4));
    CHECK(CLOSE_TO(aOut[1], half * g * pow(10.0, -4810.0 / 2000.0), 1e-4));

    // Half volume: GetDecibelVolume(0.5) is the table's entry 127.
    SoundDriver_SetPan(pVoice, -1.0f);
    SoundDriver_SetVolume(pVoice, 0.5f);
    sound_Render(aOut, 100);
    CHECK(CLOSE_TO(aOut[0], half * pow(10.0, (double)SoundDriver_GetDecibelVolume(0.5f) / 2000.0), 1e-4));

    // The one-shot is 100 ms, 4,800 output frames at 48 kHz; 300 are gone. It ends inside the next 4,800 and the
    // tail is silent; after that nothing renders.
    sound_Render(aOut, 4800);
    CHECK(aOut[0] != 0.0f);
    CHECK(aOut[2 * 4799] == 0.0f && aOut[2 * 4799 + 1] == 0.0f);
    sound_Render(aOut, 100);
    CHECK(aOut[0] == 0.0f);
    SoundDriver_Release(pVoice);

    // 8-bit unsigned: 192 is +64 from the centre, half of 16-bit's 16384.
    memset(aDC8, 192, sizeof(aDC8));
    pVoice = sound_Play(SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP, aDC8, sizeof(aDC8), 8);
    SoundDriver_SetVolume(pVoice, 1.0f);
    SoundDriver_SetPan(pVoice, -1.0f);
    sound_Render(aOut, 4800);
    sound_Render(aOut, 4800); // a loop: still sounding after twice its length
    CHECK(CLOSE_TO(aOut[2 * 4799], half * g, 1e-4));

    // A background window mutes it, but it keeps its place.
    J3DAudio_SetFocus(0);
    sound_Render(aOut, 100);
    CHECK(aOut[0] == 0.0f);
    J3DAudio_SetFocus(1);
    sound_Render(aOut, 100);
    CHECK(aOut[0] != 0.0f);
    SoundDriver_Release(pVoice);

    // Nothing left to mix.
    sound_Render(aOut, 100);
    CHECK(aOut[0] == 0.0f && aOut[1] == 0.0f);
}

static void sound_TestCompressed(void)
{
    enum { N = 4410 };
    static int16_t aSine[N];
    static _Alignas(uint32_t) uint8_t aPacked[sizeof(uint32_t) + N * 2];
    static float aOut[2 * 9600];
    tAudioCompressorState state;
    tAudioCompressedData* pCompressed = (tAudioCompressedData*)aPacked;
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_COMPRESSED;
    SoundVoice* pVoice;
    int i;
    double maxErr = 0.0;

    for ( i = 0; i < N; ++i )
    {
        aSine[i] = (int16_t)(8000.0 * sin(2.0 * 3.14159265358979 * 441.0 * i / SOUND_RATE));
    }

    pCompressed->uncompressedSize = sizeof(aSine);
    AudioLib_ResetCompressor(&state);
    AudioLib_Compress(&state, pCompressed->compressedData, (const uint8_t*)aSine, (int)sizeof(aSine), 1);

    pVoice = SoundDriver_CreateAndPlay(SOUND_RATE, 16, 1, aPacked, sizeof(aPacked), &flags, NULL);
    CHECK(pVoice != NULL);
    SoundDriver_SetVolume(pVoice, 2.0f); // past the table: 0 dB, gain 1
    SoundDriver_SetPan(pVoice, -1.0f);
    SoundDriver_SetFrequency(pVoice, 48000.0f); // one source frame per output frame: no interpolation
    sound_Render(aOut, N);
    for ( i = 0; i < N; ++i )
    {
        double err = fabs(aOut[2 * i] * 32768.0 - aSine[i]);
        maxErr = err > maxErr ? err : maxErr;
    }

    printf("compressed: largest error %.0f of 8000\n", maxErr);
    CHECK(maxErr < 800.0); // 4-bit ADPCM on a smooth tone
    SoundDriver_Release(pVoice);
}

/* ---- The ADPCM block (P4.8) ---------------------------------------------------------------------------------------- */

extern const uint16_t AudioLib_aStepTable[89];
extern const uint8_t AudioLib_aStepBits[89];
extern const int8_t* AudioLib_aIndexTableTable[8];

typedef struct
{
    const uint8_t* p;
    uint32_t acc;
    int nAcc;
} RefBits;

static unsigned int ref_Read(RefBits* pBits, int n)
{
    while ( pBits->nAcc < n )
    {
        pBits->acc = (pBits->acc << 8) | *pBits->p++;
        pBits->nAcc += 8;
    }

    pBits->nAcc -= n;
    return (pBits->acc >> pBits->nAcc) & ((1u << n) - 1);
}

// Written from AudioLib_CompressBlock's arithmetic, not from the decoder: the delta is the sum of the step's halvings
// the code's bits select, plus the last halving when the code is not 0; all-ones is an escape to a raw 16-bit sample.
static void ref_Decode(const uint8_t* pIn, int16_t* pOut, int numSamples, int numChannels, int8_t* aIndex, int16_t* aPred)
{
    RefBits bits = { pIn, 0, 0 };
    for ( int ch = 0; ch < numChannels; ++ch )
    {
        int index = aIndex[ch];
        int pred = aPred[ch];
        for ( int i = 0; i < numSamples; ++i )
        {
            int numBits = AudioLib_aStepBits[index];
            unsigned int word;
            if ( numBits < 2 ) // the table holds 4 to 7; this keeps the shifts below defined for the analyzer
            {
                return;
            }

            word = ref_Read(&bits, numBits);
            unsigned int sign = word >> (numBits - 1);
            unsigned int code = word & ((1u << (numBits - 1)) - 1);
            if ( code == (1u << (numBits - 1)) - 1 )
            {
                pred = (int16_t)ref_Read(&bits, 16);
            }
            else
            {
                int step = AudioLib_aStepTable[index];
                int delta = 0;
                for ( int b = numBits - 2; b >= 0; --b, step >>= 1 )
                {
                    if ( code & (1u << b) )
                    {
                        delta += step;
                    }
                }

                if ( code )
                {
                    delta += step;
                }

                pred += sign ? -delta : delta;
                pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
            }

            pOut[i * numChannels + ch] = (int16_t)pred;
            index += AudioLib_aIndexTableTable[numBits][code];
            index = index < 0 ? 0 : index > 88 ? 88 : index;
        }

        aIndex[ch] = (int8_t)index;
        aPred[ch] = (int16_t)pred;
    }
}

static uint32_t sound_seed = 12345;
static int16_t sound_Noise(int amp)
{
    sound_seed = sound_seed * 1664525u + 1013904223u;
    return (int16_t)((int)(sound_seed >> 16) % (2 * amp + 1) - amp);
}

static void sound_TestAdpcm(void)
{
    enum { N = 6000 };
    static int16_t aIn[2 * N], aOut[2 * N], aRef[2 * N];
    static uint8_t aPacked[8 * N + 64];
    int numSignals = 0, numEscapes = 0;

    for ( int numChannels = 1; numChannels <= 2; ++numChannels )
    {
        for ( int kind = 0; kind < 6; ++kind )
        {
            tAudioCompressorState enc, dec;
            int8_t aIndex[2];
            int16_t aPred[2];
            const uint8_t* pBlock;
            int size;

            for ( int i = 0; i < N * numChannels; ++i )
            {
                double t = (double)(i / numChannels);
                switch ( kind )
                {
                    case 0: aIn[i] = 0; break;
                    case 1: aIn[i] = (int16_t)(3000.0 * sin(t * 0.05)); break;
                    case 2: aIn[i] = (int16_t)(32000.0 * sin(t * (0.01 + 0.2 * (i % numChannels)))); break;
                    case 3: aIn[i] = sound_Noise(2000); break;
                    case 4: aIn[i] = sound_Noise(32767); break;
                    default: aIn[i] = ((i / numChannels) / 37) & 1 ? 32767 : -32768; break; // full-scale steps
                }
            }

            // numChannels + 2 selects the ADPCM block format (AudioLib_Compress); 1 or 2 would select WVSM.
            AudioLib_ResetCompressor(&enc);
            size = AudioLib_Compress(&enc, aPacked, (const uint8_t*)aIn, (int)(N * numChannels * sizeof(int16_t)), (unsigned int)numChannels + 2);
            CHECK(size > 0 && size <= (int)sizeof(aPacked));

            AudioLib_ResetCompressor(&dec);
            memset(aOut, 0x55, sizeof(aOut));
            AudioLib_Uncompress(&dec, (uint8_t*)aOut, aPacked, (unsigned int)(N * numChannels * sizeof(int16_t)));

            aIndex[0] = aIndex[1] = 0;
            aPred[0] = aPred[1] = 0;
            pBlock = aPacked + 3 * numChannels; // the header holds the (reset) start state
            ref_Decode(pBlock, aRef, N, numChannels, aIndex, aPred);

            CHECK(memcmp(aOut, aRef, N * numChannels * sizeof(int16_t)) == 0);
            for ( int ch = 0; ch < numChannels; ++ch )
            {
                CHECK(dec.aStepIndex[ch] == enc.aStepIndex[ch] && dec.aPrevSample[ch] == enc.aPrevSample[ch]);
                CHECK(aIndex[ch] == enc.aStepIndex[ch] && aPred[ch] == enc.aPrevSample[ch]);
            }

            for ( int i = 0; i < N * numChannels; ++i )
            {
                numEscapes += aOut[i] == aIn[i] && kind == 5;
            }

            ++numSignals;
        }
    }

    printf("adpcm: %d signals decoded bit for bit, %d samples restored exactly from escapes or steps\n", numSignals, numEscapes);
    CHECK(numEscapes > 0);
}

/* ---- 3D (decision 19) -------------------------------------------------------------------------------------------- */

// A ramp, so the rendered value tells the read position: sample i is 10 i.
static int16_t sound_aRamp[SOUND_FRAMES];

// The slot's rendered rate, in source frames per output frame, and its gain, from two rendered ramp values.
static void sound_MeasureSlot(int slot, double* pStep, double* pGain, double knownGain)
{
    static float aMono[256];
    J3DAudio_RenderSlot(slot, aMono, 256);
    // out[k] = 10 (p0 + k step) gain / 32768
    if ( pStep )
    {
        *pStep = (aMono[200] - aMono[0]) * 32768.0 / (10.0 * 200.0 * knownGain);
    }

    if ( pGain )
    {
        *pGain = (aMono[1] - aMono[0]) * 32768.0 / (10.0 * (double)SOUND_RATE / 48000.0); // at the plain rate
    }
}

static void sound_Test3D(void)
{
    const rdVector3 origin = { 0.0f, 0.0f, 0.0f };
    // Sound_Update passes the camera's front and top negated (Reverse sound off): the camera looks along +y, z up.
    const rdVector3 front = { 0.0f, -1.0f, 0.0f };
    const rdVector3 top = { 0.0f, 0.0f, -1.0f };
    const double plainStep = (double)SOUND_RATE / 48000.0;
    tSoundChannelFlag flags;
    SoundVoice* pVoice;
    SoundVoice* pStereo;
    SoundVoice* p2D;
    double step, gain;
    static int16_t aStereo[2 * 100];
    static float aOut[2 * 64];

    for ( int i = 0; i < SOUND_FRAMES; ++i )
    {
        sound_aRamp[i] = (int16_t)(10 * i);
    }

    SoundDriver_SetGlobals(10.0f, 0.5f, 2.5f, 1.0f, 1.0f); // the game's values (sithSound_Startup)
    SoundDriver_SetListenerPosition(&origin, NULL, &top, &front, NULL);
    SoundDriver_ListenerCommitDeferred();

    // Retail's flags: a mono positioned sound is a 3D, hardware-mixed buffer; stereo cannot be 3D; a 2D sound
    // (Sound_Play adds USE3DCAPS) is neither.
    flags = SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP;
    pVoice = SoundDriver_CreateAndPlay(SOUND_RATE, 16, 1, (const uint8_t*)sound_aRamp, sizeof(sound_aRamp), &flags, NULL);
    CHECK(pVoice != NULL);
    CHECK((flags & (SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_HWMIXING)) == (SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_HWMIXING));
    CHECK(SoundDriver_GetStatusAndCaps(pVoice) & SOUND_CHANNEL_3DSOUND);

    flags = SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP;
    pStereo = SoundDriver_CreateAndPlay(SOUND_RATE, 16, 2, (const uint8_t*)aStereo, sizeof(aStereo), &flags, NULL);
    CHECK((flags & (SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_UNKNOWN_8000000)) == (SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_UNKNOWN_8000000));
    CHECK((flags & SOUND_CHANNEL_HWMIXING) == 0); // Port: the game's mix places it (PORTING.md)

    flags = SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_USE3DCAPS;
    p2D = SoundDriver_CreateAndPlay(SOUND_RATE, 16, 1, (const uint8_t*)sound_aDC, sizeof(sound_aDC), &flags, NULL);
    CHECK((flags & (SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_HWMIXING)) == 0);
    CHECK((SoundDriver_GetStatusAndCaps(p2D) & SOUND_CHANNEL_3DSOUND) == 0);

    // The 3D voice is in slot 0, not on the stereo bus; the 2D voice is on the bus.
    SoundDriver_SetVolume(p2D, 2.0f);  // 0 dB
    SoundDriver_SetPan(p2D, 0.5f);
    SoundDriver_Stop(pStereo);
    J3DAudio_Render(aOut, 64);
    CHECK(aOut[0] > 0.0f && aOut[1] > 0.0f);   // the 2D voice
    SoundDriver_Stop(p2D);
    J3DAudio_Render(aOut, 64);
    CHECK(aOut[0] == 0.0f && aOut[1] == 0.0f);  // nothing else on the bus

    // Distance: 2 units away with minimum 0.5 and maximum 2.5 is 0.5 / (0.5 + 1.5) = 0.25; past the maximum the gain
    // holds at 0.5 / (0.5 + 2) = 0.2.
    SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.5f, 0);
    sound_MeasureSlot(0, NULL, &gain, 1.0);
    CHECK(CLOSE_TO(gain, 0.25, 0.01));
    SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.5f, 0);
    SoundDriver_ListenerCommitDeferred(); // after the first placement, changes wait for the commit
    sound_MeasureSlot(0, NULL, &gain, 1.0);
    CHECK(CLOSE_TO(gain, 0.2, 0.01));
    SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.5f, 0);
    SoundDriver_ListenerCommitDeferred();
    sound_MeasureSlot(0, &step, &gain, 1.0);
    CHECK(CLOSE_TO(gain, 1.0, 0.01));   // inside the minimum distance
    CHECK(CLOSE_TO(step, plainStep, 1e-3));

    // Doppler, source: moving at a tenth of the speed of sound toward the listener (the game's distance factor is
    // 10 / 10 = 1 metre per unit) raises the rate by c / (c - c/10) = 1/0.9.
    SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 2.0f, 0.0f, 0.0f, -34.33f, 0.0f, 0.5f, 2.5f, 0);
    SoundDriver_ListenerCommitDeferred();
    sound_MeasureSlot(0, &step, NULL, 0.25);
    CHECK(CLOSE_TO(step / plainStep, 1.0 / 0.9, 2e-3));
    // ... and moving away lowers it: c / (c + c/10).
    SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 2.0f, 0.0f, 0.0f, 34.33f, 0.0f, 0.5f, 2.5f, 0);
    SoundDriver_ListenerCommitDeferred();
    sound_MeasureSlot(0, &step, NULL, 0.25);
    CHECK(CLOSE_TO(step / plainStep, 1.0 / 1.1, 2e-3));

    // Doppler, listener: the game passes no listener velocity; it is derived from the camera's movement. Moving 0.5
    // units toward the source in 100 ms is 5 units/s, smoothed to half on the first estimate: (c + 2.5) / c.
    SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.5f, 0);
    SoundDriver_ListenerCommitDeferred();
    J3DHost_AdvanceClock(100);
    {
        const rdVector3 moved = { 0.0f, 0.5f, 0.0f };
        SoundDriver_SetListenerPosition(&moved, NULL, &top, &front, NULL);
        SoundDriver_ListenerCommitDeferred();
    }
    sound_MeasureSlot(0, &step, NULL, 0.2);
    CHECK(CLOSE_TO(step / plainStep, (343.3 + 2.5) / 343.3, 2e-3));
    // A jump faster than half the speed of sound is a cut: it counts as standing still (the estimate halves).
    J3DHost_AdvanceClock(10);
    {
        const rdVector3 jumped = { 0.0f, 10.0f, 0.0f };
        SoundDriver_SetListenerPosition(&jumped, NULL, &top, &front, NULL);
        SoundDriver_ListenerCommitDeferred();
    }
    sound_MeasureSlot(0, &step, NULL, 0.2);
    CHECK(CLOSE_TO(step / plainStep, (343.3 + 1.25) / 343.3, 2e-3));

    // Head-relative (DS3DMODE_DISABLE): Update3DSound switches a 3D buffer at the listener's position to it, for a
    // channel without SOUND_CHANNEL_PLAYING (retail's condition). It is then heard unattenuated.
    SoundDriver_SetListenerPosition(&origin, NULL, &top, &front, NULL);
    SoundDriver_ListenerCommitDeferred();
    {
        tSoundChannelFlag chFlags = SOUND_CHANNEL_3DSOUND | SOUND_CHANNEL_HWMIXING | SOUND_CHANNEL_USE3DCAPS;
        SoundDriver_Update3DSound(&pVoice, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, &chFlags, 0.5f, 2.5f, 0);
        SoundDriver_SetPosAndVelocity(pVoice, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.5f, 0);
        SoundDriver_ListenerCommitDeferred();
        sound_MeasureSlot(0, NULL, &gain, 1.0);
        CHECK(CLOSE_TO(gain, 1.0, 0.01));
    }

    SoundDriver_Release(pVoice);
    SoundDriver_Release(pStereo);
    SoundDriver_Release(p2D);
    CHECK(J3DAudio_RenderSlot(0, (float*)aOut, 64) == 0); // the slot is free again
}

int main(void)
{
    for ( int i = 0; i < SOUND_FRAMES; ++i )
    {
        sound_aDC[i] = 16384;
    }

    J3DHost_UseVirtualClock();

    // 3D sound off (retail's default "Sound 3D"): the game's mix places everything on the stereo bus.
    CHECK(SoundDriver_Open(1, 0, NULL, NULL, NULL, NULL, sound_LogError) == 1);
    CHECK(SoundDriver_GetDSound() != NULL);
    CHECK(SoundDriver_Has3DHW() == 1); // always: the channel flags must not depend on a device being open
    CHECK(SoundDriver_Use3DCaps() == 0);

    sound_TestTables();
    sound_TestClock();
    sound_TestMix();
    sound_TestCompressed();
    sound_TestAdpcm();

    SoundDriver_Close();
    CHECK(SoundDriver_GetDSound() == NULL);

    // 3D sound on.
    CHECK(SoundDriver_Open(0, 0, NULL, NULL, NULL, NULL, sound_LogError) == 1);
    CHECK(SoundDriver_Use3DCaps() == 1);
    sound_Test3D();
    SoundDriver_Close();

    if ( sound_numFailed )
    {
        fprintf(stderr, "%d check(s) failed\n", sound_numFailed);
        return 1;
    }

    printf("sounddriver: all checks passed\n");
    return 0;
}
