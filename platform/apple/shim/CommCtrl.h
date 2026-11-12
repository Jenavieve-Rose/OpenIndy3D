/*
 * Port stand-in for <commctrl.h>: the list-view wrappers the game shell uses, defined as the SDK defines them.
 * Types and message numbers are in win32_shim_gui.h.
 */
#pragma once
#include "win32_shim.h"

#define ListView_DeleteAllItems(h)        (BOOL)SendMessage((h), LVM_DELETEALLITEMS, 0, 0L)
#define ListView_DeleteItem(h, i)         (BOOL)SendMessage((h), LVM_DELETEITEM, (WPARAM)(int)(i), 0L)
#define ListView_GetColumnWidth(h, c)     (int)SendMessage((h), LVM_GETCOLUMNWIDTH, (WPARAM)(int)(c), 0)
#define ListView_SetColumnWidth(h, c, w)  (BOOL)SendMessage((h), LVM_SETCOLUMNWIDTH, (WPARAM)(int)(c), MAKELPARAM((w), 0))
#define ListView_GetCountPerPage(h)       (int)SendMessage((h), LVM_GETCOUNTPERPAGE, 0, 0)
#define ListView_GetImageList(h, i)       (HIMAGELIST)SendMessage((h), LVM_GETIMAGELIST, (WPARAM)(INT)(i), 0L)
#define ListView_SetImageList(h, l, i)    (HIMAGELIST)SendMessage((h), LVM_SETIMAGELIST, (WPARAM)(i), (LPARAM)(HIMAGELIST)(l))
#define ListView_GetItem(h, p)            (BOOL)SendMessage((h), LVM_GETITEM, 0, (LPARAM)(LV_ITEM*)(p))
#define ListView_SetItem(h, p)            (BOOL)SendMessage((h), LVM_SETITEM, 0, (LPARAM)(const LV_ITEM*)(p))
#define ListView_InsertItem(h, p)         (int)SendMessage((h), LVM_INSERTITEM, 0, (LPARAM)(const LV_ITEM*)(p))
#define ListView_GetItemCount(h)          (int)SendMessage((h), LVM_GETITEMCOUNT, 0, 0L)
#define ListView_GetNextItem(h, i, f)     (int)SendMessage((h), LVM_GETNEXTITEM, (WPARAM)(int)(i), MAKELPARAM((f), 0))
#define ListView_GetSelectedCount(h)      (UINT)SendMessage((h), LVM_GETSELECTEDCOUNT, 0, 0L)
#define ListView_InsertColumn(h, i, p)    (int)SendMessage((h), LVM_INSERTCOLUMN, (WPARAM)(int)(i), (LPARAM)(const LV_COLUMN*)(p))
#define ListView_EnsureVisible(h, i, p)   (BOOL)SendMessage((h), LVM_ENSUREVISIBLE, (WPARAM)(int)(i), MAKELPARAM((p), 0))
#define ListView_GetItemRect(h, i, prc, code) \
    (BOOL)SendMessage((h), LVM_GETITEMRECT, (WPARAM)(int)(i), \
                      ((prc) ? (((RECT*)(prc))->left = (code), (LPARAM)(RECT*)(prc)) : (LPARAM)(RECT*)NULL))
#define ListView_SetItemState(h, i, data, mask) \
    do { LV_ITEM _lvi; _lvi.stateMask = (mask); _lvi.state = (data); \
         SendMessage((h), LVM_SETITEMSTATE, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_lvi); } while (0)
#define ListView_GetItemText(h, i, sub, text, max) \
    do { LV_ITEM _lvi; _lvi.iSubItem = (sub); _lvi.cchTextMax = (max); _lvi.pszText = (text); \
         SendMessage((h), LVM_GETITEMTEXT, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_lvi); } while (0)
#define ListView_SetItemText(h, i, sub, text) \
    do { LV_ITEM _lvi; _lvi.iSubItem = (sub); _lvi.pszText = (text); \
         SendMessage((h), LVM_SETITEMTEXT, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_lvi); } while (0)
