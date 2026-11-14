#ifndef J3D_PLATFORM_WIN32_SHIM_GUI_H
#define J3D_PLATFORM_WIN32_SHIM_GUI_H
/*
 * user32 / gdi32 / comctl32 / comdlg32 / shell32 / ole32 vocabulary used by the game shell (jonesConfig.c,
 * JonesDialog.c, JonesDevDialog.c, JonesMain.c). Part of win32_shim.h; see that file for the rules.
 *
 * Phase 6 replaces the null implementations with the j3dgui dialog emulation (PORT-PLAN.md 5.6), which is why the
 * message and control constants keep their real Win32 values: the dialog code switches on them.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef LRESULT (CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
// The 1999 SDK signature (BOOL), which the game's dialog procedures are written against.
typedef BOOL    (CALLBACK* DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void    (CALLBACK* TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef BOOL    (CALLBACK* WNDENUMPROC)(HWND, LPARAM);

/* ----------------------------------------------------------------------------------------------------------------
 * Structures
 * -------------------------------------------------------------------------------------------------------------- */
typedef struct tagMSG
{
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *PMSG, *LPMSG;

typedef struct tagPAINTSTRUCT
{
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *PPAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct tagMINMAXINFO
{
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
} MINMAXINFO, *PMINMAXINFO, *LPMINMAXINFO;

typedef struct tagWINDOWPOS
{
    HWND hwnd;
    HWND hwndInsertAfter;
    int  x, y, cx, cy;
    UINT flags;
} WINDOWPOS, *LPWINDOWPOS, *PWINDOWPOS;

typedef struct tagCREATESTRUCTA
{
    LPVOID    lpCreateParams;
    HINSTANCE hInstance;
    HMENU     hMenu;
    HWND      hwndParent;
    int       cy, cx, y, x;
    LONG      style;
    LPCSTR    lpszName;
    LPCSTR    lpszClass;
    DWORD     dwExStyle;
} CREATESTRUCTA, CREATESTRUCT, *LPCREATESTRUCTA, *LPCREATESTRUCT;

typedef struct tagWNDCLASSA
{
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
} WNDCLASSA, WNDCLASS, *LPWNDCLASSA, *LPWNDCLASS;

typedef struct tagWNDCLASSEXA
{
    UINT      cbSize;
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXA, WNDCLASSEX, *LPWNDCLASSEXA, *LPWNDCLASSEX;

typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD, *LPRGBQUAD;

typedef struct tagBITMAPINFOHEADER
{
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER
{
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1;
    WORD  bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER, *LPBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAP
{
    LONG   bmType;
    LONG   bmWidth;
    LONG   bmHeight;
    LONG   bmWidthBytes;
    WORD   bmPlanes;
    WORD   bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

typedef struct _ICONINFO
{
    BOOL    fIcon;
    DWORD   xHotspot;
    DWORD   yHotspot;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
} ICONINFO, *PICONINFO;

typedef struct tagTEXTMETRICA
{
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth;
    LONG tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    BYTE tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICA, TEXTMETRIC, *LPTEXTMETRICA, *LPTEXTMETRIC;

typedef struct _ABC { int abcA; UINT abcB; int abcC; } ABC, *PABC, *LPABC;

#define LF_FACESIZE 32
typedef struct tagLOGFONTA
{
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, LOGFONT, *LPLOGFONTA, *LPLOGFONT;

typedef UINT (CALLBACK* LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM); // 1999 SDK signature
typedef struct tagOFNA
{
    DWORD         lStructSize;
    HWND          hwndOwner;
    HINSTANCE     hInstance;
    LPCSTR        lpstrFilter;
    LPSTR         lpstrCustomFilter;
    DWORD         nMaxCustFilter;
    DWORD         nFilterIndex;
    LPSTR         lpstrFile;
    DWORD         nMaxFile;
    LPSTR         lpstrFileTitle;
    DWORD         nMaxFileTitle;
    LPCSTR        lpstrInitialDir;
    LPCSTR        lpstrTitle;
    DWORD         Flags;
    WORD          nFileOffset;
    WORD          nFileExtension;
    LPCSTR        lpstrDefExt;
    LPARAM        lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCSTR        lpTemplateName;
} OPENFILENAMEA, OPENFILENAME, *LPOPENFILENAMEA, *LPOPENFILENAME;

typedef struct _SHELLEXECUTEINFOA
{
    DWORD     cbSize;
    ULONG     fMask;
    HWND      hwnd;
    LPCSTR    lpVerb;
    LPCSTR    lpFile;
    LPCSTR    lpParameters;
    LPCSTR    lpDirectory;
    int       nShow;
    HINSTANCE hInstApp;
    void*     lpIDList;
    LPCSTR    lpClass;
    HKEY      hkeyClass;
    DWORD     dwHotKey;
    HANDLE    hIcon;
    HANDLE    hProcess;
} SHELLEXECUTEINFOA, SHELLEXECUTEINFO, *LPSHELLEXECUTEINFOA, *LPSHELLEXECUTEINFO;
#define SEE_MASK_NOCLOSEPROCESS 0x00000040
#define STILL_ACTIVE 0x00000103L

typedef struct tagNMHDR
{
    HWND     hwndFrom;
    UINT_PTR idFrom;
    UINT     code;
} NMHDR, *LPNMHDR;

typedef struct tagDRAWITEMSTRUCT
{
    UINT      CtlType;
    UINT      CtlID;
    UINT      itemID;
    UINT      itemAction;
    UINT      itemState;
    HWND      hwndItem;
    HDC       hDC;
    RECT      rcItem;
    ULONG_PTR itemData;
} DRAWITEMSTRUCT, *PDRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

typedef struct tagMEASUREITEMSTRUCT
{
    UINT      CtlType;
    UINT      CtlID;
    UINT      itemID;
    UINT      itemWidth;
    UINT      itemHeight;
    ULONG_PTR itemData;
} MEASUREITEMSTRUCT, *PMEASUREITEMSTRUCT, *LPMEASUREITEMSTRUCT;

typedef struct tagLVITEMA
{
    UINT   mask;
    int    iItem;
    int    iSubItem;
    UINT   state;
    UINT   stateMask;
    LPSTR  pszText;
    int    cchTextMax;
    int    iImage;
    LPARAM lParam;
    int    iIndent;
} LVITEMA, LVITEM, LV_ITEM, *LPLVITEMA, *LPLVITEM;

typedef struct tagLVCOLUMNA
{
    UINT  mask;
    int   fmt;
    int   cx;
    LPSTR pszText;
    int   cchTextMax;
    int   iSubItem;
} LVCOLUMNA, LVCOLUMN, LV_COLUMN, *LPLVCOLUMNA, *LPLVCOLUMN;

typedef struct tagNMLISTVIEW
{
    NMHDR  hdr;
    int    iItem;
    int    iSubItem;
    UINT   uNewState;
    UINT   uOldState;
    UINT   uChanged;
    POINT  ptAction;
    LPARAM lParam;
} NMLISTVIEW, NM_LISTVIEW, *LPNMLISTVIEW;

typedef struct tagLVKEYDOWN
{
    NMHDR hdr;
    WORD  wVKey;
    UINT  flags;
} NMLVKEYDOWN, LV_KEYDOWN, *LPNMLVKEYDOWN;

typedef struct tagNMITEMACTIVATE
{
    NMHDR  hdr;
    int    iItem;
    int    iSubItem;
    UINT   uNewState;
    UINT   uOldState;
    UINT   uChanged;
    POINT  ptAction;
    LPARAM lParam;
    UINT   uKeyFlags;
} NMITEMACTIVATE, *LPNMITEMACTIVATE;

typedef struct tagLVFINDINFOA
{
    UINT   flags;
    LPCSTR psz;
    LPARAM lParam;
    POINT  pt;
    UINT   vkDirection;
} LVFINDINFOA, LVFINDINFO, *LPLVFINDINFOA;

typedef struct tagTCITEMA
{
    UINT   mask;
    DWORD  dwState;
    DWORD  dwStateMask;
    LPSTR  pszText;
    int    cchTextMax;
    int    iImage;
    LPARAM lParam;
} TCITEMA, TCITEM, TC_ITEM, *LPTCITEMA, *LPTCITEM;

typedef struct tagINITCOMMONCONTROLSEX { DWORD dwSize; DWORD dwICC; } INITCOMMONCONTROLSEX, *LPINITCOMMONCONTROLSEX;

typedef struct tagSCROLLINFO
{
    UINT cbSize, fMask;
    int  nMin, nMax;
    UINT nPage;
    int  nPos, nTrackPos;
} SCROLLINFO, *LPSCROLLINFO;

typedef struct tagDLGTEMPLATE
{
    DWORD style;
    DWORD dwExtendedStyle;
    WORD  cdit;
    short x, y, cx, cy;
} DLGTEMPLATE, *LPDLGTEMPLATE, *LPDLGTEMPLATEA;
typedef const DLGTEMPLATE* LPCDLGTEMPLATE, *LPCDLGTEMPLATEA;

/* ----------------------------------------------------------------------------------------------------------------
 * Window messages
 * -------------------------------------------------------------------------------------------------------------- */
#define WM_NULL              0x0000
#define WM_CREATE            0x0001
#define WM_DESTROY           0x0002
#define WM_MOVE              0x0003
#define WM_SIZE              0x0005
#define WM_ACTIVATE          0x0006
#define WM_SETFOCUS          0x0007
#define WM_KILLFOCUS         0x0008
#define WM_ENABLE            0x000A
#define WM_SETREDRAW         0x000B
#define WM_SETTEXT           0x000C
#define WM_GETTEXT           0x000D
#define WM_GETTEXTLENGTH     0x000E
#define WM_PAINT             0x000F
#define WM_CLOSE             0x0010
#define WM_QUERYENDSESSION   0x0011
#define WM_QUIT              0x0012
#define WM_ERASEBKGND        0x0014
#define WM_SYSCOLORCHANGE    0x0015
#define WM_ENDSESSION        0x0016
#define WM_SHOWWINDOW        0x0018
#define WM_ACTIVATEAPP       0x001C
#define WM_SETCURSOR         0x0020
#define WM_MOUSEACTIVATE     0x0021
#define WM_GETMINMAXINFO     0x0024
#define WM_DRAWITEM          0x002B
#define WM_MEASUREITEM       0x002C
#define WM_SETFONT           0x0030
#define WM_GETFONT           0x0031
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED  0x0047
#define WM_POWER             0x0048
#define WM_NOTIFY            0x004E
#define WM_HELP              0x0053
#define WM_ICONERASEBKGND    0x0027
#define WM_DISPLAYCHANGE     0x007E
#define WM_GETICON           0x007F
#define WM_SETICON           0x0080
#define WM_NCCREATE          0x0081
#define WM_NCDESTROY         0x0082
#define WM_NCCALCSIZE        0x0083
#define WM_NCHITTEST         0x0084
#define WM_NCPAINT           0x0085
#define WM_NCACTIVATE        0x0086
#define WM_GETDLGCODE        0x0087
#define WM_NCMOUSEMOVE       0x00A0
#define WM_NCLBUTTONDOWN     0x00A1
#define WM_KEYFIRST          0x0100
#define WM_KEYDOWN           0x0100
#define WM_KEYUP             0x0101
#define WM_CHAR              0x0102
#define WM_SYSKEYDOWN        0x0104
#define WM_SYSKEYUP          0x0105
#define WM_SYSCHAR           0x0106
#define WM_KEYLAST           0x0108
#define WM_INITDIALOG        0x0110
#define WM_COMMAND           0x0111
#define WM_SYSCOMMAND        0x0112
#define WM_TIMER             0x0113
#define WM_HSCROLL           0x0114
#define WM_VSCROLL           0x0115
#define WM_INITMENU          0x0116
#define WM_INITMENUPOPUP     0x0117
#define WM_MENUSELECT        0x011F
#define WM_ENTERIDLE         0x0121
#define WM_CTLCOLORMSGBOX    0x0132
#define WM_CTLCOLOREDIT      0x0133
#define WM_CTLCOLORLISTBOX   0x0134
#define WM_CTLCOLORBTN       0x0135
#define WM_CTLCOLORDLG       0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC    0x0138
#define WM_MOUSEFIRST        0x0200
#define WM_MOUSEMOVE         0x0200
#define WM_LBUTTONDOWN       0x0201
#define WM_LBUTTONUP         0x0202
#define WM_LBUTTONDBLCLK     0x0203
#define WM_RBUTTONDOWN       0x0204
#define WM_RBUTTONUP         0x0205
#define WM_RBUTTONDBLCLK     0x0206
#define WM_MBUTTONDOWN       0x0207
#define WM_MBUTTONUP         0x0208
#define WM_MBUTTONDBLCLK     0x0209
#define WM_MOUSEWHEEL        0x020A
#define WM_MOUSELAST         0x020A
#define WM_PARENTNOTIFY      0x0210
#define WM_ENTERMENULOOP     0x0211
#define WM_EXITMENULOOP      0x0212
#define WM_POWERBROADCAST    0x0218
#define WM_DEVICECHANGE      0x0219
#define WM_MOVING            0x0216
#define WM_SIZING            0x0214
#define WM_ENTERSIZEMOVE     0x0231
#define WM_EXITSIZEMOVE      0x0232
#define WM_PALETTECHANGED    0x0311
#define WM_QUERYNEWPALETTE   0x030F
#define WM_HOTKEY            0x0312
#define WM_USER              0x0400
#define WM_APP               0x8000

#define WA_INACTIVE    0
#define WA_ACTIVE      1
#define WA_CLICKACTIVE 2

#define SIZE_RESTORED  0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2

#define KF_EXTENDED 0x0100
#define KF_DLGMODE  0x0800
#define KF_MENUMODE 0x1000
#define KF_ALTDOWN  0x2000
#define KF_REPEAT   0x4000
#define KF_UP       0x8000

#define MK_LBUTTON  0x0001
#define MK_RBUTTON  0x0002
#define MK_SHIFT    0x0004
#define MK_CONTROL  0x0008
#define MK_MBUTTON  0x0010

#define SC_SIZE         0xF000
#define SC_MOVE         0xF010
#define SC_MINIMIZE     0xF020
#define SC_MAXIMIZE     0xF030
#define SC_CLOSE        0xF060
#define SC_KEYMENU      0xF100
#define SC_RESTORE      0xF120
#define SC_SCREENSAVE   0xF140
#define SC_MONITORPOWER 0xF170

#define HTCLIENT  1
#define HTCAPTION 2

/* Power broadcast (pbt.h) */
#define PBT_APMQUERYSUSPEND     0x0000
#define PBT_APMQUERYSTANDBY     0x0001
#define PBT_APMQUERYSUSPENDFAILED 0x0002
#define PBT_APMSUSPEND          0x0004
#define PBT_APMSTANDBY          0x0005
#define PBT_APMRESUMECRITICAL   0x0006
#define PBT_APMRESUMESUSPEND    0x0007
#define PBT_APMRESUMESTANDBY    0x0008
#define PBT_APMRESUMEAUTOMATIC  0x0012
#define BROADCAST_QUERY_DENY    0x424D5144

/* ----------------------------------------------------------------------------------------------------------------
 * Control messages and notifications
 * -------------------------------------------------------------------------------------------------------------- */
#define BM_GETCHECK      0x00F0
#define BM_SETCHECK      0x00F1
#define BM_GETSTATE      0x00F2
#define BM_SETSTATE      0x00F3
#define BM_SETSTYLE      0x00F4
#define BM_CLICK         0x00F5
#define BM_GETIMAGE      0x00F6
#define BM_SETIMAGE      0x00F7
#define BST_UNCHECKED    0x0000
#define BST_CHECKED      0x0001
#define BST_INDETERMINATE 0x0002
#define BST_PUSHED       0x0004
#define BST_FOCUS        0x0008
#define BN_CLICKED       0
#define BN_DOUBLECLICKED 5
#define BN_SETFOCUS      6
#define BN_KILLFOCUS     7

#define EM_GETSEL        0x00B0
#define EM_SETSEL        0x00B1
#define EM_GETLINECOUNT  0x00BA
#define EM_LIMITTEXT     0x00C5
#define EM_SETLIMITTEXT  EM_LIMITTEXT
#define EM_SETREADONLY   0x00CF
#define EN_SETFOCUS      0x0100
#define EN_KILLFOCUS     0x0200
#define EN_CHANGE        0x0300
#define EN_UPDATE        0x0400
#define EN_MAXTEXT       0x0501

#define STM_SETICON      0x0170
#define STM_GETICON      0x0171
#define STM_SETIMAGE     0x0172
#define STM_GETIMAGE     0x0173
#define STN_CLICKED      0
#define STN_DBLCLK       1

#define LB_ADDSTRING     0x0180
#define LB_INSERTSTRING  0x0181
#define LB_DELETESTRING  0x0182
#define LB_RESETCONTENT  0x0184
#define LB_SETSEL        0x0185
#define LB_SETCURSEL     0x0186
#define LB_GETSEL        0x0187
#define LB_GETCURSEL     0x0188
#define LB_GETTEXT       0x0189
#define LB_GETTEXTLEN    0x018A
#define LB_GETCOUNT      0x018B
#define LB_SELECTSTRING  0x018C
#define LB_DIR           0x018D
#define LB_GETTOPINDEX   0x018E
#define LB_FINDSTRING    0x018F
#define LB_GETSELCOUNT   0x0190
#define LB_GETSELITEMS   0x0191
#define LB_SETTOPINDEX   0x0197
#define LB_GETITEMRECT   0x0198
#define LB_GETITEMDATA   0x0199
#define LB_SETITEMDATA   0x019A
#define LB_SETCARETINDEX 0x019E
#define LB_GETCARETINDEX 0x019F
#define LB_SETITEMHEIGHT 0x01A0
#define LB_GETITEMHEIGHT 0x01A1
#define LB_FINDSTRINGEXACT 0x01A2
#define LB_SETHORIZONTALEXTENT 0x0194
#define LB_GETHORIZONTALEXTENT 0x0193
#define LB_ERR           (-1)
#define LB_ERRSPACE      (-2)
#define LBN_ERRSPACE     (-2)
#define LBN_SELCHANGE    1
#define LBN_DBLCLK       2
#define LBN_SELCANCEL    3
#define LBN_SETFOCUS     4
#define LBN_KILLFOCUS    5

#define CB_GETEDITSEL    0x0140
#define CB_LIMITTEXT     0x0141
#define CB_SETEDITSEL    0x0142
#define CB_ADDSTRING     0x0143
#define CB_DELETESTRING  0x0144
#define CB_DIR           0x0145
#define CB_GETCOUNT      0x0146
#define CB_GETCURSEL     0x0147
#define CB_GETLBTEXT     0x0148
#define CB_GETLBTEXTLEN  0x0149
#define CB_INSERTSTRING  0x014A
#define CB_RESETCONTENT  0x014B
#define CB_FINDSTRING    0x014C
#define CB_SELECTSTRING  0x014D
#define CB_SETCURSEL     0x014E
#define CB_SHOWDROPDOWN  0x014F
#define CB_GETITEMDATA   0x0150
#define CB_SETITEMDATA   0x0151
#define CB_GETDROPPEDCONTROLRECT 0x0152
#define CB_SETITEMHEIGHT 0x0153
#define CB_GETITEMHEIGHT 0x0154
#define CB_FINDSTRINGEXACT 0x0158
#define CB_GETDROPPEDSTATE 0x0157
#define CB_ERR           (-1)
#define CB_ERRSPACE      (-2)
#define CBN_ERRSPACE     (-1)
#define CBN_SELCHANGE    1
#define CBN_DBLCLK       2
#define CBN_SETFOCUS     3
#define CBN_KILLFOCUS    4
#define CBN_EDITCHANGE   5
#define CBN_EDITUPDATE   6
#define CBN_DROPDOWN     7
#define CBN_CLOSEUP      8
#define CBN_SELENDOK     9
#define CBN_SELENDCANCEL 10

#define SBM_SETPOS       0x00E0
#define SBM_GETPOS       0x00E1
#define SBM_SETRANGE     0x00E2
#define SBM_SETSCROLLINFO 0x00E9
#define SBM_GETSCROLLINFO 0x00EA
#define SIF_RANGE           0x0001
#define SIF_PAGE            0x0002
#define SIF_POS             0x0004
#define SIF_DISABLENOSCROLL 0x0008
#define SIF_TRACKPOS        0x0010
#define SIF_ALL             (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)
#define SB_HORZ          0
#define SB_VERT          1
#define SB_CTL           2
#define SB_LINEUP        0
#define SB_LINELEFT      0
#define SB_LINEDOWN      1
#define SB_LINERIGHT     1
#define SB_PAGEUP        2
#define SB_PAGELEFT      2
#define SB_PAGEDOWN      3
#define SB_PAGERIGHT     3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK    5
#define SB_TOP           6
#define SB_LEFT          6
#define SB_BOTTOM        7
#define SB_RIGHT         7
#define SB_ENDSCROLL     8

/* Trackbar (comctl32) */
#define TBM_GETPOS       (WM_USER)
#define TBM_GETRANGEMIN  (WM_USER + 1)
#define TBM_GETRANGEMAX  (WM_USER + 2)
#define TBM_GETTIC       (WM_USER + 3)
#define TBM_SETTIC       (WM_USER + 4)
#define TBM_SETPOS       (WM_USER + 5)
#define TBM_SETRANGE     (WM_USER + 6)
#define TBM_SETRANGEMIN  (WM_USER + 7)
#define TBM_SETRANGEMAX  (WM_USER + 8)
#define TBM_CLEARTICS    (WM_USER + 9)
#define TBM_SETSEL       (WM_USER + 10)
#define TBM_SETSELSTART  (WM_USER + 11)
#define TBM_SETSELEND    (WM_USER + 12)
#define TBM_SETPAGESIZE  (WM_USER + 21)
#define TBM_GETPAGESIZE  (WM_USER + 22)
#define TBM_SETLINESIZE  (WM_USER + 23)
#define TBM_GETLINESIZE  (WM_USER + 24)
#define TBM_SETTICFREQ   (WM_USER + 20)
#define TB_LINEUP        0
#define TB_LINEDOWN      1
#define TB_PAGEUP        2
#define TB_PAGEDOWN      3
#define TB_THUMBPOSITION 4
#define TB_THUMBTRACK    5
#define TB_TOP           6
#define TB_BOTTOM        7
#define TB_ENDTRACK      8

/* List view (comctl32) */
#define LVM_FIRST            0x1000
#define LVM_SETBKCOLOR       (LVM_FIRST + 1)
#define LVM_GETIMAGELIST     (LVM_FIRST + 2)
#define LVM_SETIMAGELIST     (LVM_FIRST + 3)
#define LVM_GETITEMCOUNT     (LVM_FIRST + 4)
#define LVM_GETITEMA         (LVM_FIRST + 5)
#define LVM_SETITEMA         (LVM_FIRST + 6)
#define LVM_INSERTITEMA      (LVM_FIRST + 7)
#define LVM_DELETEITEM       (LVM_FIRST + 8)
#define LVM_DELETEALLITEMS   (LVM_FIRST + 9)
#define LVM_GETNEXTITEM      (LVM_FIRST + 12)
#define LVM_FINDITEMA        (LVM_FIRST + 13)
#define LVM_GETITEMRECT      (LVM_FIRST + 14)
#define LVM_ENSUREVISIBLE    (LVM_FIRST + 19)
#define LVM_SCROLL           (LVM_FIRST + 20)
#define LVM_REDRAWITEMS      (LVM_FIRST + 21)
#define LVM_GETCOLUMNA       (LVM_FIRST + 25)
#define LVM_SETCOLUMNA       (LVM_FIRST + 26)
#define LVM_INSERTCOLUMNA    (LVM_FIRST + 27)
#define LVM_DELETECOLUMN     (LVM_FIRST + 28)
#define LVM_GETCOLUMNWIDTH   (LVM_FIRST + 29)
#define LVM_SETCOLUMNWIDTH   (LVM_FIRST + 30)
#define LVM_SETTEXTCOLOR     (LVM_FIRST + 36)
#define LVM_SETTEXTBKCOLOR   (LVM_FIRST + 38)
#define LVM_GETTOPINDEX      (LVM_FIRST + 39)
#define LVM_GETCOUNTPERPAGE  (LVM_FIRST + 40)
#define LVM_UPDATE           (LVM_FIRST + 42)
#define LVM_SETITEMSTATE     (LVM_FIRST + 43)
#define LVM_GETITEMSTATE     (LVM_FIRST + 44)
#define LVM_GETITEMTEXTA     (LVM_FIRST + 45)
#define LVM_SETITEMTEXTA     (LVM_FIRST + 46)
#define LVM_SETITEMCOUNT     (LVM_FIRST + 47)
#define LVM_GETSELECTEDCOUNT (LVM_FIRST + 50)
#define LVM_SETICONSPACING   (LVM_FIRST + 53)
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#define LVM_GETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 55)
#define LVM_SETHOVERTIME     (LVM_FIRST + 71)
#define LVM_GETITEM       LVM_GETITEMA
#define LVM_SETITEM       LVM_SETITEMA
#define LVM_INSERTITEM    LVM_INSERTITEMA
#define LVM_FINDITEM      LVM_FINDITEMA
#define LVM_GETCOLUMN     LVM_GETCOLUMNA
#define LVM_SETCOLUMN     LVM_SETCOLUMNA
#define LVM_INSERTCOLUMN  LVM_INSERTCOLUMNA
#define LVM_GETITEMTEXT   LVM_GETITEMTEXTA
#define LVM_SETITEMTEXT   LVM_SETITEMTEXTA

#define LVIF_TEXT        0x0001
#define LVIF_IMAGE       0x0002
#define LVIF_PARAM       0x0004
#define LVIF_STATE       0x0008
#define LVIS_FOCUSED     0x0001
#define LVIS_SELECTED    0x0002
#define LVIS_CUT         0x0004
#define LVIS_DROPHILITED 0x0008
#define LVNI_ALL         0x0000
#define LVNI_FOCUSED     0x0001
#define LVNI_SELECTED    0x0002
#define LVFI_PARAM       0x0001
#define LVFI_STRING      0x0002
#define LVCF_FMT         0x0001
#define LVCF_WIDTH       0x0002
#define LVCF_TEXT        0x0004
#define LVCF_SUBITEM     0x0008
#define LVCFMT_LEFT      0x0000
#define LVCFMT_RIGHT     0x0001
#define LVCFMT_CENTER    0x0002
#define LVSIL_NORMAL     0
#define LVSIL_SMALL      1
#define LVSIL_STATE      2
#define LVS_ICON         0x0000
#define LVS_REPORT       0x0001
#define LVS_SMALLICON    0x0002
#define LVS_LIST         0x0003
#define LVS_SINGLESEL    0x0004
#define LVS_SHOWSELALWAYS 0x0008
#define LVS_SHAREIMAGELISTS 0x0040
#define LVS_NOLABELWRAP  0x0080
#define LVS_ALIGNLEFT    0x0800
#define LVS_NOSORTHEADER 0x8000
#define LVS_EX_GRIDLINES 0x00000001
#define LVS_EX_FULLROWSELECT 0x00000020
#define LVS_EX_ONECLICKACTIVATE 0x00000040
#define LVS_EX_TRACKSELECT 0x00000008
#define LVIR_BOUNDS       0
#define LVIR_ICON         1
#define LVIR_LABEL        2
#define LVIR_SELECTBOUNDS 3
#define LVSCW_AUTOSIZE   (-1)
#define LVSCW_AUTOSIZE_USEHEADER (-2)

#define NM_FIRST         (0U - 0U)
#define NM_OUTOFMEMORY   (NM_FIRST - 1)
#define NM_CLICK         (NM_FIRST - 2)
#define NM_DBLCLK        (NM_FIRST - 3)
#define NM_RETURN        (NM_FIRST - 4)
#define NM_RCLICK        (NM_FIRST - 5)
#define NM_RDBLCLK       (NM_FIRST - 6)
#define NM_SETFOCUS      (NM_FIRST - 7)
#define NM_KILLFOCUS     (NM_FIRST - 8)
#define NM_HOVER         (NM_FIRST - 13)
#define LVN_FIRST        (0U - 100U)
#define LVN_ITEMCHANGING (LVN_FIRST - 0)
#define LVN_ITEMCHANGED  (LVN_FIRST - 1)
#define LVN_INSERTITEM   (LVN_FIRST - 2)
#define LVN_DELETEITEM   (LVN_FIRST - 3)
#define LVN_DELETEALLITEMS (LVN_FIRST - 4)
#define LVN_COLUMNCLICK  (LVN_FIRST - 8)
#define LVN_BEGINDRAG    (LVN_FIRST - 9)
#define LVN_BEGINRDRAG   (LVN_FIRST - 11)
#define LVN_ITEMACTIVATE (LVN_FIRST - 14)
#define LVN_KEYDOWN      (LVN_FIRST - 55)
#define LVN_HOTTRACK     (LVN_FIRST - 21)
#define TCN_FIRST        (0U - 550U)
#define TCN_SELCHANGE    (TCN_FIRST - 1)
#define TCN_SELCHANGING  (TCN_FIRST - 2)
#define TCM_FIRST        0x1300
#define TCM_GETITEMCOUNT (TCM_FIRST + 4)
#define TCM_GETITEMA     (TCM_FIRST + 5)
#define TCM_SETITEMA     (TCM_FIRST + 6)
#define TCM_INSERTITEMA  (TCM_FIRST + 7)
#define TCM_DELETEITEM   (TCM_FIRST + 8)
#define TCM_DELETEALLITEMS (TCM_FIRST + 9)
#define TCM_GETCURSEL    (TCM_FIRST + 11)
#define TCM_SETCURSEL    (TCM_FIRST + 12)
#define TCM_INSERTITEM   TCM_INSERTITEMA
#define TCIF_TEXT        0x0001
#define TCIF_IMAGE       0x0002
#define TCIF_PARAM       0x0008
#define UDM_SETRANGE     (WM_USER + 101)
#define UDM_GETRANGE     (WM_USER + 102)
#define UDM_SETPOS       (WM_USER + 103)
#define UDM_GETPOS       (WM_USER + 104)
#define UDM_SETBUDDY     (WM_USER + 105)
#define UDN_FIRST        (0U - 721U)
#define UDN_DELTAPOS     (UDN_FIRST - 1)
#define PBM_SETRANGE     (WM_USER + 1)
#define PBM_SETPOS       (WM_USER + 2)
#define PBM_DELTAPOS     (WM_USER + 3)
#define PBM_SETSTEP      (WM_USER + 4)
#define PBM_STEPIT       (WM_USER + 5)
#define ICC_LISTVIEW_CLASSES 0x00000001
#define ICC_TREEVIEW_CLASSES 0x00000002
#define ICC_BAR_CLASSES      0x00000004
#define ICC_TAB_CLASSES      0x00000008
#define ICC_UPDOWN_CLASS     0x00000010
#define ICC_PROGRESS_CLASS   0x00000020
#define ICC_WIN95_CLASSES    0x000000FF

/* Image lists */
#define ILC_MASK     0x0001
#define ILC_COLOR    0x0000
#define ILC_COLORDDB 0x00FE
#define ILC_COLOR4   0x0004
#define ILC_COLOR8   0x0008
#define ILC_COLOR16  0x0010
#define ILC_COLOR24  0x0018
#define ILC_COLOR32  0x0020
#define ILD_NORMAL      0x0000
#define ILD_TRANSPARENT 0x0001
#define CLR_NONE    0xFFFFFFFFL
#define CLR_DEFAULT 0xFF000000L

/* ----------------------------------------------------------------------------------------------------------------
 * Styles, show commands, window longs, metrics
 * -------------------------------------------------------------------------------------------------------------- */
#define WS_OVERLAPPED   0x00000000L
#define WS_POPUP        0x80000000L
#define WS_CHILD        0x40000000L
#define WS_MINIMIZE     0x20000000L
#define WS_VISIBLE      0x10000000L
#define WS_DISABLED     0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE     0x01000000L
#define WS_CAPTION      0x00C00000L
#define WS_BORDER       0x00800000L
#define WS_DLGFRAME     0x00400000L
#define WS_VSCROLL      0x00200000L
#define WS_HSCROLL      0x00100000L
#define WS_SYSMENU      0x00080000L
#define WS_THICKFRAME   0x00040000L
#define WS_GROUP        0x00020000L
#define WS_TABSTOP      0x00010000L
#define WS_MINIMIZEBOX  0x00020000L
#define WS_MAXIMIZEBOX  0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW  (WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST     0x00000008L
#define WS_EX_TRANSPARENT 0x00000020L
#define WS_EX_APPWINDOW   0x00040000L
#define WS_EX_TOOLWINDOW  0x00000080L
#define WS_EX_CLIENTEDGE  0x00000200L
#define WS_EX_DLGMODALFRAME 0x00000001L
#define CS_VREDRAW  0x0001
#define CS_HREDRAW  0x0002
#define CS_DBLCLKS  0x0008
#define CS_OWNDC    0x0020
#define BS_PUSHBUTTON    0x00000000L
#define BS_DEFPUSHBUTTON 0x00000001L
#define BS_CHECKBOX      0x00000002L
#define BS_AUTOCHECKBOX  0x00000003L
#define BS_RADIOBUTTON   0x00000004L
#define BS_AUTORADIOBUTTON 0x00000009L
#define BS_OWNERDRAW     0x0000000BL
#define BS_BITMAP        0x00000080L
#define BS_GROUPBOX      0x00000007L
#define BS_VCENTER       0x00000C00L
#define BS_FLAT          0x00008000L
#define SS_LEFT          0x00000000L
#define SS_CENTER        0x00000001L
#define SS_RIGHT         0x00000002L
#define SS_ICON          0x00000003L
#define SS_BITMAP        0x0000000EL
#define SS_OWNERDRAW     0x0000000DL
#define SS_NOTIFY        0x00000100L
#define SS_BLACKFRAME    0x00000007L
#define SS_GRAYFRAME     0x00000008L
#define SS_CENTERIMAGE   0x00000200L
#define SS_REALSIZEIMAGE 0x00000800L
#define SS_SUNKEN        0x00001000L
#define CBS_SIMPLE       0x0001L
#define CBS_DROPDOWN     0x0002L
#define CBS_DROPDOWNLIST 0x0003L
#define CBS_SORT         0x0100L
#define LBS_NOTIFY       0x0001L
#define LBS_SORT         0x0002L
#define LBS_NOINTEGRALHEIGHT 0x0100L
#define SBS_HORZ         0x0000L
#define SBS_VERT         0x0001L
#define SBS_LEFTALIGN    0x0002L
#define TBS_AUTOTICKS    0x0001
#define ES_LEFT          0x0000L
#define ES_AUTOHSCROLL   0x0080L
#define ES_READONLY      0x0800L
#define ES_NUMBER        0x2000L
#define DS_MODALFRAME    0x80L
#define DS_SETFONT       0x40L
#define DS_CENTER        0x0800L
#define DS_ABSALIGN      0x01L
#define DS_3DLOOK        0x0004L
#define DS_SETFOREGROUND 0x0200L
#define DS_CONTROL       0x0400L
#define ODT_BUTTON       4
#define ODT_STATIC       5
#define ODT_LISTBOX      2
#define ODS_SELECTED     0x0001
#define ODS_DISABLED     0x0004
#define ODS_FOCUS        0x0010
#define ODA_DRAWENTIRE   0x0001
#define ODA_SELECT       0x0002
#define ODA_FOCUS        0x0004

#define SW_HIDE            0
#define SW_SHOWNORMAL      1
#define SW_NORMAL          1
#define SW_SHOWMINIMIZED   2
#define SW_SHOWMAXIMIZED   3
#define SW_MAXIMIZE        3
#define SW_SHOWNOACTIVATE  4
#define SW_SHOW            5
#define SW_MINIMIZE        6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA          8
#define SW_RESTORE         9
#define SW_SHOWDEFAULT     10

#define GWL_WNDPROC    (-4)
#define GWL_HINSTANCE  (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_STYLE      (-16)
#define GWL_EXSTYLE    (-20)
#define GWL_USERDATA   (-21)
#define GWL_ID         (-12)
#define DWL_MSGRESULT  0
#define DWL_DLGPROC    4
#define DWL_USER       8
#define GWLP_WNDPROC   GWL_WNDPROC
#define GWLP_USERDATA  GWL_USERDATA
#define DWLP_MSGRESULT DWL_MSGRESULT
#define DWLP_USER      DWL_USER

#define SWP_NOSIZE         0x0001
#define SWP_NOMOVE         0x0002
#define SWP_NOZORDER       0x0004
#define SWP_NOREDRAW       0x0008
#define SWP_NOACTIVATE     0x0010
#define SWP_FRAMECHANGED   0x0020
#define SWP_SHOWWINDOW     0x0040
#define SWP_HIDEWINDOW     0x0080
#define SWP_NOOWNERZORDER  0x0200
#define SWP_NOSENDCHANGING 0x0400
#define HWND_TOP       ((HWND)0)
#define HWND_BOTTOM    ((HWND)1)
#define HWND_TOPMOST   ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_DESKTOP   ((HWND)0)

#define SM_CXSCREEN   0
#define SM_CYSCREEN   1
#define SM_CXVSCROLL  2
#define SM_CYHSCROLL  3
#define SM_CYCAPTION  4
#define SM_CXBORDER   5
#define SM_CYBORDER   6
#define SM_CXDLGFRAME 7
#define SM_CYDLGFRAME 8
#define SM_CXICON     11
#define SM_CYICON     12
#define SM_CXCURSOR   13
#define SM_CYCURSOR   14
#define SM_CYMENU     15
#define SM_CXFULLSCREEN 16
#define SM_CYFULLSCREEN 17
#define SM_CXFRAME    32
#define SM_CYFRAME    33
#define SM_CXFIXEDFRAME SM_CXDLGFRAME
#define SM_CYFIXEDFRAME SM_CYDLGFRAME
#define SM_CXSMICON   49
#define SM_CYSMICON   50

#define SPI_GETWORKAREA     0x0030
#define SPI_SCREENSAVERRUNNING 0x0061
#define SPI_SETSCREENSAVERRUNNING 0x0061
#define SPI_GETSCREENSAVEACTIVE 0x0010
#define SPI_SETSCREENSAVEACTIVE 0x0011

#define RDW_INVALIDATE  0x0001
#define RDW_ERASE       0x0004
#define RDW_UPDATENOW   0x0100
#define RDW_ALLCHILDREN 0x0080

#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001

#define IDOK     1
#define IDCANCEL 2
#define IDABORT  3
#define IDRETRY  4
#define IDIGNORE 5
#define IDYES    6
#define IDNO     7
#define IDCLOSE  8
#define IDHELP   9

#define MB_OK              0x00000000L
#define MB_OKCANCEL        0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL     0x00000003L
#define MB_YESNO           0x00000004L
#define MB_RETRYCANCEL     0x00000005L
#define MB_ICONHAND        0x00000010L
#define MB_ICONERROR       MB_ICONHAND
#define MB_ICONSTOP        MB_ICONHAND
#define MB_ICONQUESTION    0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONWARNING     MB_ICONEXCLAMATION
#define MB_ICONASTERISK    0x00000040L
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_DEFBUTTON1      0x00000000L
#define MB_DEFBUTTON2      0x00000100L
#define MB_APPLMODAL       0x00000000L
#define MB_SYSTEMMODAL     0x00001000L
#define MB_TASKMODAL       0x00002000L
#define MB_SETFOREGROUND   0x00010000L
#define MB_TOPMOST         0x00040000L

#define IDC_ARROW    MAKEINTRESOURCE(32512)
#define IDC_IBEAM    MAKEINTRESOURCE(32513)
#define IDC_WAIT     MAKEINTRESOURCE(32514)
#define IDC_HAND     MAKEINTRESOURCE(32649)
#define IDI_APPLICATION MAKEINTRESOURCE(32512)

#define IMAGE_BITMAP 0
#define IMAGE_ICON   1
#define IMAGE_CURSOR 2
#define LR_DEFAULTCOLOR     0x00000000
#define LR_LOADFROMFILE     0x00000010
#define LR_CREATEDIBSECTION 0x00002000
#define LR_DEFAULTSIZE      0x00000040
#define LR_SHARED           0x00008000

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_STRING     0x00000400
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define LANG_NEUTRAL    0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))

#define LMEM_FIXED    0x0000
#define LMEM_ZEROINIT 0x0040
#define LPTR          (LMEM_FIXED | LMEM_ZEROINIT)
#define GMEM_FIXED    0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GHND          (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR          (GMEM_FIXED | GMEM_ZEROINIT)

/* Open-file dialog */
#define OFN_READONLY          0x00000001
#define OFN_OVERWRITEPROMPT   0x00000002
#define OFN_HIDEREADONLY      0x00000004
#define OFN_NOCHANGEDIR       0x00000008
#define OFN_ENABLEHOOK        0x00000020
#define OFN_ENABLETEMPLATE    0x00000040
#define OFN_NOVALIDATE        0x00000100
#define OFN_ALLOWMULTISELECT  0x00000200
#define OFN_EXTENSIONDIFFERENT 0x00000400
#define OFN_SHAREAWARE        0x00004000
#define OFN_NOTESTFILECREATE  0x00010000
#define OFN_NONETWORKBUTTON   0x00020000
#define OFN_NOLONGNAMES       0x00040000
#define OFN_PATHMUSTEXIST     0x00000800
#define OFN_FILEMUSTEXIST     0x00001000
#define OFN_CREATEPROMPT      0x00002000
#define OFN_NOREADONLYRETURN  0x00008000
#define OFN_EXPLORER          0x00080000
#define OFN_NODEREFERENCELINKS 0x00100000
#define OFN_LONGNAMES         0x00200000
#define OFN_ENABLESIZING      0x00800000
#define CDN_FIRST          (0U - 601U)
#define CDN_INITDONE       (CDN_FIRST - 0)
#define CDN_SELCHANGE      (CDN_FIRST - 1)
#define CDN_FOLDERCHANGE   (CDN_FIRST - 2)
#define CDN_SHAREVIOLATION (CDN_FIRST - 3)
#define CDN_HELP           (CDN_FIRST - 4)
#define CDN_FILEOK         (CDN_FIRST - 5)
#define CDN_TYPECHANGE     (CDN_FIRST - 6)
#define CDM_FIRST          (WM_USER + 100)
#define CDM_GETSPEC        (CDM_FIRST + 0)
#define CDM_GETFILEPATH    (CDM_FIRST + 1)
#define CDM_GETFOLDERPATH  (CDM_FIRST + 2)
#define CDM_SETCONTROLTEXT (CDM_FIRST + 4)
#define CDM_HIDECONTROL    (CDM_FIRST + 5)
#define CDM_SETDEFEXT      (CDM_FIRST + 6)
#define CommDlg_OpenSave_GetSpec(d, s, m)       (int)SendMessage((d), CDM_GETSPEC, (WPARAM)(m), (LPARAM)(LPSTR)(s))
#define CommDlg_OpenSave_GetFilePath(d, s, m)   (int)SendMessage((d), CDM_GETFILEPATH, (WPARAM)(m), (LPARAM)(LPSTR)(s))
#define CommDlg_OpenSave_GetFolderPath(d, s, m) (int)SendMessage((d), CDM_GETFOLDERPATH, (WPARAM)(m), (LPARAM)(LPSTR)(s))
#define CommDlg_OpenSave_SetControlText(d, id, t) (void)SendMessage((d), CDM_SETCONTROLTEXT, (WPARAM)(id), (LPARAM)(LPSTR)(t))
#define CommDlg_OpenSave_HideControl(d, id)     (void)SendMessage((d), CDM_HIDECONTROL, (WPARAM)(id), 0)
typedef struct _OFNOTIFYA { NMHDR hdr; LPOPENFILENAMEA lpOFN; LPSTR pszFile; } OFNOTIFYA, OFNOTIFY, *LPOFNOTIFYA, *LPOFNOTIFY;

/* GDI */
#define HGDI_ERROR  ((HGDIOBJ)(LONG_PTR)-1)
#define SRCCOPY     0x00CC0020
#define SRCPAINT    0x00EE0086
#define SRCAND      0x008800C6
#define SRCINVERT   0x00660046
#define BLACKNESS   0x00000042
#define WHITENESS   0x00FF0062
#define PATCOPY     0x00F00021
#define BI_RGB       0L
#define BI_BITFIELDS 3L
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1
#define BLACKONWHITE 1
#define WHITEONBLACK 2
#define COLORONCOLOR 3
#define HALFTONE     4
#define STRETCH_DELETESCANS COLORONCOLOR
#define STRETCH_HALFTONE    HALFTONE
#define TRANSPARENT  1
#define OPAQUE       2
#define BITSPIXEL    12
#define PLANES       14
#define HORZRES      8
#define VERTRES      10
#define LOGPIXELSX   88
#define LOGPIXELSY   90
#define RASTERCAPS   38
#define NUMCOLORS    24
#define SIZEPALETTE  104
#define RC_PALETTE   0x0100
#define FW_DONTCARE   0
#define FW_THIN       100
#define FW_NORMAL     400
#define FW_MEDIUM     500
#define FW_SEMIBOLD   600
#define FW_BOLD       700
#define FW_HEAVY      900
#define ANSI_CHARSET    0
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define OUT_TT_PRECIS      4
#define OUT_TT_ONLY_PRECIS 7
#define CLIP_DEFAULT_PRECIS 0
#define CLIP_CHARACTER_PRECIS 1
#define CLIP_STROKE_PRECIS 2
#define DEFAULT_QUALITY     0
#define DRAFT_QUALITY       1
#define PROOF_QUALITY       2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define DEFAULT_PITCH  0
#define FIXED_PITCH    1
#define VARIABLE_PITCH 2
#define FF_DONTCARE   (0 << 4)
#define FF_ROMAN      (1 << 4)
#define FF_SWISS      (2 << 4)
#define FF_MODERN     (3 << 4)
#define WHITE_BRUSH  0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH   2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH  4
#define NULL_BRUSH   5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN    6
#define BLACK_PEN    7
#define NULL_PEN     8
#define DEFAULT_GUI_FONT 17
#define DT_TOP        0x00000000
#define DT_LEFT       0x00000000
#define DT_CENTER     0x00000001
#define DT_RIGHT      0x00000002
#define DT_VCENTER    0x00000004
#define DT_BOTTOM     0x00000008
#define DT_WORDBREAK  0x00000010
#define DT_SINGLELINE 0x00000020
#define DT_NOCLIP     0x00000100
#define DT_CALCRECT   0x00000400
#define DT_NOPREFIX   0x00000800
#define DT_END_ELLIPSIS 0x00008000
#define COLOR_SCROLLBAR 0
#define COLOR_BACKGROUND 1
#define COLOR_WINDOW    5
#define COLOR_WINDOWTEXT 8
#define COLOR_BTNFACE   15
#define COLOR_3DFACE    COLOR_BTNFACE
#define COLOR_BTNTEXT   18
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define COLOR_GRAYTEXT  17

/* Virtual keys */
#define VK_LBUTTON   0x01
#define VK_RBUTTON   0x02
#define VK_CANCEL    0x03
#define VK_MBUTTON   0x04
#define VK_BACK      0x08
#define VK_TAB       0x09
#define VK_CLEAR     0x0C
#define VK_RETURN    0x0D
#define VK_SHIFT     0x10
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_PAUSE     0x13
#define VK_CAPITAL   0x14
#define VK_ESCAPE    0x1B
#define VK_SPACE     0x20
#define VK_PRIOR     0x21
#define VK_NEXT      0x22
#define VK_END       0x23
#define VK_HOME      0x24
#define VK_LEFT      0x25
#define VK_UP        0x26
#define VK_RIGHT     0x27
#define VK_DOWN      0x28
#define VK_SELECT    0x29
#define VK_PRINT     0x2A
#define VK_SNAPSHOT  0x2C
#define VK_INSERT    0x2D
#define VK_DELETE    0x2E
#define VK_HELP      0x2F
#define VK_LWIN      0x5B
#define VK_RWIN      0x5C
#define VK_APPS      0x5D
#define VK_NUMPAD0   0x60
#define VK_NUMPAD1   0x61
#define VK_NUMPAD2   0x62
#define VK_NUMPAD3   0x63
#define VK_NUMPAD4   0x64
#define VK_NUMPAD5   0x65
#define VK_NUMPAD6   0x66
#define VK_NUMPAD7   0x67
#define VK_NUMPAD8   0x68
#define VK_NUMPAD9   0x69
#define VK_MULTIPLY  0x6A
#define VK_ADD       0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT  0x6D
#define VK_DECIMAL   0x6E
#define VK_DIVIDE    0x6F
#define VK_F1        0x70
#define VK_F2        0x71
#define VK_F3        0x72
#define VK_F4        0x73
#define VK_F5        0x74
#define VK_F6        0x75
#define VK_F7        0x76
#define VK_F8        0x77
#define VK_F9        0x78
#define VK_F10       0x79
#define VK_F11       0x7A
#define VK_F12       0x7B
#define VK_NUMLOCK   0x90
#define VK_SCROLL    0x91
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5
// The US-layout punctuation keys (PORT-PLAN.md P2.2: platform/apple/wkernel_sdl.c reports them)
#define VK_OEM_1      0xBA  // ;:
#define VK_OEM_PLUS   0xBB  // =+
#define VK_OEM_COMMA  0xBC  // ,<
#define VK_OEM_MINUS  0xBD  // -_
#define VK_OEM_PERIOD 0xBE  // .>
#define VK_OEM_2      0xBF  // /?
#define VK_OEM_3      0xC0  // `~
#define VK_OEM_4      0xDB  // [{
#define VK_OEM_5      0xDC  // \|
#define VK_OEM_6      0xDD  // ]}
#define VK_OEM_7      0xDE  // '"
#define VK_OEM_102    0xE2  // the extra key left of Z on ISO keyboards

/* ----------------------------------------------------------------------------------------------------------------
 * Functions (null implementations in platform/common/null/win32_null.c)
 * -------------------------------------------------------------------------------------------------------------- */
/* Windows and dialogs */
HWND     WINAPI GetDlgItem(HWND, int);
int      WINAPI GetDlgCtrlID(HWND);
UINT     WINAPI GetDlgItemTextA(HWND, int, LPSTR, int);
BOOL     WINAPI SetDlgItemTextA(HWND, int, LPCSTR);
UINT     WINAPI GetDlgItemInt(HWND, int, BOOL*, BOOL);
BOOL     WINAPI SetDlgItemInt(HWND, int, UINT, BOOL);
LRESULT  WINAPI SendDlgItemMessageA(HWND, int, UINT, WPARAM, LPARAM);
BOOL     WINAPI CheckDlgButton(HWND, int, UINT);
UINT     WINAPI IsDlgButtonChecked(HWND, int);
BOOL     WINAPI CheckRadioButton(HWND, int, int, int);
INT_PTR  WINAPI DialogBoxParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
HWND     WINAPI CreateDialogParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
BOOL     WINAPI EndDialog(HWND, INT_PTR);
BOOL     WINAPI IsDialogMessageA(HWND, LPMSG);
HWND     WINAPI GetNextDlgTabItem(HWND, HWND, BOOL);
LONG     WINAPI GetWindowLongA(HWND, int);
LONG     WINAPI SetWindowLongA(HWND, int, LONG);
LONG_PTR WINAPI GetWindowLongPtrA(HWND, int);
LONG_PTR WINAPI SetWindowLongPtrA(HWND, int, LONG_PTR);
LRESULT  WINAPI SendMessageA(HWND, UINT, WPARAM, LPARAM);
BOOL     WINAPI PostMessageA(HWND, UINT, WPARAM, LPARAM);
void     WINAPI PostQuitMessage(int);
BOOL     WINAPI PeekMessageA(LPMSG, HWND, UINT, UINT, UINT);
BOOL     WINAPI GetMessageA(LPMSG, HWND, UINT, UINT);
BOOL     WINAPI TranslateMessage(const MSG*);
LRESULT  WINAPI DispatchMessageA(const MSG*);
LRESULT  WINAPI DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
LRESULT  WINAPI CallWindowProcA(WNDPROC, HWND, UINT, WPARAM, LPARAM);
ATOM     WINAPI RegisterClassA(const WNDCLASSA*);
ATOM     WINAPI RegisterClassExA(const WNDCLASSEXA*);
BOOL     WINAPI UnregisterClassA(LPCSTR, HINSTANCE);
HWND     WINAPI CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
BOOL     WINAPI DestroyWindow(HWND);
BOOL     WINAPI ShowWindow(HWND, int);
BOOL     WINAPI UpdateWindow(HWND);
BOOL     WINAPI EnableWindow(HWND, BOOL);
BOOL     WINAPI IsWindowEnabled(HWND);
BOOL     WINAPI IsWindowVisible(HWND);
BOOL     WINAPI IsWindow(HWND);
BOOL     WINAPI IsChild(HWND, HWND);
BOOL     WINAPI IsIconic(HWND);
BOOL     WINAPI IsZoomed(HWND);
BOOL     WINAPI MoveWindow(HWND, int, int, int, int, BOOL);
BOOL     WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
BOOL     WINAPI GetWindowRect(HWND, LPRECT);
BOOL     WINAPI GetClientRect(HWND, LPRECT);
BOOL     WINAPI ClientToScreen(HWND, LPPOINT);
BOOL     WINAPI ScreenToClient(HWND, LPPOINT);
int      WINAPI MapWindowPoints(HWND, HWND, LPPOINT, UINT);
BOOL     WINAPI AdjustWindowRect(LPRECT, DWORD, BOOL);
BOOL     WINAPI AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD);
BOOL     WINAPI SetWindowTextA(HWND, LPCSTR);
int      WINAPI GetWindowTextA(HWND, LPSTR, int);
int      WINAPI GetWindowTextLengthA(HWND);
int      WINAPI GetClassNameA(HWND, LPSTR, int);
HWND     WINAPI GetParent(HWND);
HWND     WINAPI SetParent(HWND, HWND);
HWND     WINAPI GetDesktopWindow(void);
HWND     WINAPI GetForegroundWindow(void);
BOOL     WINAPI SetForegroundWindow(HWND);
HWND     WINAPI GetActiveWindow(void);
HWND     WINAPI SetActiveWindow(HWND);
HWND     WINAPI GetFocus(void);
HWND     WINAPI SetFocus(HWND);
HWND     WINAPI GetWindow(HWND, UINT);
HWND     WINAPI FindWindowA(LPCSTR, LPCSTR);
BOOL     WINAPI EnumChildWindows(HWND, WNDENUMPROC, LPARAM);
BOOL     WINAPI InvalidateRect(HWND, const RECT*, BOOL);
BOOL     WINAPI ValidateRect(HWND, const RECT*);
BOOL     WINAPI RedrawWindow(HWND, const RECT*, HRGN, UINT);
HDC      WINAPI BeginPaint(HWND, LPPAINTSTRUCT);
BOOL     WINAPI EndPaint(HWND, const PAINTSTRUCT*);
UINT_PTR WINAPI SetTimer(HWND, UINT_PTR, UINT, TIMERPROC);
BOOL     WINAPI KillTimer(HWND, UINT_PTR);
int      WINAPI MessageBoxA(HWND, LPCSTR, LPCSTR, UINT);
BOOL     WINAPI MessageBeep(UINT);
int      WINAPI GetSystemMetrics(int);
BOOL     WINAPI SystemParametersInfoA(UINT, UINT, PVOID, UINT);
DWORD    WINAPI GetSysColor(int);
HBRUSH   WINAPI GetSysColorBrush(int);
int      WINAPI ShowCursor(BOOL);
HCURSOR  WINAPI SetCursor(HCURSOR);
BOOL     WINAPI GetCursorPos(LPPOINT);
BOOL     WINAPI SetCursorPos(int, int);
BOOL     WINAPI ClipCursor(const RECT*);
BOOL     WINAPI GetClipCursor(LPRECT);
HWND     WINAPI SetCapture(HWND);
BOOL     WINAPI ReleaseCapture(void);
HWND     WINAPI GetCapture(void);
SHORT    WINAPI GetAsyncKeyState(int);
SHORT    WINAPI GetKeyState(int);
int      WINAPI GetKeyNameTextA(LONG, LPSTR, int);
UINT     WINAPI MapVirtualKeyA(UINT, UINT);
HCURSOR  WINAPI LoadCursorA(HINSTANCE, LPCSTR);
HICON    WINAPI LoadIconA(HINSTANCE, LPCSTR);
HANDLE   WINAPI LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT);
HBITMAP  WINAPI LoadBitmapA(HINSTANCE, LPCSTR);
int      WINAPI LoadStringA(HINSTANCE, UINT, LPSTR, int);
BOOL     WINAPI DestroyIcon(HICON);
BOOL     WINAPI DestroyCursor(HCURSOR);
BOOL     WINAPI GetIconInfo(HICON, PICONINFO);
BOOL     WINAPI DrawIcon(HDC, int, int, HICON);
BOOL     WINAPI DrawIconEx(HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT);
BOOL     WINAPI PtInRect(const RECT*, POINT);
BOOL     WINAPI SetRect(LPRECT, int, int, int, int);
BOOL     WINAPI SetRectEmpty(LPRECT);
BOOL     WINAPI OffsetRect(LPRECT, int, int);
BOOL     WINAPI InflateRect(LPRECT, int, int);
BOOL     WINAPI IntersectRect(LPRECT, const RECT*, const RECT*);
BOOL     WINAPI UnionRect(LPRECT, const RECT*, const RECT*);
BOOL     WINAPI CopyRect(LPRECT, const RECT*);
BOOL     WINAPI IsRectEmpty(const RECT*);
BOOL     WINAPI EqualRect(const RECT*, const RECT*);
int      WINAPI SetScrollPos(HWND, int, int, BOOL);
int      WINAPI GetScrollPos(HWND, int);
BOOL     WINAPI SetScrollRange(HWND, int, int, int, BOOL);
BOOL     WINAPI GetScrollRange(HWND, int, LPINT, LPINT);
int      WINAPI SetScrollInfo(HWND, int, const SCROLLINFO*, BOOL);
BOOL     WINAPI GetScrollInfo(HWND, int, LPSCROLLINFO);
int      WINAPI MulDiv(int, int, int);
DWORD    WINAPI FormatMessageA(DWORD, LPCVOID, DWORD, DWORD, LPSTR, DWORD, va_list*);
HLOCAL   WINAPI LocalAlloc(UINT, SIZE_T);
HLOCAL   WINAPI LocalFree(HLOCAL);
HGLOBAL  WINAPI GlobalAlloc(UINT, SIZE_T);
HGLOBAL  WINAPI GlobalFree(HGLOBAL);
LPVOID   WINAPI GlobalLock(HGLOBAL);
BOOL     WINAPI GlobalUnlock(HGLOBAL);
DWORD    WINAPI SearchPathA(LPCSTR, LPCSTR, LPCSTR, DWORD, LPSTR, LPSTR*);
BOOL     WINAPI GetComputerNameA(LPSTR, LPDWORD);
BOOL     WINAPI GetUserNameA(LPSTR, LPDWORD);
BOOL     WINAPI GetExitCodeProcess(HANDLE, LPDWORD);
void     WINAPI DebugBreak(void);
BOOL     WINAPI IsDebuggerPresent(void);

/* GDI */
HDC      WINAPI GetDC(HWND);
HDC      WINAPI GetWindowDC(HWND);
int      WINAPI ReleaseDC(HWND, HDC);
HDC      WINAPI CreateCompatibleDC(HDC);
HDC      WINAPI CreateDCA(LPCSTR, LPCSTR, LPCSTR, const void*);
BOOL     WINAPI DeleteDC(HDC);
HGDIOBJ  WINAPI SelectObject(HDC, HGDIOBJ);
BOOL     WINAPI DeleteObject(HGDIOBJ);
HGDIOBJ  WINAPI GetStockObject(int);
int      WINAPI GetObjectA(HANDLE, int, LPVOID);
HBITMAP  WINAPI CreateCompatibleBitmap(HDC, int, int);
HBITMAP  WINAPI CreateBitmap(int, int, UINT, UINT, const void*);
HBITMAP  WINAPI CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD);
int      WINAPI GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, LPBITMAPINFO, UINT);
int      WINAPI SetDIBits(HDC, HBITMAP, UINT, UINT, const void*, const BITMAPINFO*, UINT);
int      WINAPI StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void*, const BITMAPINFO*, UINT, DWORD);
int      WINAPI SetDIBitsToDevice(HDC, int, int, DWORD, DWORD, int, int, UINT, UINT, const void*, const BITMAPINFO*, UINT);
BOOL     WINAPI GetBitmapDimensionEx(HBITMAP, LPSIZE);
BOOL     WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
BOOL     WINAPI StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
BOOL     WINAPI PatBlt(HDC, int, int, int, int, DWORD);
int      WINAPI SetStretchBltMode(HDC, int);
int      WINAPI GetDeviceCaps(HDC, int);
HBRUSH   WINAPI CreateSolidBrush(COLORREF);
HPEN     WINAPI CreatePen(int, int, COLORREF);
int      WINAPI FillRect(HDC, const RECT*, HBRUSH);
int      WINAPI FrameRect(HDC, const RECT*, HBRUSH);
BOOL     WINAPI Rectangle(HDC, int, int, int, int);
BOOL     WINAPI MoveToEx(HDC, int, int, LPPOINT);
BOOL     WINAPI LineTo(HDC, int, int);
COLORREF WINAPI SetPixel(HDC, int, int, COLORREF);
COLORREF WINAPI GetPixel(HDC, int, int);
COLORREF WINAPI SetTextColor(HDC, COLORREF);
COLORREF WINAPI GetTextColor(HDC);
COLORREF WINAPI SetBkColor(HDC, COLORREF);
COLORREF WINAPI GetBkColor(HDC);
int      WINAPI SetBkMode(HDC, int);
UINT     WINAPI SetTextAlign(HDC, UINT);
BOOL     WINAPI TextOutA(HDC, int, int, LPCSTR, int);
BOOL     WINAPI ExtTextOutA(HDC, int, int, UINT, const RECT*, LPCSTR, UINT, const INT*);
int      WINAPI DrawTextA(HDC, LPCSTR, int, LPRECT, UINT);
BOOL     WINAPI GetTextExtentPoint32A(HDC, LPCSTR, int, LPSIZE);
BOOL     WINAPI GetTextExtentPointA(HDC, LPCSTR, int, LPSIZE);
BOOL     WINAPI GetTextMetricsA(HDC, LPTEXTMETRICA);
BOOL     WINAPI GetCharABCWidthsA(HDC, UINT, UINT, LPABC);
BOOL     WINAPI GetCharWidthA(HDC, UINT, UINT, LPINT);
HFONT    WINAPI CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR);
HFONT    WINAPI CreateFontIndirectA(const LOGFONTA*);
int      WINAPI AddFontResourceA(LPCSTR);
BOOL     WINAPI RemoveFontResourceA(LPCSTR);
HRGN     WINAPI CreateRectRgn(int, int, int, int);
int      WINAPI SelectClipRgn(HDC, HRGN);
BOOL     WINAPI GdiFlush(void);

/* comctl32 */
void       WINAPI InitCommonControls(void);
BOOL       WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX*);
HIMAGELIST WINAPI ImageList_Create(int, int, UINT, int, int);
BOOL       WINAPI ImageList_Destroy(HIMAGELIST);
int        WINAPI ImageList_Add(HIMAGELIST, HBITMAP, HBITMAP);
int        WINAPI ImageList_AddMasked(HIMAGELIST, HBITMAP, COLORREF);
int        WINAPI ImageList_ReplaceIcon(HIMAGELIST, int, HICON);
BOOL       WINAPI ImageList_Remove(HIMAGELIST, int);
BOOL       WINAPI ImageList_Draw(HIMAGELIST, int, HDC, int, int, UINT);
int        WINAPI ImageList_GetImageCount(HIMAGELIST);
BOOL       WINAPI ImageList_GetIconSize(HIMAGELIST, int*, int*);
COLORREF   WINAPI ImageList_SetBkColor(HIMAGELIST, COLORREF);
BOOL       WINAPI ImageList_BeginDrag(HIMAGELIST, int, int, int);
void       WINAPI ImageList_EndDrag(void);
BOOL       WINAPI ImageList_DragEnter(HWND, int, int);
BOOL       WINAPI ImageList_DragLeave(HWND);
BOOL       WINAPI ImageList_DragMove(int, int);
BOOL       WINAPI ImageList_DragShowNolock(BOOL);
#define ImageList_AddIcon(himl, hicon) ImageList_ReplaceIcon(himl, -1, hicon)
#define ImageList_RemoveAll(himl) ImageList_Remove(himl, -1)

/* comdlg32 / shell32 / ole32 */
BOOL    WINAPI GetOpenFileNameA(LPOPENFILENAMEA);
BOOL    WINAPI GetSaveFileNameA(LPOPENFILENAMEA);
DWORD   WINAPI CommDlgExtendedError(void);
BOOL    WINAPI ShellExecuteExA(LPSHELLEXECUTEINFOA);
HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
HRESULT WINAPI CoInitialize(LPVOID);
void    WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFCLSID, void*, DWORD, REFIID, LPVOID*);

/* ANSI aliases */
#define GetDlgItemText       GetDlgItemTextA
#define SetDlgItemText       SetDlgItemTextA
#define SendDlgItemMessage   SendDlgItemMessageA
#define DialogBoxParam       DialogBoxParamA
#define CreateDialogParam    CreateDialogParamA
#define DialogBox(i, t, w, p) DialogBoxParamA(i, t, w, p, 0)
#define IsDialogMessage      IsDialogMessageA
#define GetWindowLong        GetWindowLongA
#define SetWindowLong        SetWindowLongA
#define GetWindowLongPtr     GetWindowLongPtrA
#define SetWindowLongPtr     SetWindowLongPtrA
#define SendMessage          SendMessageA
#define PostMessage          PostMessageA
#define PeekMessage          PeekMessageA
#define GetMessage           GetMessageA
#define DispatchMessage      DispatchMessageA
#define DefWindowProc        DefWindowProcA
#define CallWindowProc       CallWindowProcA
#define RegisterClass        RegisterClassA
#define RegisterClassEx      RegisterClassExA
#define UnregisterClass      UnregisterClassA
#define CreateWindowEx       CreateWindowExA
#define CreateWindowA(c, n, s, x, y, w, h, p, m, i, l) CreateWindowExA(0, c, n, s, x, y, w, h, p, m, i, l)
#define CreateWindow         CreateWindowA
#define SetWindowText        SetWindowTextA
#define GetWindowText        GetWindowTextA
#define GetWindowTextLength  GetWindowTextLengthA
#define GetClassName         GetClassNameA
#define FindWindow           FindWindowA
#define MessageBox           MessageBoxA
#define SystemParametersInfo SystemParametersInfoA
#define GetKeyNameText       GetKeyNameTextA
#define MapVirtualKey        MapVirtualKeyA
#define LoadCursor           LoadCursorA
#define LoadIcon             LoadIconA
#define LoadImage            LoadImageA
#define LoadBitmap           LoadBitmapA
#define LoadString           LoadStringA
#define FormatMessage        FormatMessageA
#define SearchPath           SearchPathA
#define GetComputerName      GetComputerNameA
#define GetUserName          GetUserNameA
#define CreateDC             CreateDCA
#define GetObject            GetObjectA
#define TextOut              TextOutA
#define ExtTextOut           ExtTextOutA
#define DrawText             DrawTextA
#define GetTextExtentPoint32 GetTextExtentPoint32A
#define GetTextExtentPoint   GetTextExtentPointA
#define GetTextMetrics       GetTextMetricsA
#define GetCharABCWidths     GetCharABCWidthsA
#define GetCharWidth         GetCharWidthA
#define CreateFont           CreateFontA
#define CreateFontIndirect   CreateFontIndirectA
#define AddFontResource      AddFontResourceA
#define RemoveFontResource   RemoveFontResourceA
#define GetOpenFileName      GetOpenFileNameA
#define GetSaveFileName      GetSaveFileNameA
#define ShellExecuteEx       ShellExecuteExA
#define ShellExecute         ShellExecuteA

#ifdef __cplusplus
}
#endif


/* ---- Added for the dialog system (PORT-PLAN.md Phase 6, platform/common/j3dgui): values as in the Windows SDK ---- */
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif
#ifndef COLOR_SCROLLBAR
#define COLOR_SCROLLBAR 0
#endif
#ifndef COLOR_BACKGROUND
#define COLOR_BACKGROUND 1
#endif
#ifndef COLOR_ACTIVECAPTION
#define COLOR_ACTIVECAPTION 2
#endif
#ifndef COLOR_INACTIVECAPTION
#define COLOR_INACTIVECAPTION 3
#endif
#ifndef COLOR_MENU
#define COLOR_MENU 4
#endif
#ifndef COLOR_WINDOW
#define COLOR_WINDOW 5
#endif
#ifndef COLOR_WINDOWFRAME
#define COLOR_WINDOWFRAME 6
#endif
#ifndef COLOR_MENUTEXT
#define COLOR_MENUTEXT 7
#endif
#ifndef COLOR_WINDOWTEXT
#define COLOR_WINDOWTEXT 8
#endif
#ifndef COLOR_CAPTIONTEXT
#define COLOR_CAPTIONTEXT 9
#endif
#ifndef COLOR_ACTIVEBORDER
#define COLOR_ACTIVEBORDER 10
#endif
#ifndef COLOR_INACTIVEBORDER
#define COLOR_INACTIVEBORDER 11
#endif
#ifndef COLOR_APPWORKSPACE
#define COLOR_APPWORKSPACE 12
#endif
#ifndef COLOR_HIGHLIGHT
#define COLOR_HIGHLIGHT 13
#endif
#ifndef COLOR_HIGHLIGHTTEXT
#define COLOR_HIGHLIGHTTEXT 14
#endif
#ifndef COLOR_3DFACE
#define COLOR_3DFACE 15
#endif
#ifndef COLOR_3DSHADOW
#define COLOR_3DSHADOW 16
#endif
#ifndef COLOR_GRAYTEXT
#define COLOR_GRAYTEXT 17
#endif
#ifndef COLOR_BTNTEXT
#define COLOR_BTNTEXT 18
#endif
#ifndef COLOR_INACTIVECAPTIONTEXT
#define COLOR_INACTIVECAPTIONTEXT 19
#endif
#ifndef COLOR_3DHILIGHT
#define COLOR_3DHILIGHT 20
#endif
#ifndef COLOR_3DDKSHADOW
#define COLOR_3DDKSHADOW 21
#endif
#ifndef COLOR_3DLIGHT
#define COLOR_3DLIGHT 22
#endif
#ifndef COLOR_INFOTEXT
#define COLOR_INFOTEXT 23
#endif
#ifndef COLOR_INFOBK
#define COLOR_INFOBK 24
#endif
#ifndef DLGC_WANTARROWS
#define DLGC_WANTARROWS 0x0001
#endif
#ifndef DLGC_WANTTAB
#define DLGC_WANTTAB 0x0002
#endif
#ifndef DLGC_WANTALLKEYS
#define DLGC_WANTALLKEYS 0x0004
#endif
#ifndef DLGC_WANTMESSAGE
#define DLGC_WANTMESSAGE 0x0004
#endif
#ifndef DLGC_HASSETSEL
#define DLGC_HASSETSEL 0x0008
#endif
#ifndef DLGC_DEFPUSHBUTTON
#define DLGC_DEFPUSHBUTTON 0x0010
#endif
#ifndef DLGC_UNDEFPUSHBUTTON
#define DLGC_UNDEFPUSHBUTTON 0x0020
#endif
#ifndef DLGC_RADIOBUTTON
#define DLGC_RADIOBUTTON 0x0040
#endif
#ifndef DLGC_WANTCHARS
#define DLGC_WANTCHARS 0x0080
#endif
#ifndef DLGC_STATIC
#define DLGC_STATIC 0x0100
#endif
#ifndef DLGC_BUTTON
#define DLGC_BUTTON 0x2000
#endif
#ifndef CLR_INVALID
#define CLR_INVALID 0xFFFFFFFF
#endif
#ifndef GDI_ERROR
#define GDI_ERROR 0xFFFFFFFFL
#endif
#ifndef WM_CANCELMODE
#define WM_CANCELMODE 0x001F
#endif
#ifndef WM_NEXTDLGCTL
#define WM_NEXTDLGCTL 0x0028
#endif
#ifndef WM_VKEYTOITEM
#define WM_VKEYTOITEM 0x002E
#endif
#ifndef WM_CHARTOITEM
#define WM_CHARTOITEM 0x002F
#endif
#ifndef WM_QUERYDRAGICON
#define WM_QUERYDRAGICON 0x0037
#endif
#ifndef WM_COMPAREITEM
#define WM_COMPAREITEM 0x0039
#endif
#ifndef WM_CAPTURECHANGED
#define WM_CAPTURECHANGED 0x0215
#endif
#ifndef SS_NOPREFIX
#define SS_NOPREFIX 0x00000080L
#endif
#ifndef SS_SIMPLE
#define SS_SIMPLE 0x0000000BL
#endif
#ifndef SS_LEFTNOWORDWRAP
#define SS_LEFTNOWORDWRAP 0x0000000CL
#endif
#ifndef SBM_SETPOS
#define SBM_SETPOS 0x00E0
#endif
#ifndef SBM_GETPOS
#define SBM_GETPOS 0x00E1
#endif
#ifndef SBM_SETRANGE
#define SBM_SETRANGE 0x00E2
#endif
#ifndef SBM_GETRANGE
#define SBM_GETRANGE 0x00E3
#endif
#ifndef SBM_SETRANGEREDRAW
#define SBM_SETRANGEREDRAW 0x00E6
#endif
#ifndef SBM_SETSCROLLINFO
#define SBM_SETSCROLLINFO 0x00E9
#endif
#ifndef SBM_GETSCROLLINFO
#define SBM_GETSCROLLINFO 0x00EA
#endif
#ifndef MB_DEFBUTTON1
#define MB_DEFBUTTON1 0x00000000L
#endif
#ifndef MB_DEFBUTTON2
#define MB_DEFBUTTON2 0x00000100L
#endif
#ifndef MB_DEFBUTTON3
#define MB_DEFBUTTON3 0x00000200L
#endif
#ifndef MB_DEFMASK
#define MB_DEFMASK 0x00000F00L
#endif
#ifndef LVM_HITTEST
#define LVM_HITTEST 0x1012
#endif
#ifndef LVHT_NOWHERE
#define LVHT_NOWHERE 0x0001
#endif
#ifndef LVHT_ONITEM
#define LVHT_ONITEM 0x000E
#endif
#ifndef LPSTR_TEXTCALLBACKA
#define LPSTR_TEXTCALLBACKA ((LPSTR)-1L)
#endif
#ifndef ETO_OPAQUE
#define ETO_OPAQUE 0x0002
#endif
#ifndef ETO_CLIPPED
#define ETO_CLIPPED 0x0004
#endif
#ifndef DSTINVERT
#define DSTINVERT 0x00550009
#endif
#ifndef SRCERASE
#define SRCERASE 0x00440328
#endif
#ifndef NOTSRCCOPY
#define NOTSRCCOPY 0x00330008
#endif
#ifndef MERGECOPY
#define MERGECOPY 0x00C000CA
#endif
#ifndef BS_3STATE
#define BS_3STATE 0x00000005L
#endif
#ifndef BS_AUTO3STATE
#define BS_AUTO3STATE 0x00000006L
#endif
#ifndef HORZSIZE
#define HORZSIZE 4
#endif
#ifndef VERTSIZE
#define VERTSIZE 6
#endif
#ifndef TMPF_FIXED_PITCH
#define TMPF_FIXED_PITCH 0x01
#endif
#ifndef TMPF_TRUETYPE
#define TMPF_TRUETYPE 0x04
#endif
#ifndef TBM_GETTHUMBRECT
#define TBM_GETTHUMBRECT 0x0419
#endif
#ifndef TBM_GETCHANNELRECT
#define TBM_GETCHANNELRECT 0x041A
#endif
#ifndef TA_LEFT
#define TA_LEFT 0
#endif
#ifndef TA_RIGHT
#define TA_RIGHT 2
#endif
#ifndef TA_CENTER
#define TA_CENTER 6
#endif
#ifndef TA_TOP
#define TA_TOP 0
#endif
#ifndef TA_BOTTOM
#define TA_BOTTOM 8
#endif
#ifndef TA_BASELINE
#define TA_BASELINE 24
#endif
#ifndef ANSI_VAR_FONT
#define ANSI_VAR_FONT 12
#endif
#ifndef SYSTEM_FONT
#define SYSTEM_FONT 13
#endif
#ifndef SM_MOUSEPRESENT
#define SM_MOUSEPRESENT 19
#endif
#ifndef SM_CYVSCROLL
#define SM_CYVSCROLL 20
#endif
#ifndef SM_CXHSCROLL
#define SM_CXHSCROLL 21
#endif
#ifndef SM_CMOUSEBUTTONS
#define SM_CMOUSEBUTTONS 43
#endif
#ifndef SM_CXEDGE
#define SM_CXEDGE 45
#endif
#ifndef SM_CYEDGE
#define SM_CYEDGE 46
#endif
#ifndef SM_CXMAXIMIZED
#define SM_CXMAXIMIZED 61
#endif
#ifndef SM_CYMAXIMIZED
#define SM_CYMAXIMIZED 62
#endif
#ifndef RC_BITBLT
#define RC_BITBLT 0x0001
#endif
#ifndef RC_DI_BITMAP
#define RC_DI_BITMAP 0x0080
#endif
#ifndef RC_STRETCHBLT
#define RC_STRETCHBLT 0x0800
#endif
#ifndef PS_NULL
#define PS_NULL 5
#endif
#ifndef LVS_NOCOLUMNHEADER
#define LVS_NOCOLUMNHEADER 0x4000
#endif
#ifndef GW_HWNDFIRST
#define GW_HWNDFIRST 0
#endif
#ifndef GW_HWNDLAST
#define GW_HWNDLAST 1
#endif
#ifndef GW_HWNDNEXT
#define GW_HWNDNEXT 2
#endif
#ifndef GW_HWNDPREV
#define GW_HWNDPREV 3
#endif
#ifndef GW_OWNER
#define GW_OWNER 4
#endif
#ifndef GW_CHILD
#define GW_CHILD 5
#endif
#ifndef EM_REPLACESEL
#define EM_REPLACESEL 0x00C2
#endif
#ifndef DM_GETDEFID
#define DM_GETDEFID 0x0400
#endif
#ifndef DC_HASDEFID
#define DC_HASDEFID 0x534B
#endif
#ifndef CB_SETDROPPEDWIDTH
#define CB_SETDROPPEDWIDTH 0x0160
#endif
#ifndef J3D_SHIM_LVHITTESTINFO
#define J3D_SHIM_LVHITTESTINFO
typedef struct tagLVHITTESTINFO
{
    POINT pt;
    UINT flags;
    int iItem;
    int iSubItem;
} LVHITTESTINFO, *LPLVHITTESTINFO;
#endif

#endif /* J3D_PLATFORM_WIN32_SHIM_GUI_H */
