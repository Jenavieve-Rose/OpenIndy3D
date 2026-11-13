/*
 * The audio output device for the apple build (PORT-PLAN.md Phase 4, decision 19): AVAudioEngine on the system's default
 * output, with an AVAudioEnvironmentNode for 3D sound.
 *
 *   stereo bus (AVAudioSourceNode, 48 kHz stereo) ------------------------------+
 *   3D slot 0..31 (AVAudioSourceNode, 48 kHz mono) -> AVAudioEnvironmentNode ---+-> submix -> main mixer -> output
 *
 * The driver (SoundDriver_mix.c) renders every signal; the environment node only places the slots around the listener
 * with the chosen rendering algorithm (HRTF on headphones). Distance attenuation and doppler are the driver's
 * (DirectSound 3D's model), so the environment's own attenuation is switched off.
 *
 * Coordinates: the game's are x right, y forward, z up; AVAudioEngine's are x right, y up, z towards the listener (-z
 * forward). A point (x, y, z) is placed at (x, z, -y), a rotation, so handedness is kept. "Reverse sound" negates x.
 *
 * Settings: "Sound 3D Rendering" (0, the default: Apple's best rendering for headphones; 1 HRTF high quality; 2 HRTF;
 * 3 Apple's rendering for a Mac's built-in speakers) and "Sound Head Tracking" (1: on macOS 15 and later, with
 * headphones that track the head, the listener turns with the player's head; default 0). 0 to 2 are made for
 * headphones and work on any output; 3 places sounds only slightly apart (about 1 dB measured), as Apple's speaker
 * rendering does.
 *
 * When the output changes (headphones in or out, a new default device) AVAudioEngine stops and posts
 * AVAudioEngineConfigurationChangeNotification; the device starts it again with the same graph.
 *
 * A headless run (J3D_HEADLESS) or J3D_NO_AUDIO=1 opens no device. J3D_AUDIO_DUMP=<file.wav> writes the submix to a
 * 16-bit stereo 48 kHz WAV file and silences the speakers, for checking the mix without listening.
 */
#import <AVFAudio/AVFAudio.h>
#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/common/host_audio.h>

int wuRegistry_GetIntEx(const char* pKey, int defaultValue); // w32util/wuRegistry.h, without its Windows types

#define J3DAUDIO_RATE 48000.0

static AVAudioEngine* J3DAudio_engine = nil;
static AVAudioEnvironmentNode* J3DAudio_environment = nil;
static AVAudioMixerNode* J3DAudio_submix = nil;
static AVAudioSourceNode* J3DAudio_bus = nil;
static AVAudioSourceNode* J3DAudio_aSlots[J3DAUDIO_NUM3DSLOTS];
static id J3DAudio_configObserver = nil;
static dispatch_queue_t J3DAudio_queue = nil;
static int J3DAudio_bMirror = 0;

static FILE* J3DAudio_pDump = NULL;
static uint32_t J3DAudio_dumpFrames = 0;

static int J3DAudio_EnvSet(const char* pName)
{
    const char* pValue = getenv(pName);
    return pValue && pValue[0] && !(pValue[0] == '0' && pValue[1] == 0);
}

static AVAudio3DPoint J3DAudio_ToAV(const float a[3])
{
    float x = J3DAudio_bMirror ? -a[0] : a[0];
    return AVAudioMake3DPoint(x, a[2], -a[1]);
}

static AVAudio3DVector J3DAudio_ToAVVector(const float a[3])
{
    float x = J3DAudio_bMirror ? -a[0] : a[0];
    return AVAudioMake3DVector(x, a[2], -a[1]);
}

// AVAudioEngine's connect and tap calls gained error-returning forms in macOS 27, which deprecates the old ones; the
// deployment target is macOS 13, so the old ones remain for earlier systems.
static BOOL J3DAudio_Connect(AVAudioNode* from, AVAudioNode* to, AVAudioNodeBus fromBus, AVAudioNodeBus toBus, AVAudioFormat* format)
{
    if ( @available(macOS 27.0, *) )
    {
        return [J3DAudio_engine connect:from to:to fromBus:fromBus toBus:toBus format:format error:nil];
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [J3DAudio_engine connect:from to:to fromBus:fromBus toBus:toBus format:format];
#pragma clang diagnostic pop
    return YES;
}

static BOOL J3DAudio_InstallTap(AVAudioNode* node, AVAudioFormat* format, AVAudioNodeTapBlock block)
{
    if ( @available(macOS 27.0, *) )
    {
        return [node installTapOnBus:0 bufferSize:4096 format:format error:nil block:block];
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [node installTapOnBus:0 bufferSize:4096 format:format block:block];
#pragma clang diagnostic pop
    return YES;
}

/* ---- WAV dump ----------------------------------------------------------------------------------------------------- */

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
static void J3DAudio_WriteWavHeader(FILE* pFile, uint32_t numFrames)
{
    const uint32_t rate = (uint32_t)J3DAUDIO_RATE;
    fseek(pFile, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, pFile);
    J3DAudio_WriteU32(pFile, 36 + numFrames * 4);
    fwrite("WAVEfmt ", 1, 8, pFile);
    J3DAudio_WriteU32(pFile, 16);
    J3DAudio_WriteU16(pFile, 1);       // PCM
    J3DAudio_WriteU16(pFile, 2);       // stereo
    J3DAudio_WriteU32(pFile, rate);
    J3DAudio_WriteU32(pFile, rate * 4);
    J3DAudio_WriteU16(pFile, 4);
    J3DAudio_WriteU16(pFile, 16);
    fwrite("data", 1, 4, pFile);
    J3DAudio_WriteU32(pFile, numFrames * 4);
    fseek(pFile, 0, SEEK_END);
}

static void J3DAudio_StartDump(const char* pPath, AVAudioFormat* format)
{
    J3DAudio_pDump = fopen(pPath, "wb");
    if ( !J3DAudio_pDump )
    {
        return;
    }

    J3DAudio_dumpFrames = 0;
    J3DAudio_WriteWavHeader(J3DAudio_pDump, 0);
    J3DAudio_engine.mainMixerNode.outputVolume = 0.0f; // the tap is on the submix, before the main mixer

    // The tap's blocks arrive in order on one AVAudioEngine thread.
    J3DAudio_InstallTap(J3DAudio_submix, format, ^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
        (void)when;
        const float* pLeft = buffer.floatChannelData[0];
        const float* pRight = buffer.floatChannelData[1];
        for ( AVAudioFrameCount i = 0; i < buffer.frameLength; ++i )
        {
            float l = pLeft[i] < -1.0f ? -1.0f : pLeft[i] > 1.0f ? 1.0f : pLeft[i];
            float r = pRight[i] < -1.0f ? -1.0f : pRight[i] > 1.0f ? 1.0f : pRight[i];
            J3DAudio_WriteU16(J3DAudio_pDump, (uint16_t)(int16_t)(l * 32767.0f));
            J3DAudio_WriteU16(J3DAudio_pDump, (uint16_t)(int16_t)(r * 32767.0f));
        }

        J3DAudio_dumpFrames += buffer.frameLength;
    });
}

/* ---- Device ------------------------------------------------------------------------------------------------------- */

// "Sound 3D Rendering". The output type is set, not detected: on this development Mac automatic detection took a wired
// output for external speakers, for which Apple's automatic rendering places nothing (tests/audio3d.c measures it).
// Equal-power panning placed nothing either through a live engine, so it is not offered.
static int J3DAudio_RenderingSetting(void)
{
    int setting = wuRegistry_GetIntEx("Sound 3D Rendering", 0);
    return setting < 0 || setting > 3 ? 0 : setting;
}

static AVAudio3DMixingRenderingAlgorithm J3DAudio_RenderingAlgorithm(void)
{
    switch ( J3DAudio_RenderingSetting() )
    {
        case 1:  return AVAudio3DMixingRenderingAlgorithmHRTFHQ;
        case 2:  return AVAudio3DMixingRenderingAlgorithmHRTF;
        default: return AVAudio3DMixingRenderingAlgorithmAuto; // 0 headphones, 3 built-in speakers
    }
}

static AVAudioEnvironmentOutputType J3DAudio_OutputType(void)
{
    return J3DAudio_RenderingSetting() == 3 ? AVAudioEnvironmentOutputTypeBuiltInSpeakers : AVAudioEnvironmentOutputTypeHeadphones;
}

int J3DAudio_OpenDevice(J3DAudioRenderFunc pfRender, J3DAudioRenderSlotFunc pfRenderSlot, void* pUser, uint32_t* pSampleRate)
{
    @autoreleasepool
    {
        NSError* error = nil;
        AVAudioFormat* stereo;
        AVAudioFormat* mono;
        AVAudio3DMixingRenderingAlgorithm algorithm;
        const char* pDumpPath;

        if ( J3DAudio_engine || !pfRender || !pfRenderSlot || J3DAudio_EnvSet("J3D_HEADLESS") || J3DAudio_EnvSet("J3D_NO_AUDIO") )
        {
            return 0;
        }

        stereo = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:J3DAUDIO_RATE channels:2];
        mono   = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:J3DAUDIO_RATE channels:1];
        if ( !stereo || !mono )
        {
            return 0;
        }

        J3DAudio_engine      = [[AVAudioEngine alloc] init];
        J3DAudio_environment = [[AVAudioEnvironmentNode alloc] init];
        J3DAudio_submix      = [[AVAudioMixerNode alloc] init];

        // The stereo bus: everything that is not a 3D voice.
        J3DAudio_bus = [[AVAudioSourceNode alloc] initWithFormat:stereo renderBlock:
            ^OSStatus(BOOL* pbSilence, const AudioTimeStamp* pTime, AVAudioFrameCount frameCount, AudioBufferList* pBuffers) {
                (void)pbSilence;
                (void)pTime;
                pfRender((float*)pBuffers->mBuffers[0].mData, (float*)pBuffers->mBuffers[1].mData, frameCount, pUser);
                return noErr;
            }];

        [J3DAudio_engine attachNode:J3DAudio_environment];
        [J3DAudio_engine attachNode:J3DAudio_submix];
        [J3DAudio_engine attachNode:J3DAudio_bus];
        if ( !J3DAudio_Connect(J3DAudio_bus, J3DAudio_submix, 0, 0, stereo)
            || !J3DAudio_Connect(J3DAudio_environment, J3DAudio_submix, 0, 1, stereo)
            || !J3DAudio_Connect(J3DAudio_submix, J3DAudio_engine.mainMixerNode, 0, 0, stereo) )
        {
            J3DAudio_CloseDevice();
            return 0;
        }

        // The environment places only; the driver attenuates by distance (DirectSound 3D's model, per voice).
        J3DAudio_environment.distanceAttenuationParameters.distanceAttenuationModel = AVAudioEnvironmentDistanceAttenuationModelLinear;
        J3DAudio_environment.distanceAttenuationParameters.referenceDistance = 100000.0f;
        J3DAudio_environment.distanceAttenuationParameters.maximumDistance = 1000000.0f;
        J3DAudio_environment.distanceAttenuationParameters.rolloffFactor = 0.0f;
        J3DAudio_environment.reverbParameters.enable = NO;
        J3DAudio_environment.outputType = J3DAudio_OutputType();
        if ( @available(macOS 15.0, *) )
        {
            J3DAudio_environment.listenerHeadTrackingEnabled = wuRegistry_GetIntEx("Sound Head Tracking", 0) != 0;
        }

        // The 3D slots.
        algorithm = J3DAudio_RenderingAlgorithm();
        for ( int i = 0; i < J3DAUDIO_NUM3DSLOTS; ++i )
        {
            const int slot = i;
            AVAudioSourceNode* node = [[AVAudioSourceNode alloc] initWithFormat:mono renderBlock:
                ^OSStatus(BOOL* pbSilence, const AudioTimeStamp* pTime, AVAudioFrameCount frameCount, AudioBufferList* pBuffers) {
                    (void)pTime;
                    if ( !pfRenderSlot(slot, (float*)pBuffers->mBuffers[0].mData, frameCount, pUser) )
                    {
                        *pbSilence = YES;
                    }

                    return noErr;
                }];
            [J3DAudio_engine attachNode:node];
            if ( !J3DAudio_Connect(node, J3DAudio_environment, 0, [J3DAudio_environment nextAvailableInputBus], mono) )
            {
                J3DAudio_aSlots[i] = node;
                J3DAudio_CloseDevice();
                return 0;
            }

            node.renderingAlgorithm = algorithm;
            node.position = AVAudioMake3DPoint(0.0f, 0.0f, -1.0f);
            J3DAudio_aSlots[i] = node;
        }

        pDumpPath = getenv("J3D_AUDIO_DUMP");
        if ( pDumpPath && pDumpPath[0] )
        {
            J3DAudio_StartDump(pDumpPath, stereo);
        }

        [J3DAudio_engine prepare];
        if ( ![J3DAudio_engine startAndReturnError:&error] )
        {
            J3DAudio_CloseDevice();
            return 0;
        }

        // Output changes stop the engine; start it again, off the notifying thread.
        J3DAudio_queue = dispatch_queue_create("J3DAudio", DISPATCH_QUEUE_SERIAL);
        J3DAudio_configObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:AVAudioEngineConfigurationChangeNotification
                        object:J3DAudio_engine
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                        (void)note;
                        dispatch_async(J3DAudio_queue, ^{
                            AVAudioEngine* engine = J3DAudio_engine;
                            if ( engine && !engine.isRunning )
                            {
                                [engine prepare];
                                [engine startAndReturnError:nil];
                            }
                        });
                    }];

        if ( pSampleRate )
        {
            *pSampleRate = (uint32_t)J3DAUDIO_RATE;
        }

        return 1;
    }
}

void J3DAudio_CloseDevice(void)
{
    @autoreleasepool
    {
        if ( !J3DAudio_engine )
        {
            return;
        }

        if ( J3DAudio_configObserver )
        {
            [[NSNotificationCenter defaultCenter] removeObserver:J3DAudio_configObserver];
            J3DAudio_configObserver = nil;
        }

        if ( J3DAudio_queue )
        {
            dispatch_sync(J3DAudio_queue, ^{}); // a restart in flight finishes first
            J3DAudio_queue = nil;
        }

        [J3DAudio_engine stop]; // the render blocks are not called after this returns
        if ( J3DAudio_pDump )
        {
            [J3DAudio_submix removeTapOnBus:0];
            J3DAudio_WriteWavHeader(J3DAudio_pDump, J3DAudio_dumpFrames);
            fclose(J3DAudio_pDump);
            J3DAudio_pDump = NULL;
        }

        for ( int i = 0; i < J3DAUDIO_NUM3DSLOTS; ++i )
        {
            J3DAudio_aSlots[i] = nil;
        }

        J3DAudio_bus = nil;
        J3DAudio_submix = nil;
        J3DAudio_environment = nil;
        J3DAudio_engine = nil;
    }
}

void J3DAudio_SetListener(const float aPos[3], const float aFront[3], const float aUp[3])
{
    AVAudioEnvironmentNode* environment = J3DAudio_environment;
    if ( !environment )
    {
        return;
    }

    environment.listenerPosition = J3DAudio_ToAV(aPos);
    environment.listenerVectorOrientation = AVAudioMake3DVectorOrientation(J3DAudio_ToAVVector(aFront), J3DAudio_ToAVVector(aUp));
}

void J3DAudio_SetSlotPosition(int slot, const float aPos[3])
{
    if ( slot < 0 || slot >= J3DAUDIO_NUM3DSLOTS || !J3DAudio_aSlots[slot] )
    {
        return;
    }

    J3DAudio_aSlots[slot].position = J3DAudio_ToAV(aPos);
}

void J3DAudio_SetMirror(int bMirror)
{
    J3DAudio_bMirror = bMirror != 0; // applies from the next listener and slot update (every frame)
}
