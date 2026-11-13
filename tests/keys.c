/*
 * keys: the keyboard tables and the Win32 calls built on them (PORT-PLAN.md P3.1, P3.7). Needs no game data.
 *
 *   - every HID usage the table names maps to a DIK code that maps back to a name, and the reverse lookups agree
 *   - the positions that matter to the game: letters, digits, arrows, the modifiers (Option is Alt, Command is the
 *     Windows key) and the extended keys (0x80 set) are where DirectInput had them
 *   - GetKeyNameTextA decodes a WM_KEYDOWN lParam (scan code and extended bit) as the controls dialog passes it
 *   - SearchPathA finds a file in a folder, adds the extension only when the name has none, and fails cleanly
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Windows.h>

#include <platform/common/host_keys.h>

static int keys_numFailed = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !(cond) )                                                                                                 \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++keys_numFailed;                                                                                          \
        }                                                                                                              \
    } while ( 0 )

static void keys_TestTables(void)
{
    // HID usages: A 0x04, 1 0x1E, Return 0x28, Up 0x52, left Ctrl 0xE0, left Option 0xE2, left Command 0xE3,
    // right Option 0xE6, keypad Enter 0x58, Delete 0x4C.
    CHECK(J3DHost_HidToDik(0x04) == DIK_A);
    CHECK(J3DHost_HidToDik(0x1D) == DIK_Z);
    CHECK(J3DHost_HidToDik(0x1E) == DIK_1);
    CHECK(J3DHost_HidToDik(0x27) == DIK_0);
    CHECK(J3DHost_HidToDik(0x28) == DIK_RETURN);
    CHECK(J3DHost_HidToDik(0x52) == DIK_UP);
    CHECK(J3DHost_HidToDik(0xE0) == DIK_LCONTROL);
    CHECK(J3DHost_HidToDik(0xE2) == DIK_LMENU);
    CHECK(J3DHost_HidToDik(0xE3) == DIK_LWIN);
    CHECK(J3DHost_HidToDik(0xE6) == DIK_RMENU);
    CHECK(J3DHost_HidToDik(0x58) == DIK_NUMPADENTER);
    CHECK(J3DHost_HidToDik(0x4C) == DIK_DELETE);
    CHECK(J3DHost_HidToDik(0x00) == 0);
    CHECK(J3DHost_HidToDik(0x80) == 0); // volume up: no DirectInput key

    // Every usage that maps comes back with a name, and its DIK code finds a usage that maps to the same code.
    int numMapped = 0;
    for ( uint32_t hid = 0; hid < 0x100; hid++ )
    {
        const uint8_t dik = J3DHost_HidToDik(hid);
        if ( !dik )
        {
            continue;
        }
        ++numMapped;
        CHECK(J3DHost_DikName(dik) != NULL);
        CHECK(J3DHost_HidToDik(J3DHost_DikToHid(dik)) == dik);
        CHECK(J3DHost_DikFromName(J3DHost_DikName(dik)) != 0);
    }
    CHECK(numMapped > 100);

    CHECK(strcmp(J3DHost_DikName(DIK_ESCAPE), "Esc") == 0);
    CHECK(strcmp(J3DHost_DikName(DIK_RCONTROL), "Right Ctrl") == 0);
    CHECK(strcmp(J3DHost_DikName(DIK_NUMPAD7), "Num 7") == 0);
    CHECK(J3DHost_DikName(0x00) == NULL);

    CHECK(J3DHost_DikFromName("UP") == DIK_UP);
    CHECK(J3DHost_DikFromName("dik_lcontrol") == DIK_LCONTROL);
    CHECK(J3DHost_DikFromName("Space") == DIK_SPACE);
    CHECK(J3DHost_DikFromName("no such key") == 0);
    CHECK(J3DHost_DikFromName("") == 0);
}

static void keys_TestGetKeyNameText(void)
{
    char aName[64];

    // As jonesConfig_ControlToString builds it: ((dik & 0x7F) | ((dik & 0x80) << 1)) << 16.
    CHECK(GetKeyNameTextA((LONG)(((DIK_Q & 0x7F) | ((DIK_Q & 0x80) << 1)) << 16), aName, sizeof(aName)) == 1);
    CHECK(strcmp(aName, "Q") == 0);

    CHECK(GetKeyNameTextA((LONG)(((DIK_RCONTROL & 0x7F) | ((DIK_RCONTROL & 0x80) << 1)) << 16), aName, sizeof(aName)) > 0);
    CHECK(strcmp(aName, "Right Ctrl") == 0);

    // Truncated to the buffer, always terminated.
    CHECK(GetKeyNameTextA((LONG)((DIK_BACK & 0x7F) << 16), aName, 5) == 4);
    CHECK(strcmp(aName, "Back") == 0);

    // A code no key has: empty, 0.
    CHECK(GetKeyNameTextA(0, aName, sizeof(aName)) == 0);
    CHECK(aName[0] == '\0');
}

static void keys_TestSearchPath(void)
{
    char aDir[] = "/tmp/j3d-keys-XXXXXX";
    CHECK(mkdtemp(aDir) != NULL);

    char aFile[256];
    snprintf(aFile, sizeof(aFile), "%s/My Keys.kfg", aDir);
    FILE* pFile = fopen(aFile, "w");
    CHECK(pFile != NULL);
    if ( pFile )
    {
        fputs("x\n", pFile);
        fclose(pFile);
    }

    char  aPath[1024];
    LPSTR pFilePart = NULL;
    const DWORD len = SearchPathA(aDir, "My Keys", ".kfg", sizeof(aPath), aPath, &pFilePart);
    CHECK(len == strlen(aFile));
    CHECK(strcmp(aPath, aFile) == 0);
    CHECK(pFilePart && strcmp(pFilePart, "My Keys.kfg") == 0);

    // Case-insensitive, with the engine's backslash separator and a trailing one on the folder.
    char aEngineDir[256];
    snprintf(aEngineDir, sizeof(aEngineDir), "%s\\", aDir);
    CHECK(SearchPathA(aEngineDir, "my keys", ".KFG", sizeof(aPath), aPath, NULL) > 0);

    // The extension is not added to a name that has one.
    CHECK(SearchPathA(aDir, "My Keys.kfg", ".txt", sizeof(aPath), aPath, NULL) > 0);

    // Too small a buffer: the size needed, nothing written.
    char aSmall[4] = "abc";
    CHECK(SearchPathA(aDir, "My Keys", ".kfg", sizeof(aSmall), aSmall, NULL) == strlen(aFile) + 1);
    CHECK(strcmp(aSmall, "abc") == 0);

    // Missing files, folders and arguments.
    CHECK(SearchPathA(aDir, "Other", ".kfg", sizeof(aPath), aPath, NULL) == 0);
    CHECK(SearchPathA("/no/such/folder", "My Keys", ".kfg", sizeof(aPath), aPath, NULL) == 0);
    CHECK(SearchPathA(NULL, "My Keys", ".kfg", sizeof(aPath), aPath, NULL) == 0);
    CHECK(SearchPathA(aDir, ".", NULL, sizeof(aPath), aPath, NULL) == 0); // a folder is not a file

    unlink(aFile);
    rmdir(aDir);
}

int main(void)
{
    keys_TestTables();
    keys_TestGetKeyNameText();
    keys_TestSearchPath();

    if ( keys_numFailed )
    {
        fprintf(stderr, "keys: %d check(s) failed\n", keys_numFailed);
        return EXIT_FAILURE;
    }
    printf("keys: all checks passed\n");
    return EXIT_SUCCESS;
}
