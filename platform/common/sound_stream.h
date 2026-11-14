#ifndef J3D_PLATFORM_SOUND_STREAM_H
#define J3D_PLATFORM_SOUND_STREAM_H
/*
 * One streaming stereo source in the sound driver's mix (PORT-PLAN.md P5.9): the movie player's output.
 *
 * INSANE's DirectSound driver kept a looping buffer of a few blocks and refilled the block behind the play cursor. The
 * port's driver (platform/common/insane_dsnd.c) writes blocks of 16-bit stereo frames here instead, and reads back how
 * many the output has consumed, the play cursor. SoundDriver_mix.c mixes the stream into its stereo bus, resampled to
 * the device's rate, while it is open.
 *
 * The play cursor moves on its own, as DirectSound's did: with an output device it is what the device has rendered
 * (silence once it passes what was written); with none (null build, J3D_NO_AUDIO, no device) it is the host's real time
 * since the stream opened, at the stream's rate. Movies are paced in real time, never on the engine's clock.
 *
 * Called from INSANE's timer thread; every function takes the driver's lock.
 */
#include <stdint.h>

// The ring's size in frames. A writer keeps what it has written but the output has not consumed below this.
#define J3DSOUNDSTREAM_CAPACITY 16384

// Opens the stream at sampleRate frames per second, cursor and write position at 0, gain 1. Reopening restarts it.
void J3DSoundStream_Open(uint32_t sampleRate);
void J3DSoundStream_Close(void);

// Frames the output has consumed since the stream opened (the play cursor), and frames written.
uint64_t J3DSoundStream_GetPlayed(void);
uint64_t J3DSoundStream_GetWritten(void);

// Appends frameCount interleaved stereo frames. If the cursor has passed what was written (an underrun, heard as
// silence), writing resumes at the cursor.
void J3DSoundStream_Write(const int16_t* pFrames, uint32_t frameCount);

// The stream's linear gain (DirectSound's buffer volume).
void J3DSoundStream_SetGain(float gain);

#endif
