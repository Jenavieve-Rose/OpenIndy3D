#ifndef J3D_PLATFORM_WIN32_SHIM_H
#define J3D_PLATFORM_WIN32_SHIM_H
/*
 * The Win32 vocabulary the port keeps (PORT-PLAN.md decision 22): the base types and the kernel32 and CRT functions
 * the imported code names, each implemented for real by the port (platform/common/crt_posix.c), and through
 * win32_shim_gui.h the user32, gdi32 and comctl32 API that the port's Win32 emulation implements (j3dgui, the game
 * window's wkernel). Nothing here is a call that silently does nothing: tools/gen_null_platform.py stops with an error
 * on a declared function nobody implements, apart from the null build's window-message queue.
 *
 * Phase 0 created it as debt, to be deleted in phase 7. Phase 7 deleted what could go: the DirectX part (now the
 * engine's own Libs/std/Win95/stdDirectX.h), the stand-ins for headers nothing includes, every declaration nothing
 * calls, and win32_null.c. What stays is used by design: the dialog shell keeps its Win32 dialog procedures over
 * j3dgui (decisions 9 and 21), the game window procedure takes Win32 messages from wkernel, and the engine's structures
 * use the Win32 integer and handle types. The stand-in names (Windows.h, CommCtrl.h, ...) are kept so the imported
 * sources include what they always did. Rules:
 *   - Types only have the members the engine actually touches, with Win32 names, so field accesses compile.
 *   - Handles are opaque pointers, so they are 64-bit like the pointers they really are.
 *   - Widths follow Windows LLP64: DWORD and LONG are 32-bit on every platform.
 *   - A function is declared here only if compiled code calls it, and it is implemented by crt_posix.c or j3dgui.
 *
 * The stand-in headers next to this file (Windows.h, CommCtrl.h, windowsx.h, ...) all just include it.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------------------------------------------
 * Calling conventions and keywords
 * -------------------------------------------------------------------------------------------------------------- */
#define __cdecl
#define __stdcall
#define __fastcall
#define WINAPI
#define APIENTRY
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR
#define far
#define near
#define CONST const
#define VOID void
#define IN
#define OUT
#define OPTIONAL

/* ----------------------------------------------------------------------------------------------------------------
 * Base types (LLP64 widths)
 * -------------------------------------------------------------------------------------------------------------- */
typedef int             BOOL;
typedef uint8_t         BYTE;
typedef uint16_t        WORD;
typedef uint32_t        DWORD;
typedef uint32_t        UINT;
typedef int32_t         LONG;
typedef uint32_t        ULONG;
typedef int16_t         SHORT;
typedef uint16_t        USHORT;
typedef char            CHAR;
typedef unsigned char   UCHAR;
typedef wchar_t         WCHAR;
typedef float           FLOAT;
typedef int             INT;
typedef int64_t         LONGLONG;
typedef uint64_t        ULONGLONG;
typedef uint64_t        DWORDLONG;
typedef intptr_t        INT_PTR;
typedef uintptr_t       UINT_PTR;
typedef intptr_t        LONG_PTR;
typedef uintptr_t       ULONG_PTR;
typedef uintptr_t       DWORD_PTR;
typedef size_t          SIZE_T;
typedef UINT_PTR        WPARAM;
typedef LONG_PTR        LPARAM;
typedef LONG_PTR        LRESULT;
typedef LONG            HRESULT;
typedef DWORD           COLORREF;
typedef WORD            ATOM;
typedef char            TCHAR;

typedef BOOL*           PBOOL, *LPBOOL;
typedef BYTE*           PBYTE, *LPBYTE;
typedef WORD*           PWORD, *LPWORD;
typedef DWORD*          PDWORD, *LPDWORD;
typedef LONG*           PLONG, *LPLONG;
typedef UINT*           PUINT, *LPUINT;
typedef int*            PINT, *LPINT;
typedef void*           PVOID, *LPVOID;
typedef const void*     LPCVOID;
typedef char*           PSTR, *LPSTR, *LPTSTR;
typedef const char*     PCSTR, *LPCSTR, *LPCTSTR;
typedef wchar_t*        PWSTR, *LPWSTR;
typedef const wchar_t*  PCWSTR, *LPCWSTR;
typedef COLORREF*       LPCOLORREF;

#ifndef TRUE
#   define TRUE  1
#endif
#ifndef FALSE
#   define FALSE 0
#endif
#ifndef MAX_PATH
#   define MAX_PATH 260
#endif

/* Opaque handles. Distinct struct pointer types, as under STRICT, so mixing them up is still a type error. */
#define DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__* name
typedef void* HANDLE;
typedef HANDLE* PHANDLE, *LPHANDLE;
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HGLOBAL__);
DECLARE_HANDLE(HIMAGELIST);
DECLARE_HANDLE(HTREEITEM);
DECLARE_HANDLE(HMONITOR);
typedef HICON HCURSOR;
typedef HINSTANCE HMODULE;
typedef HANDLE HGDIOBJ;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef HKEY* PHKEY;

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
typedef int HFILE;
#define HFILE_ERROR ((HFILE)-1)

/* ----------------------------------------------------------------------------------------------------------------
 * Word / colour helpers
 * -------------------------------------------------------------------------------------------------------------- */
#define MAKEWORD(a, b)   ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b)   ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define LOWORD(l)        ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)        ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w)        ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w)        ((BYTE)((DWORD_PTR)(w) >> 8))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define RGB(r, g, b)     ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb)   (LOBYTE(rgb))
#define GetGValue(rgb)   (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)   (LOBYTE((rgb) >> 16))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define S_OK          ((HRESULT)0)
#define S_FALSE       ((HRESULT)1)
#define E_FAIL        ((HRESULT)0x80004005L)
#define E_NOTIMPL     ((HRESULT)0x80004001L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG  ((HRESULT)0x80070057L)
#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))

/* windef.h defines these unless NOMINMAX; the decomp is written against them. */
#ifndef max
#   define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#   define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* ----------------------------------------------------------------------------------------------------------------
 * Geometry and common structs
 * -------------------------------------------------------------------------------------------------------------- */
typedef struct tagRECT  { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT* LPCRECT;
typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagSIZE  { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;
typedef struct tagPOINTS { SHORT x, y; } POINTS;

typedef struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID, IID, CLSID, *LPGUID, *LPIID, *LPCLSID;
typedef const GUID* LPCGUID;
typedef const GUID* REFGUID;
typedef const IID*  REFIID;
typedef const CLSID* REFCLSID;
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

typedef union _LARGE_INTEGER
{
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME
{
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _WIN32_FIND_DATAA
{
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    CHAR     cFileName[MAX_PATH];
    CHAR     cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATAA, *LPWIN32_FIND_DATA;

#define FILE_ATTRIBUTE_READONLY  0x01
#define FILE_ATTRIBUTE_HIDDEN    0x02
#define FILE_ATTRIBUTE_SYSTEM    0x04
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE   0x20
#define FILE_ATTRIBUTE_NORMAL    0x80

typedef struct _CRITICAL_SECTION { void* opaque[6]; } CRITICAL_SECTION, *LPCRITICAL_SECTION;

typedef struct _SECURITY_ATTRIBUTES
{
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _MEMORYSTATUS
{
    DWORD  dwLength;
    DWORD  dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _OSVERSIONINFOA
{
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR  szCSDVersion[128];
} OSVERSIONINFOA, OSVERSIONINFO, *LPOSVERSIONINFOA, *LPOSVERSIONINFO;
#define VER_PLATFORM_WIN32s        0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT      2

/* ----------------------------------------------------------------------------------------------------------------
 * Errors
 * -------------------------------------------------------------------------------------------------------------- */
#define ERROR_SUCCESS              0L
#define ERROR_FILE_NOT_FOUND       2L
#define ERROR_PATH_NOT_FOUND       3L
#define ERROR_ACCESS_DENIED        5L
#define ERROR_INVALID_HANDLE       6L
#define ERROR_NO_MORE_FILES        18L
#define ERROR_MORE_DATA            234L
#define ERROR_ALREADY_EXISTS       183L
#define NO_ERROR                   0L

/* ----------------------------------------------------------------------------------------------------------------
 * Kernel
 * -------------------------------------------------------------------------------------------------------------- */
#define INFINITE         0xFFFFFFFF
#define WAIT_OBJECT_0    0x00000000L
#define WAIT_TIMEOUT     0x00000102L
#define WAIT_FAILED      0xFFFFFFFF

#define GENERIC_READ     0x80000000L
#define GENERIC_WRITE    0x40000000L
#define FILE_SHARE_READ  0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW        1
#define CREATE_ALWAYS     2
#define OPEN_EXISTING     3
#define OPEN_ALWAYS       4
#define TRUNCATE_EXISTING 5
#define FILE_BEGIN       0
#define FILE_CURRENT     1
#define FILE_END         2
#define PAGE_READONLY    0x02
#define PAGE_READWRITE   0x04
#define FILE_MAP_READ    0x0004
#define FILE_MAP_WRITE   0x0002
#define FILE_FLAG_RANDOM_ACCESS 0x10000000

#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15

typedef DWORD (WINAPI* LPTHREAD_START_ROUTINE)(LPVOID);
typedef intptr_t (WINAPI* FARPROC)(void);

DWORD   WINAPI GetLastError(void);
void    WINAPI SetLastError(DWORD dwErrCode);
void    WINAPI Sleep(DWORD);
DWORD   WINAPI GetTickCount(void);
DWORD   WINAPI timeGetTime(void);
void    WINAPI GetLocalTime(LPSYSTEMTIME);
void    WINAPI GetSystemTime(LPSYSTEMTIME);
void    WINAPI OutputDebugStringA(LPCSTR);
#define OutputDebugString OutputDebugStringA
DWORD   WINAPI GetCurrentDirectoryA(DWORD, LPSTR);
#define GetCurrentDirectory GetCurrentDirectoryA
BOOL    WINAPI SetCurrentDirectoryA(LPCSTR);
#define SetCurrentDirectory SetCurrentDirectoryA
BOOL    WINAPI CreateDirectoryA(LPCSTR, LPSECURITY_ATTRIBUTES);
#define CreateDirectory CreateDirectoryA
BOOL    WINAPI RemoveDirectoryA(LPCSTR);
#define RemoveDirectory RemoveDirectoryA
BOOL    WINAPI DeleteFileA(LPCSTR);
#define DeleteFile DeleteFileA
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6
HANDLE  WINAPI FindFirstFileA(LPCSTR, LPWIN32_FIND_DATAA);
#define FindFirstFile FindFirstFileA
BOOL    WINAPI FindNextFileA(HANDLE, LPWIN32_FIND_DATAA);
#define FindNextFile FindNextFileA
BOOL    WINAPI FindClose(HANDLE);
#define CP_ACP 0

/* ----------------------------------------------------------------------------------------------------------------
 * MSVC CRT names
 * -------------------------------------------------------------------------------------------------------------- */
#define _snprintf   snprintf
#define _vsnprintf  vsnprintf
#define _snwprintf  swprintf
#define _vsnwprintf vswprintf
#define _stricmp    strcasecmp
#define _strnicmp   strncasecmp
#define _strcmpi    strcasecmp
#define stricmp     strcasecmp
#define strnicmp    strncasecmp
#define _wcsicmp    wcscasecmp
#define _wcsnicmp   wcsncasecmp
#define _strdup     strdup
#define _unlink     unlink
#define _access     access
#define _getcwd     getcwd
#define _chdir      chdir
#define _fileno     fileno
#define _finite     isfinite
#define _isnan      isnan
#define _MAX_PATH   260
#define _MAX_DRIVE  3
#define _MAX_DIR    256
#define _MAX_FNAME  256
#define _MAX_EXT    256
char*    _strupr(char* s);
char*    _strlwr(char* s);
wchar_t* _wcsupr(wchar_t* s);
wchar_t* _wcslwr(wchar_t* s);
char*    _itoa(int value, char* str, int radix);
char*    _ltoa(long value, char* str, int radix);
wchar_t* _itow(int value, wchar_t* str, int radix);
int      _mkdir(const char* path);
#define strupr _strupr
#define strlwr _strlwr
#define itoa   _itoa
#define ltoa   _ltoa

/* ----------------------------------------------------------------------------------------------------------------
 * Registry (wuRegistry is the only user; it is replaced by the platform layer)
 * -------------------------------------------------------------------------------------------------------------- */
#define HKEY_CLASSES_ROOT  ((HKEY)(ULONG_PTR)((LONG)0x80000000))
#define HKEY_CURRENT_USER  ((HKEY)(ULONG_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#define REG_SZ     1
#define REG_BINARY 3
#define REG_DWORD  4
#define KEY_ALL_ACCESS 0xF003F
#define KEY_READ       0x20019
#define KEY_WRITE      0x20006
#define REG_OPTION_NON_VOLATILE 0
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

/* ----------------------------------------------------------------------------------------------------------------
 * Console (stdConsole)
 * -------------------------------------------------------------------------------------------------------------- */
#define FOREGROUND_BLUE      0x0001
#define FOREGROUND_GREEN     0x0002
#define FOREGROUND_RED       0x0004
#define FOREGROUND_INTENSITY 0x0008
#define BACKGROUND_BLUE      0x0010
#define BACKGROUND_GREEN     0x0020
#define BACKGROUND_RED       0x0040
#define BACKGROUND_INTENSITY 0x0080

#ifdef __cplusplus
}
#endif

#include "win32_shim_gui.h"

#endif /* J3D_PLATFORM_WIN32_SHIM_H */
