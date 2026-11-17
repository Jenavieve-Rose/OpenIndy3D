#ifndef J3D_PLATFORM_HOST_APPLE_H
#define J3D_PLATFORM_HOST_APPLE_H
#include <stddef.h>
/*
 * Host facts for the Apple platform build (PORT-PLAN.md P2.1). C interface; the implementation is Objective-C
 * (host_apple.m), so the engine and the C backends never include an Objective-C header.
 */

// Prints one line to stdout naming the macOS version, the default Metal device and the SDL3 version linked, so a log
// shows what the game ran on. Safe to call before SDL or the engine is started.
void J3DApple_LogHostInfo(void);

// The data folder question (PORT-PLAN.md P8.4), a J3DHostAskData (host_data.h): an alert saying why, then an open
// panel for a folder. Returns 1 with the folder in pRoot, or 0 when the player quits. Starts SDL's video for the
// duration, so the application object is SDL's.
int J3DApple_AskForData(const char* pProblem, char* pRoot, size_t size);

// Whether Option is held down, as the app starts: the player asks to choose the data folder again.
int J3DApple_IsOptionHeld(void);

#endif // J3D_PLATFORM_HOST_APPLE_H
