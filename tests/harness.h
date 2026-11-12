#ifndef J3D_TESTS_HARNESS_H
#define J3D_TESTS_HARNESS_H
/*
 * Shared setup for the headless test programs (PORT-PLAN.md Phase 1): engine startup without display, input, sound
 * output or dialogs, plus assert and crash handlers that print a backtrace.
 */

// Starts the engine against the game data in pDataRoot (the folder holding RESOURCE/), in JonesMain_Startup's order.
// The player's settings file is not touched (J3D_SETTINGS defaults to /dev/null). Returns 0 on success.
int harness_Startup(const char* pDataRoot);

// Line-buffers stdout and installs the SIGSEGV/SIGBUS backtrace handler. Call first.
void harness_InstallHandlers(void);

#endif
