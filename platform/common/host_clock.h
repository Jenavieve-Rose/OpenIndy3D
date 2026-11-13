#ifndef J3D_PLATFORM_HOST_CLOCK_H
#define J3D_PLATFORM_HOST_CLOCK_H
/*
 * The millisecond clock behind timeGetTime, GetTickCount and Sleep (crt_posix.c).
 *
 * Normally it is the host's monotonic clock. A headless run (PORT-PLAN.md P1.17) switches it to a virtual clock that
 * moves only when told to: by a fixed step for every presented frame, and by the requested time for every Sleep. A
 * frame then always lasts the same game time however fast the host is, so a headless run is repeatable and is not
 * slowed down to real time.
 *
 * It is also what keeps a fast host inside the timing the 1999 code assumes. With no renderer a frame takes
 * microseconds, and sithTime_Advance clamps a frame to 1 ms; scripts that Sleep(0.001) to "let the engine get set up"
 * (00_CYN_Opening.cog) then wake a frame too late, after the autosave has already sent them its user0 message.
 */

// Switches to the virtual clock, starting from the host clock's current value. No way back.
void J3DHost_UseVirtualClock(void);
int  J3DHost_IsVirtualClock(void);

// Moves the virtual clock forward. Does nothing while the host clock is in use.
void J3DHost_AdvanceClock(unsigned int msec);

/*
 * Input recording and replay (PORT-PLAN.md P3.10, host_record.h) need a clock that a later run can reproduce exactly.
 *
 * The frame clock is the virtual clock for a run a player is playing: it stands still during a frame, as the virtual
 * clock does, and J3DHost_SyncFrameClock (called at each present) moves it to the host's time. Sleep waits for real
 * and also moves it. Every value the game reads is therefore one the recording can store and a replay can set again
 * with J3DHost_SetClock, on the plain virtual clock.
 */
#include <stdint.h>

// Switches to the frame clock. No effect once any virtual clock is on.
void J3DHost_UseFrameClock(void);

// Brings the frame clock up to the host's time; never moves it back. Does nothing on any other clock.
void J3DHost_SyncFrameClock(void);

// Sets the virtual clock (replay). Does nothing while the host clock is in use.
void J3DHost_SetClock(uint64_t msec);

// The current time, on whichever clock is in use.
uint64_t J3DHost_GetClock(void);

#endif
