/*
 * The audio output device for the apple build (PORT-PLAN.md P4.1, P4.10): miniaudio's Core Audio backend, on the
 * system's default output device, as 32-bit float stereo at 48,000 Hz. miniaudio converts to the device's own rate
 * when that differs. A fixed rate keeps the mixer's arithmetic the same on every machine.
 *
 * miniaudio follows the default device itself: when headphones are plugged in or out, or the default output changes
 * in System Settings, its Core Audio backend moves the stream to the new device and keeps the format it was opened
 * with, converting if the new device's rate differs. The mixer never sees the change.
 *
 * A headless run (J3D_HEADLESS, set by --headless) opens no device: its virtual clock runs many times faster than real
 * time, and the tests that use it should be silent. J3D_NO_AUDIO=1 does the same for a windowed run.
 *
 * J3D_AUDIO_DUMP=<file.wav> writes everything the device is given to a 16-bit stereo WAV file at the device's rate,
 * and sends silence to the speakers instead, for checking the mix without listening (P4.9).
 */
#include <platform/common/host_audio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <miniaudio.h>

static ma_device J3DAudio_device;
static int J3DAudio_bOpen = 0;
static J3DAudioRenderFunc J3DAudio_pfRender = NULL;
static void* J3DAudio_pUser = NULL;
static FILE* J3DAudio_pDump = NULL;
static uint32_t J3DAudio_dumpFrames = 0;
static uint32_t J3DAudio_sampleRate = 0; // kept: ma_device_uninit zeroes the device

static void J3DAudio_WriteU32(FILE* pFile, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, pFile);
}

static void J3DAudio_WriteU16(FILE* pFile, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, pFile);
}

// The 44-byte WAV header; written with zero sizes at open and again with the real ones at close.
static void J3DAudio_WriteWavHeader(FILE* pFile, uint32_t sampleRate, uint32_t numFrames)
{
    fseek(pFile, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, pFile);
    J3DAudio_WriteU32(pFile, 36 + numFrames * 4);
    fwrite("WAVEfmt ", 1, 8, pFile);
    J3DAudio_WriteU32(pFile, 16);
    J3DAudio_WriteU16(pFile, 1);              // PCM
    J3DAudio_WriteU16(pFile, 2);              // stereo
    J3DAudio_WriteU32(pFile, sampleRate);
    J3DAudio_WriteU32(pFile, sampleRate * 4); // bytes per second
    J3DAudio_WriteU16(pFile, 4);              // block align
    J3DAudio_WriteU16(pFile, 16);             // bits per sample
    fwrite("data", 1, 4, pFile);
    J3DAudio_WriteU32(pFile, numFrames * 4);
}

static void J3DAudio_DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pDevice;
    (void)pInput;
    J3DAudio_pfRender((float*)pOutput, frameCount, J3DAudio_pUser);

    if ( J3DAudio_pDump )
    {
        // Debug output: file I/O on the device thread is acceptable here; the speakers get silence anyway.
        float* pMix = (float*)pOutput;
        for ( ma_uint32 i = 0; i < frameCount * 2; ++i )
        {
            float v = pMix[i] < -1.0f ? -1.0f : pMix[i] > 1.0f ? 1.0f : pMix[i];
            J3DAudio_WriteU16(J3DAudio_pDump, (uint16_t)(int16_t)(v * 32767.0f));
        }

        J3DAudio_dumpFrames += frameCount;
        memset(pOutput, 0, (size_t)frameCount * 2 * sizeof(float));
    }
}

static int J3DAudio_EnvSet(const char* pName)
{
    const char* pValue = getenv(pName);
    return pValue && pValue[0] && !(pValue[0] == '0' && pValue[1] == 0);
}

int J3DAudio_OpenDevice(J3DAudioRenderFunc pfRender, void* pUser, uint32_t* pSampleRate)
{
    ma_device_config config;
    const char* pDumpPath;

    if ( J3DAudio_bOpen || !pfRender || J3DAudio_EnvSet("J3D_HEADLESS") || J3DAudio_EnvSet("J3D_NO_AUDIO") )
    {
        return 0;
    }

    J3DAudio_pfRender = pfRender;
    J3DAudio_pUser    = pUser;

    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = J3DAudio_DataCallback;
    config.noPreSilencedOutputBuffer = MA_FALSE;
    config.noClip            = MA_FALSE; // miniaudio clamps the float mix to [-1, 1]

    if ( ma_device_init(NULL, &config, &J3DAudio_device) != MA_SUCCESS )
    {
        return 0;
    }

    J3DAudio_sampleRate = J3DAudio_device.sampleRate;
    pDumpPath = getenv("J3D_AUDIO_DUMP");
    if ( pDumpPath && pDumpPath[0] )
    {
        J3DAudio_pDump = fopen(pDumpPath, "wb");
        J3DAudio_dumpFrames = 0;
        if ( J3DAudio_pDump )
        {
            J3DAudio_WriteWavHeader(J3DAudio_pDump, J3DAudio_sampleRate, 0);
        }
    }

    if ( ma_device_start(&J3DAudio_device) != MA_SUCCESS )
    {
        ma_device_uninit(&J3DAudio_device);
        return 0;
    }

    J3DAudio_bOpen = 1;
    if ( pSampleRate )
    {
        *pSampleRate = J3DAudio_sampleRate;
    }

    return 1;
}

void J3DAudio_CloseDevice(void)
{
    if ( !J3DAudio_bOpen )
    {
        return;
    }

    ma_device_uninit(&J3DAudio_device); // stops the device and waits for its callback to return
    J3DAudio_bOpen = 0;

    if ( J3DAudio_pDump )
    {
        J3DAudio_WriteWavHeader(J3DAudio_pDump, J3DAudio_sampleRate, J3DAudio_dumpFrames);
        fclose(J3DAudio_pDump);
        J3DAudio_pDump = NULL;
    }
}
