#ifndef J3D_PLATFORM_HOST_KEYS_H
#define J3D_PLATFORM_HOST_KEYS_H
/*
 * Keyboard tables (PORT-PLAN.md P3.1).
 *
 * The engine names keys by DirectInput key code (DIK_*), which is the PC keyboard's set-1 scan code with 0x80 added for
 * the E0-prefixed keys: the keysets, sithControl's bindings and stdControl's key ids all use it. The host reports keys
 * by USB HID usage (page 7), which is also what SDL's SDL_Scancode values are, so these tables need no SDL and serve
 * every platform:
 *
 *   J3DHost_HidToDik   the key a HID usage is, by position on the keyboard as on a US layout (DirectInput's keyboard
 *                      device reported positions too). The Mac's Option keys are DIK_LMENU and DIK_RMENU (Alt), the
 *                      Command keys DIK_LWIN and DIK_RWIN (P3.9).
 *   J3DHost_DikName    the name Windows' GetKeyNameText gave the key on a US English keyboard ("Esc", "Num 7",
 *                      "Right Ctrl"), which is what the controls dialog showed. GetKeyNameTextA (crt_posix.c) uses it.
 *   J3DHost_DikFromName  the reverse, case-insensitive; also accepts the DIK_ name without its prefix ("LCONTROL").
 */
#include <stddef.h>
#include <stdint.h>

// 0 for a usage with no DirectInput key.
uint8_t J3DHost_HidToDik(uint32_t hidUsage);

// The first HID usage that maps to dik, or 0 if none does.
uint32_t J3DHost_DikToHid(uint8_t dik);

// NULL for a code no key has.
const char* J3DHost_DikName(uint8_t dik);

// 0 if no key has the name.
uint8_t J3DHost_DikFromName(const char* pName);

#endif
