#ifndef SOUND_DRIVER_H
#define SOUND_DRIVER_H
#include <j3dcore/j3d.h>
#include <sound/types.h>

// Port: no <dsound.h>; the handles are the opaque types in sound/types.h (P4.2)

J3D_EXTERN_C_START

int J3DAPI SoundDriver_Open(int bNoSound3D, int bGlobalFocus, HWND hwnd, SoundDevice* pDirectSound, SoundCalcListenerSoundMixFunc pfCalcListenerSoundMix, SoundDriverGetSoundBufferDataFunc pfGetSoundBufferData, tPrintfFunc pfLogError);
void J3DAPI SoundDriver_Close(void);

void J3DAPI SoundDriver_SetMaxVolume(float volume);
SoundVoice* J3DAPI SoundDriver_CreateAndPlay(int samplesPerSec, int nBitsPerSample, int numChannels, const uint8_t* pSoundData, size_t dataSize, tSoundChannelFlag* pFlags, SoundVoice* pSBuffer);
void J3DAPI SoundDriver_Release(SoundVoice* pDSBuf);
void J3DAPI SoundDriver_Play(SoundVoice* pDSBuf, int bLoop);
void J3DAPI SoundDriver_Stop(SoundVoice* pDSBuf);

void J3DAPI SoundDriver_SetVolume(SoundVoice* pDSBuf, float volume);
float J3DAPI SoundDriver_GetVolume(SoundVoice* pDSBuf);

void J3DAPI SoundDriver_SetPan(SoundVoice* pDSoundBuf, float pan);
float J3DAPI SoundDriver_GetPan(SoundVoice* pDSBuf);

void J3DAPI SoundDriver_SetFrequency(SoundVoice* pDSBuf, float freq);
float J3DAPI SoundDriver_GetFrequency(SoundVoice* pDSBuf);

size_t J3DAPI SoundDriver_GetCurrentPosition(SoundVoice* pDSoundBuf);
tSoundChannelFlag J3DAPI SoundDriver_GetStatusAndCaps(SoundVoice* pDSBuffer);

void J3DAPI SoundDriver_SetListenerPosition(const rdVector3* pPos, const rdVector3* pVelocity, const rdVector3* pTopOrient, const rdVector3* pFrontOrient, const rdVector3* pRightOrient);
void J3DAPI SoundDriver_ListenerCommitDeferred(void);

void J3DAPI SoundDriver_SetPosAndVelocity(SoundVoice* pBuffer, float x, float y, float z, float velX, float velY, float velZ, float minDistance, float maxDistance, SoundEnvFlags envflags);

int J3DAPI SoundDriver_Update3DSound(SoundVoice** ppDSBuf, float x, float y, float z, float volume, float pitch, tSoundChannelFlag* pChannelFlags, float minRadius, float maxRadius, SoundEnvFlags envflags);

void J3DAPI SoundDriver_SetGlobals(float distanceFactor, float minDistance, float maxDistance, float rolloffFactor, float dopplerFactor);

int J3DAPI SoundDriver_Has3DHW(void);
int J3DAPI SoundDriver_Use3DCaps(void);
SoundDevice* J3DAPI SoundDriver_GetDSound(void);

SoundDevice* J3DAPI SoundDriver_CreateDirectSound(int bNoSound3D);
void J3DAPI SoundDriver_ReleaseDirectSound(SoundDevice* pDSound);

void J3DAPI SoundDriver_CreateListener(void);
void J3DAPI SoundDriver_ReleaseListener(void);

void J3DAPI SoundDriver_SetOuputFormat(SoundDevice* pDSound, uint32_t nSamplesPerSec, uint32_t nBitsPerSample, uint32_t numChannels);

SoundVoice* J3DAPI SoundDriver_GetPrimaryDSBuffer(SoundDevice* pDSound);
void J3DAPI SoundDriver_ReleasePrimaryDSBuffer(void);

int J3DAPI SoundDriver_GetDecibelVolume(float volume);
int J3DAPI SoundDriver_GetIntPan(float pan);

float J3DAPI SoundDriver_GetVolumeFromDecibels(int dB);
float J3DAPI SoundDriver_GetPanFromTable(int pan);

uint32_t J3DAPI SoundDriver_GetTimeMsec(void);
void J3DAPI SoundDriver_Sleep(unsigned int dwMilliseconds);

J3D_EXTERN_C_END
#endif // SOUND_DRIVER_H
