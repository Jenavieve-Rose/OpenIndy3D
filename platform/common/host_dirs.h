#ifndef J3D_PLATFORM_HOST_DIRS_H
#define J3D_PLATFORM_HOST_DIRS_H
/*
 * Where the port writes (PORT-PLAN.md P1.19).
 *
 * The retail game wrote everything under its install folder. The port never writes to the player's game data:
 *
 *   ~/Library/Application Support/J3DPort/   settings.ini, and the default "Install Path": SaveGames/, ScreenShots/
 *                                             and KeySets/ (the engine appends those names to "Install Path")
 *   ~/Library/Logs/J3DPort/                   JonesError.txt and JonesLog.txt
 *
 * J3D_REG_Install_Path overrides the first, like any setting; J3D_LOGS overrides the second. Both returned paths end
 * in '/'.
 */

// "~/Library/Application Support/J3DPort/". Not created here.
const char* J3DHost_GetUserDir(void);

// J3D_LOGS, or "~/Library/Logs/J3DPort/". Created on first call.
const char* J3DHost_GetLogDir(void);

// The game data root ("Data Root": the folder holding RESOURCE/), ending in '/', or "" when none is set. Valid once the
// settings module has started. (wuRegistry_file.c)
const char* J3DHost_GetDataRoot(void);

// Creates pPath and every missing folder above it. Returns 0 on success.
int J3DHost_MakeDirs(const char* pPath);

// Prepares the effective "Install Path" before the game starts: creates it and, the first time, copies the retail
// keysets (*.kfg in the data root's KeySets folder) into its KeySets/ so the game still finds them. Starts the
// settings module (wuRegistry_Startup) if it has not been started. Called by the app entry point only, so the test
// programs never write to the user's folders.
void J3DHost_PrepareUserDirs(void);

#endif
