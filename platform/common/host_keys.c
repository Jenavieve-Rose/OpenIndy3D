/*
 * Keyboard tables (PORT-PLAN.md P3.1): USB HID usage to DirectInput key code, and DirectInput key code to the name
 * Windows gave the key. See host_keys.h.
 */
#include <string.h>
#include <strings.h>

#include <Windows.h> // DIK_* (the shim's dinput subset)

#include "host_keys.h"

typedef struct HostKey
{
    uint32_t    hid;     // USB HID keyboard usage (page 7); equal to SDL_Scancode
    uint8_t     dik;     // DirectInput key code
    const char* pName;   // GetKeyNameText's name, US English
    const char* pDikName; // the DIK_ constant without its prefix
} HostKey;

// In HID usage order. Usages that no PC key matches (Mac media keys, F16 and up, the Lang keys) are left out.
static const HostKey host_aKeys[] = {
    { 0x04, DIK_A, "A", "A" },
    { 0x05, DIK_B, "B", "B" },
    { 0x06, DIK_C, "C", "C" },
    { 0x07, DIK_D, "D", "D" },
    { 0x08, DIK_E, "E", "E" },
    { 0x09, DIK_F, "F", "F" },
    { 0x0A, DIK_G, "G", "G" },
    { 0x0B, DIK_H, "H", "H" },
    { 0x0C, DIK_I, "I", "I" },
    { 0x0D, DIK_J, "J", "J" },
    { 0x0E, DIK_K, "K", "K" },
    { 0x0F, DIK_L, "L", "L" },
    { 0x10, DIK_M, "M", "M" },
    { 0x11, DIK_N, "N", "N" },
    { 0x12, DIK_O, "O", "O" },
    { 0x13, DIK_P, "P", "P" },
    { 0x14, DIK_Q, "Q", "Q" },
    { 0x15, DIK_R, "R", "R" },
    { 0x16, DIK_S, "S", "S" },
    { 0x17, DIK_T, "T", "T" },
    { 0x18, DIK_U, "U", "U" },
    { 0x19, DIK_V, "V", "V" },
    { 0x1A, DIK_W, "W", "W" },
    { 0x1B, DIK_X, "X", "X" },
    { 0x1C, DIK_Y, "Y", "Y" },
    { 0x1D, DIK_Z, "Z", "Z" },
    { 0x1E, DIK_1, "1", "1" },
    { 0x1F, DIK_2, "2", "2" },
    { 0x20, DIK_3, "3", "3" },
    { 0x21, DIK_4, "4", "4" },
    { 0x22, DIK_5, "5", "5" },
    { 0x23, DIK_6, "6", "6" },
    { 0x24, DIK_7, "7", "7" },
    { 0x25, DIK_8, "8", "8" },
    { 0x26, DIK_9, "9", "9" },
    { 0x27, DIK_0, "0", "0" },
    { 0x28, DIK_RETURN, "Enter", "RETURN" },
    { 0x29, DIK_ESCAPE, "Esc", "ESCAPE" },
    { 0x2A, DIK_BACK, "Backspace", "BACK" },
    { 0x2B, DIK_TAB, "Tab", "TAB" },
    { 0x2C, DIK_SPACE, "Space", "SPACE" },
    { 0x2D, DIK_MINUS, "-", "MINUS" },
    { 0x2E, DIK_EQUALS, "=", "EQUALS" },
    { 0x2F, DIK_LBRACKET, "[", "LBRACKET" },
    { 0x30, DIK_RBRACKET, "]", "RBRACKET" },
    { 0x31, DIK_BACKSLASH, "\\", "BACKSLASH" },
    { 0x32, DIK_BACKSLASH, "\\", "BACKSLASH" }, // the ISO key left of Return: the same scan code as \ on a PC
    { 0x33, DIK_SEMICOLON, ";", "SEMICOLON" },
    { 0x34, DIK_APOSTROPHE, "'", "APOSTROPHE" },
    { 0x35, DIK_GRAVE, "`", "GRAVE" },
    { 0x36, DIK_COMMA, ",", "COMMA" },
    { 0x37, DIK_PERIOD, ".", "PERIOD" },
    { 0x38, DIK_SLASH, "/", "SLASH" },
    { 0x39, DIK_CAPITAL, "Caps Lock", "CAPITAL" },
    { 0x3A, DIK_F1, "F1", "F1" },
    { 0x3B, DIK_F2, "F2", "F2" },
    { 0x3C, DIK_F3, "F3", "F3" },
    { 0x3D, DIK_F4, "F4", "F4" },
    { 0x3E, DIK_F5, "F5", "F5" },
    { 0x3F, DIK_F6, "F6", "F6" },
    { 0x40, DIK_F7, "F7", "F7" },
    { 0x41, DIK_F8, "F8", "F8" },
    { 0x42, DIK_F9, "F9", "F9" },
    { 0x43, DIK_F10, "F10", "F10" },
    { 0x44, DIK_F11, "F11", "F11" },
    { 0x45, DIK_F12, "F12", "F12" },
    { 0x46, DIK_SYSRQ, "Prnt Scrn", "SYSRQ" },
    { 0x47, DIK_SCROLL, "Scroll Lock", "SCROLL" },
    { 0x48, DIK_PAUSE, "Pause", "PAUSE" },
    { 0x49, DIK_INSERT, "Insert", "INSERT" },
    { 0x4A, DIK_HOME, "Home", "HOME" },
    { 0x4B, DIK_PRIOR, "Page Up", "PRIOR" },
    { 0x4C, DIK_DELETE, "Delete", "DELETE" },
    { 0x4D, DIK_END, "End", "END" },
    { 0x4E, DIK_NEXT, "Page Down", "NEXT" },
    { 0x4F, DIK_RIGHT, "Right", "RIGHT" },
    { 0x50, DIK_LEFT, "Left", "LEFT" },
    { 0x51, DIK_DOWN, "Down", "DOWN" },
    { 0x52, DIK_UP, "Up", "UP" },
    { 0x53, DIK_NUMLOCK, "Num Lock", "NUMLOCK" }, // the Mac's Clear key
    { 0x54, DIK_DIVIDE, "Num /", "DIVIDE" },
    { 0x55, DIK_MULTIPLY, "Num *", "MULTIPLY" },
    { 0x56, DIK_SUBTRACT, "Num -", "SUBTRACT" },
    { 0x57, DIK_ADD, "Num +", "ADD" },
    { 0x58, DIK_NUMPADENTER, "Num Enter", "NUMPADENTER" },
    { 0x59, DIK_NUMPAD1, "Num 1", "NUMPAD1" },
    { 0x5A, DIK_NUMPAD2, "Num 2", "NUMPAD2" },
    { 0x5B, DIK_NUMPAD3, "Num 3", "NUMPAD3" },
    { 0x5C, DIK_NUMPAD4, "Num 4", "NUMPAD4" },
    { 0x5D, DIK_NUMPAD5, "Num 5", "NUMPAD5" },
    { 0x5E, DIK_NUMPAD6, "Num 6", "NUMPAD6" },
    { 0x5F, DIK_NUMPAD7, "Num 7", "NUMPAD7" },
    { 0x60, DIK_NUMPAD8, "Num 8", "NUMPAD8" },
    { 0x61, DIK_NUMPAD9, "Num 9", "NUMPAD9" },
    { 0x62, DIK_NUMPAD0, "Num 0", "NUMPAD0" },
    { 0x63, DIK_DECIMAL, "Num Del", "DECIMAL" },
    { 0x64, DIK_OEM_102, "\\", "OEM_102" }, // the ISO key left of Z (the Mac's § key is 0x35 on ISO layouts)
    { 0x65, DIK_APPS, "Application", "APPS" },
    { 0x66, DIK_POWER, "Power", "POWER" },
    { 0x67, DIK_NUMPADEQUALS, "Num =", "NUMPADEQUALS" },
    { 0x68, DIK_F13, "F13", "F13" },
    { 0x69, DIK_F14, "F14", "F14" },
    { 0x6A, DIK_F15, "F15", "F15" },
    { 0x85, DIK_NUMPADCOMMA, "Num ,", "NUMPADCOMMA" },
    { 0x87, DIK_ABNT_C1, "Ro", "ABNT_C1" },     // International1
    { 0x88, DIK_KANA, "Kana", "KANA" },          // International2
    { 0x89, DIK_YEN, "Yen", "YEN" },             // International3
    { 0x8A, DIK_CONVERT, "Convert", "CONVERT" }, // International4
    { 0x8B, DIK_NOCONVERT, "No Convert", "NOCONVERT" },
    { 0xE0, DIK_LCONTROL, "Ctrl", "LCONTROL" },
    { 0xE1, DIK_LSHIFT, "Shift", "LSHIFT" },
    { 0xE2, DIK_LMENU, "Alt", "LMENU" },          // the Mac's left Option
    { 0xE3, DIK_LWIN, "Left Windows", "LWIN" },   // the Mac's left Command
    { 0xE4, DIK_RCONTROL, "Right Ctrl", "RCONTROL" },
    { 0xE5, DIK_RSHIFT, "Right Shift", "RSHIFT" },
    { 0xE6, DIK_RMENU, "Right Alt", "RMENU" },    // the Mac's right Option
    { 0xE7, DIK_RWIN, "Right Windows", "RWIN" },  // the Mac's right Command
};

#define HOST_NUMKEYS (sizeof(host_aKeys) / sizeof(host_aKeys[0]))

uint8_t J3DHost_HidToDik(uint32_t hidUsage)
{
    for ( size_t i = 0; i < HOST_NUMKEYS; i++ )
    {
        if ( host_aKeys[i].hid == hidUsage )
        {
            return host_aKeys[i].dik;
        }
    }
    return 0;
}

uint32_t J3DHost_DikToHid(uint8_t dik)
{
    for ( size_t i = 0; i < HOST_NUMKEYS; i++ )
    {
        if ( host_aKeys[i].dik == dik )
        {
            return host_aKeys[i].hid;
        }
    }
    return 0;
}

const char* J3DHost_DikName(uint8_t dik)
{
    for ( size_t i = 0; i < HOST_NUMKEYS; i++ )
    {
        if ( host_aKeys[i].dik == dik )
        {
            return host_aKeys[i].pName;
        }
    }
    return NULL;
}

uint8_t J3DHost_DikFromName(const char* pName)
{
    if ( !pName || !*pName )
    {
        return 0;
    }

    if ( strncasecmp(pName, "DIK_", 4) == 0 )
    {
        pName += 4;
    }

    for ( size_t i = 0; i < HOST_NUMKEYS; i++ )
    {
        if ( strcasecmp(host_aKeys[i].pName, pName) == 0 || strcasecmp(host_aKeys[i].pDikName, pName) == 0 )
        {
            return host_aKeys[i].dik;
        }
    }
    return 0;
}
