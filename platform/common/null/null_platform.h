#ifndef J3D_PLATFORM_NULL_PLATFORM_H
#define J3D_PLATFORM_NULL_PLATFORM_H
/*
 * Internal to the null platform: the message-queue state that ties PostMessage (win32_null.c) to the event pump
 * (wkernel_null.c), and the abort used by entry points that must never run without a backend.
 */

// Records a posted window message. Only WM_CLOSE and WM_QUIT have an effect; everything else is dropped.
void null_PostMessage(unsigned int msg);

// Returns 1 once per posted WM_CLOSE, then clears it.
int null_TakeCloseRequest(void);

// PostQuitMessage, and the end of the WM_CLOSE -> WM_DESTROY chain.
void null_RequestQuit(void);
int  null_IsQuitRequested(void);

// For functions whose only callers are in a subsystem that cannot start without a real backend (movie decoding, for
// example). Reaching one means a startup check is missing, so it aborts rather than returning a wrong answer.
_Noreturn void null_Unreachable(const char* pFunction);

#endif
