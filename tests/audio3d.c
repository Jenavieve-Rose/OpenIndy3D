/*
 * audio3d: the apple build's audio device places 3D voices where the game puts them (PORT-PLAN.md decision 19).
 * Needs no game data, but an audio output device; skipped (77) without one. Nothing is heard: the mix goes to a WAV file
 * (J3D_AUDIO_DUMP) and the speakers get silence.
 *
 * A 440 Hz tone plays in 3D slot 0 for a moment at each of several positions around a listener at the origin looking
 * along the game's +y with z up, and the captured left and right energies are compared:
 *
 *   - a source at the game's +x (the camera's right) is louder on the right; at -x, on the left
 *   - ahead and behind are balanced (within 1 dB)
 *   - with the listener turned to face +x, a source at +y is on its left
 *   - "Reverse sound" (J3DAudio_SetMirror) swaps left and right
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <platform/common/host_audio.h>

static int audio3d_numFailed = 0;
static volatile int audio3d_bTone = 0;
static double audio3d_phase = 0.0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !(cond) )                                                                                                 \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++audio3d_numFailed;                                                                                       \
        }                                                                                                              \
    } while ( 0 )

static void audio3d_Bus(float* pLeft, float* pRight, uint32_t frameCount, void* pUser)
{
    (void)pUser;
    memset(pLeft, 0, frameCount * sizeof(float));
    memset(pRight, 0, frameCount * sizeof(float));
}

static int audio3d_Slot(int slot, float* pMono, uint32_t frameCount, void* pUser)
{
    (void)pUser;
    if ( slot != 0 || !audio3d_bTone )
    {
        memset(pMono, 0, frameCount * sizeof(float));
        return 0;
    }

    for ( uint32_t i = 0; i < frameCount; ++i )
    {
        pMono[i] = 0.25f * (float)sin(audio3d_phase);
        audio3d_phase += 2.0 * 3.14159265358979 * 440.0 / 48000.0;
    }

    return 1;
}

// One capture: the tone at aPos with the given listener, for 0.4 s; returns the left / right energy ratio in dB.
static double audio3d_Capture(const char* pPath, const float aPos[3], const float aFront[3], int bMirror)
{
    static const float origin[3] = { 0.0f, 0.0f, 0.0f };
    static const float up[3] = { 0.0f, 0.0f, 1.0f };
    uint32_t rate = 0;
    FILE* pFile;
    uint8_t header[44];
    double l = 0.0, r = 0.0;
    int16_t frame[2];

    setenv("J3D_AUDIO_DUMP", pPath, 1);
    if ( !J3DAudio_OpenDevice(audio3d_Bus, audio3d_Slot, NULL, &rate) )
    {
        return NAN;
    }

    J3DAudio_SetMirror(bMirror);
    J3DAudio_SetListener(origin, aFront, up);
    J3DAudio_SetSlotPosition(0, aPos);
    usleep(100000); // settle
    audio3d_bTone = 1;
    usleep(400000);
    audio3d_bTone = 0;
    usleep(100000);
    J3DAudio_CloseDevice();

    pFile = fopen(pPath, "rb");
    if ( !pFile || fread(header, 1, sizeof(header), pFile) != sizeof(header) )
    {
        if ( pFile )
        {
            fclose(pFile);
        }

        return NAN;
    }

    while ( fread(frame, sizeof(int16_t), 2, pFile) == 2 )
    {
        l += (double)frame[0] * frame[0];
        r += (double)frame[1] * frame[1];
    }

    fclose(pFile);
    remove(pPath);
    if ( l + r < 1.0 )
    {
        return NAN; // nothing captured
    }

    return 10.0 * log10((l + 1.0) / (r + 1.0));
}

int main(int argc, char** argv)
{
    static const float ahead[3] = { 0.0f, 1.0f, 0.0f };
    static const float right[3] = { 2.0f, 0.0f, 0.0f };
    static const float left[3] = { -2.0f, 0.0f, 0.0f };
    static const float front[3] = { 0.0f, 2.0f, 0.0f };
    static const float behind[3] = { 0.0f, -2.0f, 0.0f };
    static const float facingX[3] = { 1.0f, 0.0f, 0.0f };
    const char* pPath = argc > 1 ? argv[1] : "audio3d.wav";
    double b;

    unsetenv("J3D_HEADLESS");
    unsetenv("J3D_NO_AUDIO");

    b = audio3d_Capture(pPath, right, ahead, 0);
    if ( isnan(b) )
    {
        printf("audio3d: no audio output device, skipped\n");
        return 77;
    }

    printf("right %+.1f dB (left/right)\n", b);
    CHECK(b < -3.0);

    b = audio3d_Capture(pPath, left, ahead, 0);
    printf("left %+.1f dB\n", b);
    CHECK(b > 3.0);

    b = audio3d_Capture(pPath, front, ahead, 0);
    printf("ahead %+.1f dB\n", b);
    CHECK(fabs(b) < 1.0);

    b = audio3d_Capture(pPath, behind, ahead, 0);
    printf("behind %+.1f dB\n", b);
    CHECK(fabs(b) < 1.0);

    b = audio3d_Capture(pPath, front, facingX, 0); // facing +x, +y is to the left
    printf("turned, +y %+.1f dB\n", b);
    CHECK(b > 3.0);

    b = audio3d_Capture(pPath, right, ahead, 1);
    printf("mirrored right %+.1f dB\n", b);
    CHECK(b > 3.0);

    if ( audio3d_numFailed )
    {
        fprintf(stderr, "%d check(s) failed\n", audio3d_numFailed);
        return 1;
    }

    printf("audio3d: all checks passed\n");
    return 0;
}
