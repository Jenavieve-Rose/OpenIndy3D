/* Port stand-in for MSVC <io.h>: the _findfirst family (implemented in platform/common/crt_posix.c). */
#pragma once
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "win32_shim.h"

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

struct _finddata_t
{
    unsigned attrib;
    time_t   time_create;
    time_t   time_access;
    time_t   time_write;
    size_t   size;
    char     name[260];
};

// The handle is intptr_t, as in MSVC since VC 2005; -1 means failure.
intptr_t _findfirst(const char* pFilespec, struct _finddata_t* pFileInfo);
int      _findnext(intptr_t handle, struct _finddata_t* pFileInfo);
int      _findclose(intptr_t handle);
