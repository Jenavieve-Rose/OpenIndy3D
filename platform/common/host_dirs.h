#ifndef J3D_PLATFORM_HOST_DIRS_H
#define J3D_PLATFORM_HOST_DIRS_H
#include <platform/common/host_data.h>
/*
 * Where the port writes (PORT-PLAN.md P1.19).
 *
 * The retail game wrote everything under its install folder. The port never writes to the player's game data:
 *
 *   ~/Library/Application Support/OpenIndy3D/   settings.ini, and the default "Install Path": SaveGames/,
 *                                                ScreenShots/ and KeySets/ (the engine appends those names)
 *   ~/Library/Logs/OpenIndy3D/                   JonesError.txt and JonesLog.txt
 *
 * J3D_REG_Install_Path overrides the first, like any setting; J3D_LOGS overrides the second. Both returned paths end
 * in '/'.
 */

// "~/Library/Application Support/OpenIndy3D/". Not created here.
const char* J3DHost_GetUserDir(void);

// J3D_LOGS, or "~/Library/Logs/OpenIndy3D/". Created on first call.
const char* J3DHost_GetLogDir(void);

// The game data root ("Data Root": the folder holding RESOURCE/), ending in '/', or "" when none is set. Valid once the
// settings module has started. (wuRegistry_file.c)
const char* J3DHost_GetDataRoot(void);

// Creates pPath and every missing folder above it. Returns 0 on success.
int J3DHost_MakeDirs(const char* pPath);

// Prepares the per-user folders and the game data before the game starts. Moves a ~/Library/Application Support/
// J3DPort/ of an earlier build to the new name when there is none yet (P8.1), starts the settings module, makes sure
// of the game data (J3DHost_EnsureData, host_data.h, with pfAskData and bChooseData), creates the effective "Install
// Path" and, the first time, copies the retail keysets (*.kfg in the data root's KeySets folder) into its KeySets/ so
// the game still finds them. Returns 1 when the player declined to choose a data folder (quit), else 0. Called by the
// app entry point only, so the test programs never write to the user's folders.
int J3DHost_PrepareUserDirs(J3DHostAskData pfAskData, int bChooseData);

#endif
