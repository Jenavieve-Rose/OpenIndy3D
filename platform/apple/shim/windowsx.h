/*
 * Port stand-in for <windowsx.h>: the control message wrappers the game shell uses, defined exactly as the SDK
 * defines them (on top of SendMessage), so the Phase 6 dialog emulation only has to implement the messages.
 */
#pragma once
#include "win32_shim.h"

#define Button_GetCheck(h)              ((int)(DWORD)SendMessage((h), BM_GETCHECK, 0, 0))
#define Button_SetCheck(h, c)           ((void)SendMessage((h), BM_SETCHECK, (WPARAM)(int)(c), 0))
#define Button_GetState(h)              ((int)(DWORD)SendMessage((h), BM_GETSTATE, 0, 0))
#define Button_SetState(h, s)           ((UINT)(DWORD)SendMessage((h), BM_SETSTATE, (WPARAM)(int)(s), 0))
#define Button_Enable(h, f)             EnableWindow((h), (f))

#define Edit_GetText(h, s, n)           GetWindowText((h), (s), (n))
#define Edit_SetText(h, s)              SetWindowText((h), (s))
#define Edit_GetTextLength(h)           GetWindowTextLength(h)
#define Edit_LimitText(h, n)            ((void)SendMessage((h), EM_LIMITTEXT, (WPARAM)(n), 0))
#define Edit_SetSel(h, a, b)            ((void)SendMessage((h), EM_SETSEL, (a), (b)))

#define Static_GetIcon(h, i)            ((HICON)(UINT_PTR)SendMessage((h), STM_GETICON, 0, 0))
#define Static_SetIcon(h, i)            ((HICON)(UINT_PTR)SendMessage((h), STM_SETICON, (WPARAM)(HICON)(i), 0))

#define ListBox_AddString(h, s)         ((int)(DWORD)SendMessage((h), LB_ADDSTRING, 0, (LPARAM)(LPCSTR)(s)))
#define ListBox_InsertString(h, i, s)   ((int)(DWORD)SendMessage((h), LB_INSERTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ListBox_DeleteString(h, i)      ((int)(DWORD)SendMessage((h), LB_DELETESTRING, (WPARAM)(int)(i), 0))
#define ListBox_ResetContent(h)         ((BOOL)(DWORD)SendMessage((h), LB_RESETCONTENT, 0, 0))
#define ListBox_GetCount(h)             ((int)(DWORD)SendMessage((h), LB_GETCOUNT, 0, 0))
#define ListBox_GetCurSel(h)            ((int)(DWORD)SendMessage((h), LB_GETCURSEL, 0, 0))
#define ListBox_SetCurSel(h, i)         ((int)(DWORD)SendMessage((h), LB_SETCURSEL, (WPARAM)(int)(i), 0))
#define ListBox_GetText(h, i, s)        ((int)(DWORD)SendMessage((h), LB_GETTEXT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ListBox_GetTextLen(h, i)        ((int)(DWORD)SendMessage((h), LB_GETTEXTLEN, (WPARAM)(int)(i), 0))
#define ListBox_GetItemData(h, i)       ((LRESULT)(ULONG_PTR)SendMessage((h), LB_GETITEMDATA, (WPARAM)(int)(i), 0))
#define ListBox_SetItemData(h, i, d)    ((int)(DWORD)SendMessage((h), LB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(d)))
#define ListBox_FindString(h, i, s)     ((int)(DWORD)SendMessage((h), LB_FINDSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ListBox_SetHorizontalExtent(h, e) ((void)SendMessage((h), LB_SETHORIZONTALEXTENT, (WPARAM)(int)(e), 0))

#define ComboBox_AddString(h, s)        ((int)(DWORD)SendMessage((h), CB_ADDSTRING, 0, (LPARAM)(LPCSTR)(s)))
#define ComboBox_InsertString(h, i, s)  ((int)(DWORD)SendMessage((h), CB_INSERTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ComboBox_DeleteString(h, i)     ((int)(DWORD)SendMessage((h), CB_DELETESTRING, (WPARAM)(int)(i), 0))
#define ComboBox_ResetContent(h)        ((int)(DWORD)SendMessage((h), CB_RESETCONTENT, 0, 0))
#define ComboBox_GetCount(h)            ((int)(DWORD)SendMessage((h), CB_GETCOUNT, 0, 0))
#define ComboBox_GetCurSel(h)           ((int)(DWORD)SendMessage((h), CB_GETCURSEL, 0, 0))
#define ComboBox_SetCurSel(h, i)        ((int)(DWORD)SendMessage((h), CB_SETCURSEL, (WPARAM)(int)(i), 0))
#define ComboBox_GetLBText(h, i, s)     ((int)(DWORD)SendMessage((h), CB_GETLBTEXT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ComboBox_GetItemData(h, i)      ((LRESULT)(ULONG_PTR)SendMessage((h), CB_GETITEMDATA, (WPARAM)(int)(i), 0))
#define ComboBox_SetItemData(h, i, d)   ((int)(DWORD)SendMessage((h), CB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(d)))
#define ComboBox_FindString(h, i, s)    ((int)(DWORD)SendMessage((h), CB_FINDSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ComboBox_FindStringExact(h, i, s) ((int)(DWORD)SendMessage((h), CB_FINDSTRINGEXACT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))
#define ComboBox_SelectString(h, i, s)  ((int)(DWORD)SendMessage((h), CB_SELECTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(s)))

#define GetWindowInstance(h)            ((HMODULE)GetWindowLongPtr((h), GWL_HINSTANCE))
#define SubclassWindow(h, p)            ((WNDPROC)SetWindowLongPtr((h), GWL_WNDPROC, (LONG_PTR)(WNDPROC)(p)))
