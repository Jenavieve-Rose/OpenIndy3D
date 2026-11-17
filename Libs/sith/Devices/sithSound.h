#ifndef SITH_SITHSOUND_H
#define SITH_SITHSOUND_H
#include <j3dcore/j3d.h>
#include <sith/types.h>
#include <sith/Main/sithMain.h>
#include <std/types.h>

J3D_EXTERN_C_START

// Max number of simultaneous playing sounds
#ifdef J3D_FIXES
#define SITHSOUND_MAXCHANNELS 128 // Fix (OpenJones3D d8fdedc): sounds playing at once; 32 dropped new sounds in busy scenes
#else
#define SITHSOUND_MAXCHANNELS 32
#endif

int J3DAPI sithSound_Initialize(tHostServices* pHS);
void J3DAPI sithSound_Uninitialize(void);

int J3DAPI sithSound_Startup(int bSound3D);
int J3DAPI sithSound_StartupSound(void);

void J3DAPI sithSound_Shutdown(void);
void J3DAPI sithSound_ShutdownSound(void);

int J3DAPI sithSound_WriteSoundsListText(const SithWorld* pWorld); // linker-discarded; the debug build's sithWrite.c calls it
int J3DAPI sithSound_ReadSoundsListText(SithWorld* pWorld, int bSkip);

int J3DAPI sithSound_WriteSoundsListBinary(tFileHandle fh, const SithWorld* pWorld);
int J3DAPI sithSound_ReadSoundsListBinary(tFileHandle fh, SithWorld* pWorld);

void J3DAPI sithSound_FreeWorldSounds(SithWorld* pWorld);

tSoundHandle J3DAPI sithSound_Load(SithWorld* pWorld, const char* filename);
void J3DAPI sithSound_SetSkipRestoringSounds(int bSkip);

J3D_EXTERN_C_END
#endif // SITH_SITHSOUND_H
