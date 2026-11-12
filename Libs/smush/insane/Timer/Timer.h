#ifndef SMUSH_INSANE_TIMER_H
#define SMUSH_INSANE_TIMER_H

#include <stdint.h>

/*
 * The `TIMER=insane` service Timer.c publishes (`timer_interface`), subscribed by the animation player (Pcplay.c), the DirectSound
 * driver (Dsnd.c) and the sound engine (Engine.c, as `TIMER.external_sync`).
 */
// Port: param is intptr_t; the animation player passes its AnimPlay* through it.
typedef void (*TimerCallback)(int index, intptr_t param);

typedef struct TimerInterface
{
    int  (*Alloc)(TimerCallback pfn, int interval, unsigned flags, intptr_t param);
    void (*Free)(int index);
    void (*Pause)(void);
    void (*Resume)(void);
    void (*SetInterval)(int index, int interval);
    void (*Wait)(int ticks);
    int  (*GetTicks)(void);
    int  (*GetTickLen)(void);
    void (*Sync)(int ticks);
} TimerInterface;

#endif
