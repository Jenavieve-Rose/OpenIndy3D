#ifndef J3D_PLATFORM_HOST_CRASH_H
#define J3D_PLATFORM_HOST_CRASH_H
/*
 * Crash handling (PORT-PLAN.md P8.11).
 *
 * The engine's log goes nowhere by default (retail's "Debug Mode" 0), so its last lines are kept in memory, whatever
 * the mode. When the game crashes (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP), the handler writes them with a
 * backtrace to ~/Library/Logs/OpenIndy3D/Crash.log (J3D_LOGS overrides the folder), then lets the signal take its
 * course, so macOS writes its own crash report too. The next run tells the player where the log is.
 */
#include <stddef.h>

#include <std/types.h>

// Installs the signal handlers. Called once by the app entry point, after the user folders exist.
void J3DHost_InstallCrashHandler(void);

// Wraps the five print hooks of pHS so every line also goes to the in-memory log. Called once JonesMain has chosen
// them; NULL hooks are recorded only.
void J3DHost_TapPrints(tHostServices* pHS);

// Whether the previous run crashed: returns 1 once, with the crash log's path, and forgets it.
int J3DHost_TakeLastCrash(char* pPath, size_t size);

#endif
