/*
 * dialogs: the Win32 dialog emulation (PORT-PLAN.md P6.3 to P6.10) on the game's own templates, with no game.
 *
 *   dialogs [data root]
 *
 * Every template of Jones3D/Indy3D.rc is opened with DialogBoxParam and a test hook (J3DGui_SetModalHook), which
 * checks its structure (every control created, inside the dialog's client area, a control with the focus) and ends
 * it. Then one dialog is driven through real input, as a player would: Tab moves the focus in template order, a click
 * toggles a check box, a radio button clears its group, a combo box drops down and takes a selection with the mouse,
 * a list box takes arrow keys, a trackbar follows a drag, Return presses the default button and Escape cancels; a
 * message box returns the button clicked. With a data root the icons must come from its Resource/Indy3D.exe.
 *
 * DIALOGS_DUMP=<dir> writes each dialog as a BMP, to look at: with the apple platform the text is Core Text's.
 */
#include <platform/common/j3dgui/gui_internal.h>
#include <platform/common/j3dgui/j3dgui.h>
#include <w32util/wuRegistry.h>

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int dialogs_failures = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            dialogs_failures++;                                                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static const char* dialogs_pDumpDir = NULL;

static void dialogs_WriteBmp(J3DGuiWindow* p, const char* pName)
{
    char aPath[1024];
    FILE* f;
    J3DGuiSurface* s = p->pSurface;
    int rowSize;
    unsigned char aHeader[54] = { 'B', 'M' };
    uint32_t fileSize;
    int y;
    if ( !dialogs_pDumpDir || !s )
    {
        return;
    }
    J3DGui_Paint(p);
    snprintf(aPath, sizeof(aPath), "%s/%s.bmp", dialogs_pDumpDir, pName);
    f = fopen(aPath, "wb");
    if ( !f )
    {
        return;
    }
    rowSize = s->width * 4;
    fileSize = 54u + (uint32_t)rowSize * (uint32_t)s->height;
    memcpy(aHeader + 2, &fileSize, 4);
    aHeader[10] = 54;
    aHeader[14] = 40;
    memcpy(aHeader + 18, &s->width, 4);
    {
        int h = -s->height; // top-down
        memcpy(aHeader + 22, &h, 4);
    }
    aHeader[26] = 1;
    aHeader[28] = 32;
    fwrite(aHeader, 1, sizeof(aHeader), f);
    for ( y = 0; y < s->height; ++y )
    {
        fwrite(s->pPixels + (size_t)y * (size_t)s->pitch, 4, (size_t)s->width, f);
    }
    fclose(f);
}

/* ---- Every template ---------------------------------------------------------------------------------------------------- */

static const J3DGuiTemplate* dialogs_pTemplate;

static BOOL CALLBACK dialogs_PlainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hwnd;
    (void)wParam;
    (void)lParam;
    return msg == WM_INITDIALOG;
}

static void dialogs_CheckHook(HWND hDlg, int iteration)
{
    J3DGuiWindow* p = J3DGui_FromHwnd(hDlg);
    J3DGuiWindow* q;
    RECT client;
    int n = 0;
    char aName[32];
    (void)iteration;

    GetClientRect(hDlg, &client);
    for ( q = p->pFirstChild; q; q = q->pNext )
    {
        RECT r = q->rect;
        n++;
        // controls lie inside the client area (a few retail templates overhang it by a dialog unit or two)
        CHECK(r.left >= -2 && r.top >= -2 && r.right <= client.right + 4 && r.bottom <= client.bottom + 4, "dialog %d: control %d at %ld,%ld-%ld,%ld outside %ldx%ld",
              dialogs_pTemplate->id, q->id, (long)r.left, (long)r.top, (long)r.right, (long)r.bottom, (long)client.right, (long)client.bottom);
    }
    CHECK(n == dialogs_pTemplate->numItems, "dialog %d: %d controls, the template has %d", dialogs_pTemplate->id, n, dialogs_pTemplate->numItems);
    snprintf(aName, sizeof(aName), "dialog-%d", dialogs_pTemplate->id);
    dialogs_WriteBmp(p, aName);
    EndDialog(hDlg, 1234);
}

static void dialogs_AllTemplates(void)
{
    int i;
    J3DGui_SetModalHook(dialogs_CheckHook);
    for ( i = 0; i < J3DGui_numTemplates; ++i )
    {
        INT_PTR result;
        dialogs_pTemplate = &J3DGui_aTemplates[i];
        result = DialogBoxParamA(NULL, MAKEINTRESOURCEA(dialogs_pTemplate->id), NULL, dialogs_PlainProc, 0);
        CHECK(result == 1234, "dialog %d: DialogBoxParam returned %ld", dialogs_pTemplate->id, (long)result);
    }
    J3DGui_SetModalHook(NULL);
}

/* ---- Driving the launcher (template 101) ---------------------------------------------------------------------------- */

static int dialogs_commands[64];
static int dialogs_numCommands = 0;
static int dialogs_step = 0;
static int dialogs_scrolls = 0;

static BOOL CALLBACK dialogs_LauncherProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch ( msg )
    {
        case WM_INITDIALOG:
        {
            static const char* aLevels[] = { "00_cyn", "01_bab", "02_riv", "03_shs", "05_tem", "06_jun" };
            int k;
            for ( k = 0; k < 6; ++k )
            {
                SendDlgItemMessageA(hwnd, 1001, LB_ADDSTRING, 0, (LPARAM)aLevels[5 - k]); // LBS_SORT puts them in order
            }
            for ( k = 0; k < 3; ++k )
            {
                int index = (int)SendDlgItemMessageA(hwnd, 1029, CB_ADDSTRING, 0, (LPARAM)(k == 0 ? "640 x 480" : k == 1 ? "800 x 600" : "1024 x 768"));
                SendDlgItemMessageA(hwnd, 1029, CB_SETITEMDATA, (WPARAM)index, 100 + k);
            }
            SendDlgItemMessageA(hwnd, 1029, CB_SETCURSEL, 0, 0);
            CheckRadioButton(hwnd, 1013, 1015, 1013);
            return TRUE;
        }
        case WM_COMMAND:
            if ( dialogs_numCommands < 64 )
            {
                dialogs_commands[dialogs_numCommands++] = (int)wParam;
            }
            if ( LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL )
            {
                EndDialog(hwnd, LOWORD(wParam));
            }
            return TRUE;
        case WM_HSCROLL:
            dialogs_scrolls++;
            return TRUE;
        default:
            return FALSE;
    }
}

static int dialogs_SawCommand(int id, int code)
{
    int k;
    for ( k = 0; k < dialogs_numCommands; ++k )
    {
        if ( dialogs_commands[k] == MAKEWPARAM(id, code) )
        {
            return 1;
        }
    }
    return 0;
}

static void dialogs_Click(HWND hCtl, int x, int y)
{
    RECT r;
    GetWindowRect(hCtl, &r);
    J3DGui_Input(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(r.left + x, r.top + y));
    J3DGui_Input(WM_LBUTTONUP, 0, MAKELPARAM(r.left + x, r.top + y));
}

static void dialogs_Key(WPARAM vk)
{
    J3DGui_Input(WM_KEYDOWN, vk, 1);
    J3DGui_Input(WM_KEYUP, vk, (LPARAM)(1u | (3u << 30)));
}

static void dialogs_LauncherHook(HWND hDlg, int iteration)
{
    HWND hList = GetDlgItem(hDlg, 1001);
    HWND hCombo = GetDlgItem(hDlg, 1029);
    HWND hCheck = GetDlgItem(hDlg, 1002);
    (void)iteration;

    switch ( dialogs_step++ )
    {
        case 0:
            // the focus starts on the first tab stop, the "Start Game" button; Tab goes on in template order
            CHECK(GetFocus() == GetDlgItem(hDlg, 1), "first focus %p, expected Start Game", (void*)GetFocus());
            dialogs_Key(VK_TAB);
            CHECK(GetFocus() == GetDlgItem(hDlg, 2), "after Tab the focus is %p, expected Exit", (void*)GetFocus());
            dialogs_Key(VK_TAB);
            CHECK(GetFocus() == GetDlgItem(hDlg, 1030), "after two Tabs the focus is %p, expected the 3D device list", (void*)GetFocus());
            J3DGui_Input(WM_KEYDOWN, VK_SHIFT, 1);
            dialogs_Key(VK_TAB);
            J3DGui_Input(WM_KEYUP, VK_SHIFT, (LPARAM)(1u | (3u << 30)));
            CHECK(GetFocus() == GetDlgItem(hDlg, 2), "Shift+Tab went to %p, expected Exit", (void*)GetFocus());
            break;
        case 1:
            // a click on the check box toggles it and notifies
            CHECK(!IsDlgButtonChecked(hDlg, 1002), "Open in a Window starts checked");
            dialogs_Click(hCheck, 4, 4);
            CHECK(IsDlgButtonChecked(hDlg, 1002) == BST_CHECKED, "a click did not check Open in a Window");
            CHECK(dialogs_SawCommand(1002, BN_CLICKED), "no BN_CLICKED from the check box");
            CHECK(GetFocus() == hCheck, "the click did not focus the check box");
            break;
        case 2:
            // automatic radio buttons: a click checks one and clears the rest of its group
            dialogs_Click(GetDlgItem(hDlg, 1015), 3, 3);
            CHECK(IsDlgButtonChecked(hDlg, 1015) && !IsDlgButtonChecked(hDlg, 1013) && !IsDlgButtonChecked(hDlg, 1014), "radio group 1013-1015: %u %u %u",
                  IsDlgButtonChecked(hDlg, 1013), IsDlgButtonChecked(hDlg, 1014), IsDlgButtonChecked(hDlg, 1015));
            break;
        case 3:
        {
            // the drop-down list: a click opens it, a click on its third item selects it and closes it
            J3DGuiWindow* ap[64];
            int n;
            RECT r;
            dialogs_Click(hCombo, 10, 5);
            n = J3DGui_GetTopLevels(ap, 64);
            CHECK(n >= 2 && ap[n - 1]->cls == GUI_CLASS_COMBOLBOX, "the combo box did not drop down");
            CHECK(dialogs_SawCommand(1029, CBN_DROPDOWN), "no CBN_DROPDOWN");
            if ( n >= 2 && ap[n - 1]->cls == GUI_CLASS_COMBOLBOX )
            {
                int itemH = J3DGui_FontHeight(J3DGui_GetFont(J3DGui_FromHwnd(hCombo)));
                dialogs_WriteBmp(J3DGui_FromHwnd(hDlg), "launcher-dropped");
                dialogs_WriteBmp(ap[n - 1], "launcher-droplist");
                GetWindowRect(J3DGui_ToHwnd(ap[n - 1]), &r);
                J3DGui_Input(WM_MOUSEMOVE, 0, MAKELPARAM(r.left + 10, r.top + 1 + 2 * itemH + 2));
                J3DGui_Input(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(r.left + 10, r.top + 1 + 2 * itemH + 2));
                J3DGui_Input(WM_LBUTTONUP, 0, MAKELPARAM(r.left + 10, r.top + 1 + 2 * itemH + 2));
            }
            CHECK(SendMessageA(hCombo, CB_GETCURSEL, 0, 0) == 2, "the combo box selected %ld, expected 2", (long)SendMessageA(hCombo, CB_GETCURSEL, 0, 0));
            CHECK(SendMessageA(hCombo, CB_GETITEMDATA, 2, 0) == 102, "item data %ld", (long)SendMessageA(hCombo, CB_GETITEMDATA, 2, 0));
            CHECK(dialogs_SawCommand(1029, CBN_SELCHANGE) && dialogs_SawCommand(1029, CBN_CLOSEUP), "no CBN_SELCHANGE/CBN_CLOSEUP");
            CHECK(J3DGui_GetTopLevels(ap, 64) == 1, "the drop-down is still open");
            break;
        }
        case 4:
        {
            // the sorted list box takes clicks and arrow keys
            char aText[32] = { 0 };
            dialogs_Click(hList, 10, 2);
            SendMessageA(hList, LB_GETTEXT, 0, (LPARAM)aText);
            CHECK(strcmp(aText, "00_cyn") == 0, "LBS_SORT: the first item is %s", aText);
            CHECK(SendMessageA(hList, LB_GETCURSEL, 0, 0) == 0, "a click selected %ld", (long)SendMessageA(hList, LB_GETCURSEL, 0, 0));
            dialogs_Key(VK_DOWN);
            dialogs_Key(VK_DOWN);
            CHECK(SendMessageA(hList, LB_GETCURSEL, 0, 0) == 2, "two Downs selected %ld", (long)SendMessageA(hList, LB_GETCURSEL, 0, 0));
            CHECK(dialogs_SawCommand(1001, LBN_SELCHANGE), "no LBN_SELCHANGE");
            dialogs_WriteBmp(J3DGui_FromHwnd(hDlg), "launcher-driven");
            break;
        }
        default:
            // Return presses the default button (the list box does not want Return)
            dialogs_Key(VK_RETURN);
            break;
    }
}

static void dialogs_Launcher(void)
{
    INT_PTR result;
    J3DGui_SetModalHook(dialogs_LauncherHook);
    result = DialogBoxParamA(NULL, MAKEINTRESOURCEA(101), NULL, dialogs_LauncherProc, 0);
    J3DGui_SetModalHook(NULL);
    CHECK(result == IDOK, "Return ended the launcher with %ld, expected IDOK", (long)result);
    CHECK(dialogs_step >= 6, "the launcher ended after step %d", dialogs_step);
}

/* ---- A trackbar, Escape -------------------------------------------------------------------------------------------- */

static int dialogs_trackStep = 0;

static BOOL CALLBACK dialogs_SoundProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch ( msg )
    {
        case WM_INITDIALOG:
            SendDlgItemMessageA(hwnd, 1050, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
            SendDlgItemMessageA(hwnd, 1050, TBM_SETPOS, TRUE, 5);
            return TRUE;
        case WM_HSCROLL:
            dialogs_scrolls++;
            return TRUE;
        case WM_COMMAND:
            if ( LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL )
            {
                EndDialog(hwnd, LOWORD(wParam));
            }
            return TRUE;
        default:
            return FALSE;
    }
}

static void dialogs_SoundHook(HWND hDlg, int iteration)
{
    HWND hTrack = GetDlgItem(hDlg, 1050);
    RECT thumb;
    RECT r;
    (void)iteration;
    if ( dialogs_trackStep++ == 0 )
    {
        GetWindowRect(hTrack, &r);
        SendMessageA(hTrack, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
        // drag the thumb to the right end
        J3DGui_Input(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(r.left + (thumb.left + thumb.right) / 2, r.top + 5));
        J3DGui_Input(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(r.right + 50, r.top + 5));
        J3DGui_Input(WM_LBUTTONUP, 0, MAKELPARAM(r.right + 50, r.top + 5));
        CHECK(SendMessageA(hTrack, TBM_GETPOS, 0, 0) == 10, "the drag left the trackbar at %ld", (long)SendMessageA(hTrack, TBM_GETPOS, 0, 0));
        CHECK(dialogs_scrolls >= 2, "the trackbar sent %d WM_HSCROLL", dialogs_scrolls);
        dialogs_Key(VK_LEFT);
        CHECK(SendMessageA(hTrack, TBM_GETPOS, 0, 0) == 9, "Left moved the trackbar to %ld", (long)SendMessageA(hTrack, TBM_GETPOS, 0, 0));
        dialogs_WriteBmp(J3DGui_FromHwnd(hDlg), "sound-driven");
    }
    else
    {
        dialogs_Key(VK_ESCAPE);
    }
}

static void dialogs_Trackbar(void)
{
    INT_PTR result;
    J3DGui_SetModalHook(dialogs_SoundHook);
    result = DialogBoxParamA(NULL, MAKEINTRESOURCEA(113), NULL, dialogs_SoundProc, 0);
    J3DGui_SetModalHook(NULL);
    CHECK(result == IDCANCEL, "Escape ended the sound dialog with %ld", (long)result);
}

/* ---- A message box ------------------------------------------------------------------------------------------------- */

static void dialogs_MessageBoxHook(HWND hDlg, int iteration)
{
    (void)iteration;
    dialogs_WriteBmp(J3DGui_FromHwnd(hDlg), "messagebox");
    dialogs_Click(GetDlgItem(hDlg, IDNO), 10, 5);
}

static void dialogs_MessageBox(void)
{
    int result;
    J3DGui_SetModalHook(dialogs_MessageBoxHook);
    result = MessageBoxA(NULL, "The game data could not be found. Do you want to look for it?", "Indiana Jones and the Infernal Machine", MB_YESNO);
    J3DGui_SetModalHook(NULL);
    CHECK(result == IDNO, "the message box returned %d, expected IDNO", result);
}

int main(int argc, char** argv)
{
    harness_InstallHandlers();
    if ( argc > 1 )
    {
        char aRoot[1024];
        snprintf(aRoot, sizeof(aRoot), "%s/", argv[1]);
        setenv("J3D_DATA", aRoot, 1);
    }
    harness_StartupStd();
    wuRegistry_Startup(HKEY_LOCAL_MACHINE, "Software\\LucasArts Entertainment Company LLC\\Indiana Jones and the Infernal Machine\\v1.0");
    dialogs_pDumpDir = getenv("DIALOGS_DUMP");

    dialogs_AllTemplates();
    dialogs_Launcher();
    dialogs_Trackbar();
    dialogs_MessageBox();
    if ( argc > 1 )
    {
        HICON hIcon = LoadIconA(NULL, MAKEINTRESOURCEA(145));
        ICONINFO info;
        BITMAP bm;
        CHECK(hIcon && GetIconInfo(hIcon, &info), "icon 145 did not load");
        if ( hIcon && GetIconInfo(hIcon, &info) )
        {
            GetObjectA(info.hbmColor, sizeof(bm), &bm);
            CHECK(bm.bmWidth == 48 || bm.bmWidth == 64, "icon 145 is %ld pixels wide: not the executable's", (long)bm.bmWidth);
            DeleteObject(info.hbmColor);
            DeleteObject(info.hbmMask);
        }
    }

    if ( dialogs_failures )
    {
        printf("dialogs: %d check(s) failed\n", dialogs_failures);
        return 1;
    }
    printf("dialogs: %d templates built and checked; Tab, clicks, a drop-down, a list, a trackbar, Return, Escape and a message box "
           "behave\n", J3DGui_numTemplates);
    return 0;
}
