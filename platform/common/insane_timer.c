/*
 * insane_timer for the port (PORT-PLAN.md P5.3): INSANE's `TIMER=insane` service, in place of retail's Win32
 * Libs/smush/insane/Timer/Timer.c (excluded from the build). Shared by every build.
 *
 * Retail: 19 software timers, driven by one THREAD_PRIORITY_TIME_CRITICAL thread that called timer_Tick 60 times a
 * second (every 16 2/3 ms, the remainder of 1000 / 60 carried so the rate does not drift), timed with timeGetTime and
 * WaitForSingleObject on an event that shutdown signalled. Each tick adds tickLen (0x4DAE, 1193180 / 60, the 8253 PIT
 * rate the 1992-era interface counts in) to the global tick count and to every live timer, and calls a timer's
 * callback once for each interval it has accumulated. The callback contract the player relies on is kept unchanged:
 *
 *   flags & 5 == 1   periodic, called from the first pass;
 *   flags & 5 == 0   called from the second pass, guarded against re-entry by `busy`;
 *   flags & 5 == 4   "deferred", called from timer_Poll's pass (no INSANE module allocates one);
 *   flags & 2        one-shot: its interval is cleared before the call (Dsnd.c re-arms it with SetInterval);
 *   flags & 8        resynchronised by Sync: the sound engine (Engine.c) calls Sync with the ticks the audio has
 *                    consumed, and such a timer's elapsed time follows the audio instead of the tick count. The
 *                    animation player's frame pacer is one; that is INSANE's audio/video sync.
 *
 * What changed:
 *   - The thread is a pthread at the user-interactive QoS class, timed on the host's monotonic clock with absolute
 *     deadlines (nanosleep to each). It never reads the engine's clock (timeGetTime), which a headless or recorded
 *     run replaces with a virtual one; INSANE paces movies in real time.
 *   - A recursive mutex is held for each tick and by Alloc, Free, SetInterval and Sync. Retail had none (the critical
 *     section it entered was a local of the thread, so it excluded nothing): a player that freed its timers and then
 *     its state could race a callback already running on the timer thread. With the lock, once Free returns no
 *     callback of that timer is running or will run. Callbacks may call back into the service (Dsnd re-arms itself).
 *   - timer_Poll's setjmp/longjmp pair is gone. Retail jumped to timer_jmpB only when timer_x36C was set, which
 *     nothing does, and its other longjmp returned straight to its own setjmp; the C below runs the same steps.
 */
#include <smush/insane/lec.h>
#include <smush/insane/Timer/Timer.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

typedef struct TimerEntry
{
    TimerCallback pfn;  /* NULL: the slot is free */
    int interval;       /* ticks */
    unsigned flags;     /* see above */
    intptr_t param;
    int busy;           /* re-entry guard of the second pass */
    int elapsed;
    int running;
    int total;          /* ticks since the last Sync, for flags & 8 */
} TimerEntry;

#define TIMER_COUNT   19
#define TIMER_TICKLEN 0x4DAE // 1193180 / 60
#define TIMER_PERIOD_NS (1000000000LL / 60)
#define TIMER_PERIOD_REM (1000000000LL % 60)

int timer_Register(LecShare* pShare, void* pParam);
static void timer_Unregister(void);
static int timer_Startup(void);
static void timer_Shutdown(void);
static void timer_Pause(void);
static void timer_Resume(void);
static int timer_Alloc(TimerCallback pfn, int interval, unsigned flags, intptr_t param);
static void timer_Free(int index);
static void timer_SetInterval(int index, int interval);
static void timer_Wait(int ticks);
static int timer_GetTicks(void);
static int timer_GetTickLen(void);
static void timer_Sync(int ticks);

static LecModule timer_module = { timer_Register, timer_Unregister, timer_Startup, timer_Shutdown };
static TimerInterface timer_interface = {
    timer_Alloc, timer_Free, timer_Pause, timer_Resume, timer_SetInterval, timer_Wait, timer_GetTicks, timer_GetTickLen, timer_Sync
};

static LecShare* timer_pShare = NULL;
static void* timer_pParam = NULL;
static void* timer_pServices = NULL;
static int timer_rate = 0;
static int timer_refs = 0;
static int timer_tickLen = 0;
static TimerEntry timer_entries[TIMER_COUNT + 1];
static atomic_int timer_ticks = 0;
static atomic_int timer_paused = 0;
static int timer_reent = 0;
static int timer_inTick = 0;
static int timer_savedReent = 0;
static int timer_polling = 0;

static pthread_mutex_t timer_lock;
static pthread_once_t timer_lockOnce = PTHREAD_ONCE_INIT;
static pthread_t timer_thread;
static atomic_int timer_bStop = 0;

static void timer_InitLock(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&timer_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

int timer_Register(LecShare* pShare, void* pParam)
{
    pthread_once(&timer_lockOnce, timer_InitLock);
    timer_pShare = pShare;
    timer_pParam = pParam;
    pShare->Publish("LECMODULE=insane_timer", &timer_module);
    timer_pShare->Publish("TIMER=insane", &timer_interface);
    timer_rate = 300;
    return 0;
}

static void timer_Unregister(void)
{
    timer_pShare->Unpublish("TIMER=insane");
    timer_pShare->Unpublish("LECMODULE=insane_timer");
}

// One timer's due calls: one per accumulated interval, a one-shot only once. Caller holds the lock.
static void timer_RunEntry(int i)
{
    TimerEntry* p = &timer_entries[i];

    while (p->elapsed >= p->interval) {
        p->elapsed -= p->interval;
        if (p->flags & 2) {
            p->interval = 0;
            p->pfn(i, p->param);
            break;
        }
        p->pfn(i, p->param);
        if (p->pfn == NULL || p->interval == 0) {
            break; // Port: the callback freed or disarmed its own timer; retail would have called through the freed slot
        }
    }
}

// retail timer_RunDeferred: the flags & 5 == 4 timers
static void timer_RunDeferred(void)
{
    int i;

    for (i = 1; i < TIMER_COUNT + 1; i++) {
        timer_entries[i].running = 1;
        if (timer_entries[i].pfn && (timer_entries[i].flags & 5) == 4 && timer_entries[i].interval != 0) {
            timer_RunEntry(i);
        }
        timer_entries[i].running = 0;
    }
}

static void timer_Tick(void)
{
    int i;

    atomic_fetch_add(&timer_ticks, timer_tickLen);
    if (atomic_load(&timer_paused) != 0) {
        return;
    }

    timer_savedReent = timer_reent;
    timer_inTick = 1;
    timer_reent = 0;
    for (i = 1; i < TIMER_COUNT + 1; i++) {
        TimerEntry* p = &timer_entries[i];

        if (p->pfn) {
            p->elapsed += timer_tickLen;
            p->total += timer_tickLen;
            if ((p->flags & 5) == 1 && p->interval != 0) {
                timer_RunEntry(i);
            }
        }
    }
    for (i = 1; i < TIMER_COUNT + 1; i++) {
        TimerEntry* p = &timer_entries[i];

        if (p->pfn && !(p->flags & 5) && p->interval != 0 && p->busy == 0) {
            p->busy = 1;
            timer_RunEntry(i);
            p->busy = 0;
        }
    }
    timer_inTick = 0;
    timer_reent = timer_savedReent;

    // retail timer_Poll, guarded by timer_x368 (here timer_polling)
    if (timer_polling == 0) {
        timer_polling = 1;
        timer_RunDeferred();
        timer_reent = 0;
        timer_polling = 0;
    }
}

static void timer_AddNs(struct timespec* pTime, long long ns)
{
    long long total = (long long)pTime->tv_nsec + ns;
    pTime->tv_sec += (time_t)(total / 1000000000LL);
    pTime->tv_nsec = (long)(total % 1000000000LL);
}

static long long timer_DiffNs(const struct timespec* pA, const struct timespec* pB)
{
    return ((long long)pA->tv_sec - (long long)pB->tv_sec) * 1000000000LL + ((long long)pA->tv_nsec - (long long)pB->tv_nsec);
}

static void* timer_Thread(void* pArg)
{
    struct timespec next;
    struct timespec now;
    long long frac = 0;

    (void)pArg;
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); // retail: THREAD_PRIORITY_TIME_CRITICAL
#endif
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!atomic_load(&timer_bStop)) {
        long long wait;

        pthread_mutex_lock(&timer_lock);
        timer_Tick();
        pthread_mutex_unlock(&timer_lock);

        // the next deadline: 1/60 s on, the remainder carried as retail carried 1000 % 60 ms
        timer_AddNs(&next, TIMER_PERIOD_NS);
        frac += TIMER_PERIOD_REM;
        if (frac >= 60) {
            frac -= 60;
            timer_AddNs(&next, 1);
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        wait = timer_DiffNs(&next, &now);
        if (wait > 0) {
            struct timespec delay = { (time_t)(wait / 1000000000LL), (long)(wait % 1000000000LL) };
            nanosleep(&delay, NULL);
        }
        else if (wait < -100000000LL) {
            next = now; // more than 100 ms late (the host slept): resume from now instead of ticking to catch up
        }
    }
    return NULL;
}

static int timer_Start(void)
{
    int i;

    if (timer_refs++ == 0) {
        pthread_mutex_lock(&timer_lock);
        for (i = 1; i < TIMER_COUNT + 1; i++) {
            timer_entries[i].pfn = NULL;
        }
        timer_tickLen = TIMER_TICKLEN;
        pthread_mutex_unlock(&timer_lock);
        atomic_store(&timer_bStop, 0);
        if (pthread_create(&timer_thread, NULL, timer_Thread, NULL) != 0) {
            timer_refs--;
            return -1; // retail did not check CreateThread
        }
    }
    return 0;
}

static void timer_Stop(void)
{
    if (timer_refs > 0 && --timer_refs == 0) {
        atomic_store(&timer_bStop, 1);
        pthread_join(timer_thread, NULL);
    }
}

static int timer_Startup(void)
{
    timer_pServices = timer_pShare->Subscribe("MEMORY.workspace", timer_pParam);
    if (timer_pServices == NULL) {
        timer_pShare->ReportError("Timer Module: Cannot allocate memory services.");
        return -1;
    }
    return timer_Start(); // retail passed 1193180 / timer_rate and 0x10000, which it did not use
}

static void timer_Shutdown(void)
{
    timer_pShare->Unsubscribe(timer_pServices);
    timer_Stop();
}

static void timer_Pause(void)
{
    atomic_fetch_add(&timer_paused, 1);
}

static void timer_Resume(void)
{
    atomic_fetch_sub(&timer_paused, 1);
}

static int timer_Alloc(TimerCallback pfn, int interval, unsigned flags, intptr_t param)
{
    int i;
    int index = 0;

    pthread_mutex_lock(&timer_lock);
    for (i = 1; i < TIMER_COUNT + 1; i++) {
        if (timer_entries[i].pfn == NULL) {
            timer_entries[i].busy = 0;
            timer_entries[i].elapsed = 0;
            timer_entries[i].param = param;
            timer_entries[i].flags = flags;
            timer_entries[i].interval = interval;
            timer_entries[i].running = 0;
            timer_entries[i].total = 0;
            timer_entries[i].pfn = pfn;
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&timer_lock);
    return index;
}

static void timer_Free(int index)
{
    if (index > 0 && index < TIMER_COUNT + 1) {
        pthread_mutex_lock(&timer_lock);
        timer_entries[index].pfn = NULL;
        pthread_mutex_unlock(&timer_lock);
    }
}

static void timer_SetInterval(int index, int interval)
{
    if (index > 0 && index < TIMER_COUNT + 1) {
        pthread_mutex_lock(&timer_lock);
        timer_entries[index].interval = interval;
        timer_entries[index].elapsed = 0;
        pthread_mutex_unlock(&timer_lock);
    }
}

static void timer_Wait(int ticks)
{
    int start = atomic_load(&timer_ticks);
    int end = start + ticks;
    int cur = start;

    while (cur >= start && cur < end) {
        struct timespec delay = { 0, 1000000 };
        nanosleep(&delay, NULL); // Port: retail spun
        cur = atomic_load(&timer_ticks);
    }
}

static int timer_GetTicks(void)
{
    return atomic_load(&timer_ticks);
}

static int timer_GetTickLen(void)
{
    return timer_tickLen;
}

static void timer_Sync(int ticks)
{
    int i;

    pthread_mutex_lock(&timer_lock);
    for (i = 1; i < TIMER_COUNT + 1; i++) {
        TimerEntry* p = &timer_entries[i];

        if (p->pfn && (p->flags & 8)) {
            if (ticks > 0) {
                p->elapsed += ticks - p->total;
            }
            p->total = 0;
        }
    }
    pthread_mutex_unlock(&timer_lock);
}
