/*
 * Crash handling (PORT-PLAN.md P8.11); see host_crash.h.
 */
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <platform/common/host_crash.h>
#include <platform/common/host_dirs.h>
#include <platform/common/host_product.h>

#define HOSTCRASH_RINGSIZE (64 * 1024)

static char   hostcrash_aRing[HOSTCRASH_RINGSIZE]; // the last output, written round
static size_t hostcrash_ringPos;
static int    hostcrash_bRingWrapped;
static char   hostcrash_aLogPath[1100];
static char   hostcrash_aPendingPath[1100]; // exists from a crash until the next run has told the player
static char   hostcrash_aHeader[256];

static tPrintfFunc hostcrash_apfOriginal[5];

static void hostcrash_Record(const char* pText)
{
    for ( ; *pText; ++pText )
    {
        hostcrash_aRing[hostcrash_ringPos++] = *pText;
        if ( hostcrash_ringPos == HOSTCRASH_RINGSIZE )
        {
            hostcrash_ringPos = 0;
            hostcrash_bRingWrapped = 1;
        }
    }
}

// The wrappers: format once, record, and hand the text on to the hook JonesMain chose
#define HOSTCRASH_TAP(n)                                                                                               \
    static int J3DAPI hostcrash_Print##n(const char* pFormat, ...)                                                     \
    {                                                                                                                  \
        char aText[2048];                                                                                              \
        va_list args;                                                                                                  \
        va_start(args, pFormat);                                                                                       \
        vsnprintf(aText, sizeof(aText), pFormat, args);                                                                \
        va_end(args);                                                                                                  \
        hostcrash_Record(aText);                                                                                       \
        return hostcrash_apfOriginal[n] ? hostcrash_apfOriginal[n]("%s", aText) : 0;                                   \
    }
HOSTCRASH_TAP(0)
HOSTCRASH_TAP(1)
HOSTCRASH_TAP(2)
HOSTCRASH_TAP(3)
HOSTCRASH_TAP(4)

void J3DHost_TapPrints(tHostServices* pHS)
{
    static const tPrintfFunc apfTaps[5] = { hostcrash_Print0, hostcrash_Print1, hostcrash_Print2, hostcrash_Print3, hostcrash_Print4 };
    tPrintfFunc* appfHooks[5] = { &pHS->pStatusPrint, &pHS->pMessagePrint, &pHS->pWarningPrint, &pHS->pErrorPrint, &pHS->pDebugPrint };
    for ( int i = 0; i < 5; i++ )
    {
        if ( *appfHooks[i] != apfTaps[i] )
        {
            hostcrash_apfOriginal[i] = *appfHooks[i];
            *appfHooks[i] = apfTaps[i];
        }
    }
}

static void hostcrash_Write(int fd, const char* pText)
{
    size_t len = strlen(pText);
    while ( len )
    {
        const ssize_t n = write(fd, pText, len);
        if ( n <= 0 )
        {
            return;
        }
        pText += n;
        len -= (size_t)n;
    }
}

static void hostcrash_Handler(int sig)
{
    // Only async-signal-safe calls from here: sigaction, open, write, close, backtrace_symbols_fd, raise

    // The kernel ignores SA_RESETHAND for SIGILL and SIGTRAP (xnu, like the BSDs), so put the default action back
    // ourselves; otherwise the raise() below re-enters this handler until the stack runs out. The guard covers a
    // second fault inside the handler itself.
    static volatile sig_atomic_t bInHandler = 0;
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, NULL);
    if ( bInHandler )
    {
        raise(sig);
        return;
    }
    bInHandler = 1;

    const int fd = open(hostcrash_aLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if ( fd >= 0 )
    {
        hostcrash_Write(fd, hostcrash_aHeader);
        hostcrash_Write(fd, "Signal: ");
        hostcrash_Write(fd, sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL"
                            : sig == SIGFPE ? "SIGFPE" : sig == SIGABRT ? "SIGABRT" : sig == SIGTRAP ? "SIGTRAP" : "other");
        hostcrash_Write(fd, "\n\nBacktrace:\n");
        void* apFrames[64];
        backtrace_symbols_fd(apFrames, backtrace(apFrames, 64), fd);
        hostcrash_Write(fd, "\nThe engine's last output:\n");
        if ( hostcrash_bRingWrapped )
        {
            (void)!write(fd, hostcrash_aRing + hostcrash_ringPos, HOSTCRASH_RINGSIZE - hostcrash_ringPos);
        }
        (void)!write(fd, hostcrash_aRing, hostcrash_ringPos);
        close(fd);

        const int pending = open(hostcrash_aPendingPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if ( pending >= 0 )
        {
            close(pending);
        }
    }

    // The handler was installed with SA_RESETHAND: raised again, the signal does what it would have done
    raise(sig);
}

void J3DHost_InstallCrashHandler(void)
{
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    return; // the sanitizers' own handlers report a crash in those builds
#endif
#endif
    const char* pDir = J3DHost_GetLogDir(); // created on first call
    snprintf(hostcrash_aLogPath, sizeof(hostcrash_aLogPath), "%sCrash.log", pDir);
    snprintf(hostcrash_aPendingPath, sizeof(hostcrash_aPendingPath), "%sCrash.pending", pDir);

    char aTime[64];
    const time_t now = time(NULL);
    struct tm when;
    localtime_r(&now, &when);
    strftime(aTime, sizeof(aTime), "%Y-%m-%d %H:%M:%S", &when);
    snprintf(hostcrash_aHeader, sizeof(hostcrash_aHeader), J3D_PRODUCT_NAME " " J3D_PRODUCT_VERSION " crashed. Started %s.\n", aTime);

    static const int aSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = hostcrash_Handler;
    action.sa_flags = SA_RESETHAND | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for ( size_t i = 0; i < sizeof(aSignals) / sizeof(aSignals[0]); i++ )
    {
        sigaction(aSignals[i], &action, NULL);
    }
}

int J3DHost_TakeLastCrash(char* pPath, size_t size)
{
    char aPending[1100];
    snprintf(aPending, sizeof(aPending), "%sCrash.pending", J3DHost_GetLogDir());
    struct stat st;
    if ( stat(aPending, &st) != 0 )
    {
        return 0;
    }
    unlink(aPending);
    snprintf(pPath, size, "%sCrash.log", J3DHost_GetLogDir());
    return 1;
}
