/*
 * POSIX implementations of the MSVC CRT and kernel32 calls the engine makes that have an obvious, exact POSIX
 * equivalent: the _findfirst family, string case helpers, time, sleeping, directories and file deletion.
 *
 * These are real implementations, not null stubs. Everything with no clean equivalent (windows, GDI, the registry,
 * file mapping, threads) lives in null/win32_null.c instead.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wctype.h>

#include <io.h>
#include <Windows.h>

#include <platform/common/host_clock.h>
#include <platform/common/host_path.h>

static DWORD crt_lastError = 0;

/* ----------------------------------------------------------------------------------------------------------------
 * Paths. The engine builds paths with '\' and any letter case; see host_path.h.
 * -------------------------------------------------------------------------------------------------------------- */

// Replaces the component pPath[start, end) with the spelling of a case-insensitive match in its parent directory.
// Returns 0 if there is no match. A case-only change keeps the length, so the edit is in place.
static int crt_ResolveComponent(char* pPath, size_t start, size_t end)
{
    char aDir[1024];
    if ( start == 0 )
    {
        strcpy(aDir, ".");
    }
    else if ( start == 1 )
    {
        strcpy(aDir, "/");
    }
    else
    {
        size_t dirLen = start - 1; // without the separator
        if ( dirLen >= sizeof(aDir) )
        {
            return 0;
        }
        memcpy(aDir, pPath, dirLen);
        aDir[dirLen] = '\0';
    }

    DIR* pDir = opendir(aDir);
    if ( !pDir )
    {
        return 0;
    }

    int bFound = 0;
    size_t len = end - start;
    struct dirent* pEntry;
    while ( (pEntry = readdir(pDir)) != NULL )
    {
        if ( strlen(pEntry->d_name) == len && strncasecmp(pEntry->d_name, pPath + start, len) == 0 )
        {
            memcpy(pPath + start, pEntry->d_name, len);
            bFound = 1;
            break;
        }
    }
    closedir(pDir);
    return bFound;
}

void J3DHost_ToPosixPath(char* pDst, size_t size, const char* pSrc)
{
    size_t i;
    for ( i = 0; pSrc[i] && i < size - 1; ++i )
    {
        pDst[i] = pSrc[i] == '\\' ? '/' : pSrc[i];
    }
    pDst[i] = '\0';

    struct stat st;
    if ( !pDst[0] || lstat(pDst, &st) == 0 )
    {
        return; // the common case: spelled right, or nothing to resolve
    }

    // Walk the components. Each one that exists as spelled is kept; one that does not is matched case-insensitively.
    // The first that has no match at all ends the walk: it and everything after it is a name to be created.
    size_t start = pDst[0] == '/' ? 1 : 0;
    while ( pDst[start] )
    {
        size_t end = start;
        while ( pDst[end] && pDst[end] != '/' )
        {
            ++end;
        }

        if ( end > start )
        {
            char saved = pDst[end];
            pDst[end] = '\0';
            int bExists = lstat(pDst, &st) == 0;
            pDst[end] = saved;

            if ( !bExists && !crt_ResolveComponent(pDst, start, end) )
            {
                return;
            }
        }

        if ( !pDst[end] )
        {
            break;
        }
        start = end + 1;
    }
}

FILE* J3DHost_fopen(const char* pPath, const char* pMode)
{
    char aPath[1024];
    J3DHost_ToPosixPath(aPath, sizeof(aPath), pPath);
    return fopen(aPath, pMode);
}

/* ----------------------------------------------------------------------------------------------------------------
 * _findfirst / _findnext / _findclose (io.h)
 * -------------------------------------------------------------------------------------------------------------- */
typedef struct CrtFind
{
    DIR* pDir;
    char aDir[1024];
    char aPattern[256];
} CrtFind;

static int crt_FindFill(CrtFind* pFind, struct _finddata_t* pInfo)
{
    struct dirent* pEntry;
    while ( (pEntry = readdir(pFind->pDir)) != NULL )
    {
        // Windows file matching is case-insensitive, and "*.*" matches names without a dot as well.
        if ( strcmp(pFind->aPattern, "*.*") != 0 && fnmatch(pFind->aPattern, pEntry->d_name, FNM_CASEFOLD) != 0 )
        {
            continue;
        }

        char aPath[1300];
        struct stat st;
        snprintf(aPath, sizeof(aPath), "%s/%s", pFind->aDir, pEntry->d_name);
        memset(pInfo, 0, sizeof(*pInfo));
        if ( stat(aPath, &st) == 0 )
        {
            pInfo->attrib      = S_ISDIR(st.st_mode) ? _A_SUBDIR : _A_NORMAL;
            pInfo->time_create = st.st_ctime;
            pInfo->time_access = st.st_atime;
            pInfo->time_write  = st.st_mtime;
            pInfo->size        = (size_t)st.st_size;
        }
        strncpy(pInfo->name, pEntry->d_name, sizeof(pInfo->name) - 1);
        return 0;
    }
    return -1;
}

intptr_t _findfirst(const char* pFilespec, struct _finddata_t* pFileInfo)
{
    char aSpec[1024];
    J3DHost_ToPosixPath(aSpec, sizeof(aSpec), pFilespec);

    CrtFind* pFind = calloc(1, sizeof(CrtFind));
    if ( !pFind )
    {
        return -1;
    }

    char* pSlash = strrchr(aSpec, '/');
    if ( pSlash )
    {
        *pSlash = '\0';
        strncpy(pFind->aDir, aSpec[0] ? aSpec : "/", sizeof(pFind->aDir) - 1);
        strncpy(pFind->aPattern, pSlash + 1, sizeof(pFind->aPattern) - 1);
    }
    else
    {
        strcpy(pFind->aDir, ".");
        strncpy(pFind->aPattern, aSpec, sizeof(pFind->aPattern) - 1);
    }

    pFind->pDir = opendir(pFind->aDir);
    if ( !pFind->pDir || crt_FindFill(pFind, pFileInfo) != 0 )
    {
        if ( pFind->pDir )
        {
            closedir(pFind->pDir);
        }
        free(pFind);
        errno = ENOENT;
        return -1;
    }
    return (intptr_t)pFind;
}

int _findnext(intptr_t handle, struct _finddata_t* pFileInfo)
{
    if ( handle == -1 || !handle )
    {
        return -1;
    }
    return crt_FindFill((CrtFind*)handle, pFileInfo);
}

int _findclose(intptr_t handle)
{
    if ( handle == -1 || !handle )
    {
        return -1;
    }
    CrtFind* pFind = (CrtFind*)handle;
    closedir(pFind->pDir);
    free(pFind);
    return 0;
}

/* FindFirstFileA family, on top of _findfirst. */
static void crt_FillFindData(const struct _finddata_t* pInfo, LPWIN32_FIND_DATAA pData)
{
    memset(pData, 0, sizeof(*pData));
    pData->dwFileAttributes = (pInfo->attrib & _A_SUBDIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    pData->nFileSizeLow     = (DWORD)pInfo->size;
    pData->nFileSizeHigh    = (DWORD)((uint64_t)pInfo->size >> 32);
    strncpy(pData->cFileName, pInfo->name, sizeof(pData->cFileName) - 1);
}

HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    struct _finddata_t info;
    intptr_t h = _findfirst(lpFileName, &info);
    if ( h == -1 )
    {
        crt_lastError = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    crt_FillFindData(&info, lpFindFileData);
    return (HANDLE)h;
}

BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
    struct _finddata_t info;
    if ( _findnext((intptr_t)hFindFile, &info) != 0 )
    {
        crt_lastError = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    crt_FillFindData(&info, lpFindFileData);
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE hFindFile)
{
    return _findclose((intptr_t)hFindFile) == 0;
}

/* ----------------------------------------------------------------------------------------------------------------
 * Directories and files
 * -------------------------------------------------------------------------------------------------------------- */
int _mkdir(const char* pPath)
{
    char aPath[1024];
    J3DHost_ToPosixPath(aPath, sizeof(aPath), pPath);
    return mkdir(aPath, 0755);
}

BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    (void)lpSecurityAttributes;
    if ( _mkdir(lpPathName) != 0 )
    {
        crt_lastError = errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_PATH_NOT_FOUND;
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR lpPathName)
{
    char aPath[1024];
    J3DHost_ToPosixPath(aPath, sizeof(aPath), lpPathName);
    return rmdir(aPath) == 0;
}

BOOL WINAPI DeleteFileA(LPCSTR lpFileName)
{
    char aPath[1024];
    J3DHost_ToPosixPath(aPath, sizeof(aPath), lpFileName);
    return unlink(aPath) == 0;
}

DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
    char aPath[1024];
    if ( !getcwd(aPath, sizeof(aPath)) )
    {
        return 0;
    }
    DWORD len = (DWORD)strlen(aPath);
    if ( !lpBuffer || nBufferLength <= len )
    {
        return len + 1;
    }
    memcpy(lpBuffer, aPath, len + 1);
    return len;
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR lpPathName)
{
    char aPath[1024];
    J3DHost_ToPosixPath(aPath, sizeof(aPath), lpPathName);
    return chdir(aPath) == 0;
}

/* ----------------------------------------------------------------------------------------------------------------
 * Time
 * -------------------------------------------------------------------------------------------------------------- */
static uint64_t crt_HostNowMsec(void)
{
    static mach_timebase_info_data_t timebase;
    if ( timebase.denom == 0 )
    {
        mach_timebase_info(&timebase);
    }
    return mach_absolute_time() * timebase.numer / timebase.denom / 1000000u;
}

// The virtual clock of host_clock.h: when on, time is crt_virtualMsec and only J3DHost_AdvanceClock and Sleep move it.
static int      crt_bVirtualClock = 0;
static uint64_t crt_virtualMsec   = 0;

void J3DHost_UseVirtualClock(void)
{
    if ( !crt_bVirtualClock )
    {
        crt_virtualMsec   = crt_HostNowMsec();
        crt_bVirtualClock = 1;
    }
}

int J3DHost_IsVirtualClock(void)
{
    return crt_bVirtualClock;
}

void J3DHost_AdvanceClock(unsigned int msec)
{
    if ( crt_bVirtualClock )
    {
        crt_virtualMsec += msec;
    }
}

static uint64_t crt_NowMsec(void)
{
    return crt_bVirtualClock ? crt_virtualMsec : crt_HostNowMsec();
}

// winmm's timeGetTime counts from system start and wraps at 2^32 ms; the engine only takes differences.
DWORD WINAPI timeGetTime(void)
{
    return (DWORD)crt_NowMsec();
}

DWORD WINAPI GetTickCount(void)
{
    return (DWORD)crt_NowMsec();
}

void WINAPI Sleep(DWORD dwMilliseconds)
{
    if ( crt_bVirtualClock )
    {
        crt_virtualMsec += dwMilliseconds; // the time passes without waiting for it
        return;
    }
    usleep((useconds_t)dwMilliseconds * 1000u);
}

static void crt_FillSystemTime(const struct tm* pTm, long msec, LPSYSTEMTIME pTime)
{
    pTime->wYear         = (WORD)(pTm->tm_year + 1900);
    pTime->wMonth        = (WORD)(pTm->tm_mon + 1);
    pTime->wDayOfWeek    = (WORD)pTm->tm_wday;
    pTime->wDay          = (WORD)pTm->tm_mday;
    pTime->wHour         = (WORD)pTm->tm_hour;
    pTime->wMinute       = (WORD)pTm->tm_min;
    pTime->wSecond       = (WORD)pTm->tm_sec;
    pTime->wMilliseconds = (WORD)msec;
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    crt_FillSystemTime(&tm, ts.tv_nsec / 1000000, lpSystemTime);
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    crt_FillSystemTime(&tm, ts.tv_nsec / 1000000, lpSystemTime);
}

/* ----------------------------------------------------------------------------------------------------------------
 * Errors, process
 * -------------------------------------------------------------------------------------------------------------- */
DWORD WINAPI GetLastError(void)
{
    return crt_lastError;
}

void WINAPI SetLastError(DWORD dwErrCode)
{
    crt_lastError = dwErrCode;
}

void WINAPI OutputDebugStringA(LPCSTR lpOutputString)
{
    fputs(lpOutputString, stderr);
}

BOOL WINAPI GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize)
{
    char aName[256];
    if ( gethostname(aName, sizeof(aName)) != 0 )
    {
        return FALSE;
    }
    aName[sizeof(aName) - 1] = '\0';
    DWORD len = (DWORD)strlen(aName);
    if ( *nSize <= len )
    {
        *nSize = len + 1;
        crt_lastError = ERROR_MORE_DATA;
        return FALSE;
    }
    memcpy(lpBuffer, aName, len + 1);
    *nSize = len;
    return TRUE;
}

void WINAPI DebugBreak(void)
{
    __builtin_debugtrap();
}

/* ----------------------------------------------------------------------------------------------------------------
 * Arithmetic and rectangles (pure functions)
 * -------------------------------------------------------------------------------------------------------------- */
int WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator)
{
    if ( nDenominator == 0 )
    {
        return -1;
    }
    // Windows rounds half away from zero.
    int64_t product = (int64_t)nNumber * nNumerator;
    int64_t half    = (nDenominator < 0 ? -nDenominator : nDenominator) / 2;
    int64_t result  = ((product < 0) != (nDenominator < 0)) ? (product - half) / nDenominator : (product + half) / nDenominator;
    return (int)result;
}

BOOL WINAPI PtInRect(const RECT* lprc, POINT pt)
{
    return lprc && pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom;
}

/* ----------------------------------------------------------------------------------------------------------------
 * MSVC string helpers
 * -------------------------------------------------------------------------------------------------------------- */
char* _strupr(char* s)
{
    for ( char* p = s; *p; ++p )
    {
        *p = (char)toupper((unsigned char)*p);
    }
    return s;
}

char* _strlwr(char* s)
{
    for ( char* p = s; *p; ++p )
    {
        *p = (char)tolower((unsigned char)*p);
    }
    return s;
}

wchar_t* _wcsupr(wchar_t* s)
{
    for ( wchar_t* p = s; *p; ++p )
    {
        *p = (wchar_t)towupper((wint_t)*p);
    }
    return s;
}

wchar_t* _wcslwr(wchar_t* s)
{
    for ( wchar_t* p = s; *p; ++p )
    {
        *p = (wchar_t)towlower((wint_t)*p);
    }
    return s;
}

static char* crt_FormatInt(long long value, char* str, int radix)
{
    // MSVC: only base 10 prints a sign; other bases print the two's-complement value of the 32-bit int.
    unsigned long long u = (radix == 10 && value < 0) ? (unsigned long long)(-value) : (unsigned long long)(uint32_t)value;
    char aTmp[66];
    int n = 0;
    if ( radix < 2 || radix > 36 )
    {
        str[0] = '\0';
        return str;
    }
    do
    {
        int digit = (int)(u % (unsigned)radix);
        aTmp[n++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        u /= (unsigned)radix;
    } while ( u );

    char* p = str;
    if ( radix == 10 && value < 0 )
    {
        *p++ = '-';
    }
    while ( n )
    {
        *p++ = aTmp[--n];
    }
    *p = '\0';
    return str;
}

char* _itoa(int value, char* str, int radix)
{
    return crt_FormatInt(value, str, radix);
}

char* _ltoa(long value, char* str, int radix)
{
    return crt_FormatInt((int32_t)value, str, radix); // MSVC long is 32-bit
}

wchar_t* _itow(int value, wchar_t* str, int radix)
{
    char aTmp[40];
    crt_FormatInt(value, aTmp, radix);
    size_t i;
    for ( i = 0; aTmp[i]; ++i )
    {
        str[i] = (wchar_t)aTmp[i];
    }
    str[i] = L'\0';
    return str;
}
