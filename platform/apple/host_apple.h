#ifndef J3D_PLATFORM_HOST_APPLE_H
#define J3D_PLATFORM_HOST_APPLE_H
/*
 * Host facts for the Apple platform build (PORT-PLAN.md P2.1). C interface; the implementation is Objective-C
 * (host_apple.m), so the engine and the C backends never include an Objective-C header.
 */

// Prints one line to stdout naming the macOS version, the default Metal device and the SDL3 version linked, so a log
// shows what the game ran on. Safe to call before SDL or the engine is started.
void J3DApple_LogHostInfo(void);

#endif // J3D_PLATFORM_HOST_APPLE_H
