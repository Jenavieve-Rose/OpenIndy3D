/*
 * insane_stdio: INSANE's `FILIO=insane_stdio` file-I/O module, a thin layer over the C runtime's stdio
 * (retail 0x004EA020-0x004EA0F0). Its registration, open, close and flush wrappers are here; its read, write,
 * tell and seek wrappers are byte-identical to the game's std file wrappers, which LINK folded together
 *
 * Port (PORT-PLAN.md P5.2): the handle is a FilioHandle (a FILE*, which an int truncated on 64-bit), and paths go
 * through the host path conversion (J3DHost_fopen), so a movie path in the engine's spelling finds the file.
 */
#include <stdio.h>
#include <platform/common/host_path.h>
#include "../lec.h"
#include "../filio.h"

int stdio_Register(LecShare* pShare, void* pParam);
static void stdio_Unregister(void);
static int stdio_Startup(void);
static void stdio_Shutdown(void);
static FilioHandle stdio_open(const char* pPath, const char* pMode);
static void stdio_close(FilioHandle fh);
static int stdio_read(FilioHandle fh, void* pBuf, int size);
static int stdio_write(FilioHandle fh, const void* pBuf, int size);
static int stdio_tell(FilioHandle fh);
static int stdio_seek(FilioHandle fh, int offset, int whence);
static void stdio_flush(FilioHandle fh);

static LecModule stdio_module = { stdio_Register, stdio_Unregister, stdio_Startup, stdio_Shutdown };
static FilioInterface stdio_filio = { stdio_open, stdio_close, stdio_read, stdio_write, stdio_tell, stdio_seek, stdio_flush };

static void* stdio_pParam;
static LecShare* stdio_pShare;

int stdio_Register(LecShare* pShare, void* pParam)
{
    stdio_pShare = pShare;
    stdio_pParam = pParam;
    pShare->Publish("LECMODULE=insane_stdio", &stdio_module);
    stdio_pShare->Publish("FILIO=insane_stdio", &stdio_filio);
    return 0;
}

static void stdio_Unregister(void)
{
    stdio_pShare->Unpublish("FILIO=insane_stdio");
    stdio_pShare->Unpublish("LECMODULE=insane_stdio");
}

static int stdio_Startup(void)
{
    return 0;
}

static void stdio_Shutdown(void)
{
}

static FilioHandle stdio_open(const char* pPath, const char* pMode)
{
    if (*pMode == 'w') {
        char aPath[1024];
        J3DHost_ToPosixPath(aPath, sizeof(aPath), pPath);
        remove(aPath);
    }
    return (FilioHandle)J3DHost_fopen(pPath, pMode);
}

static void stdio_close(FilioHandle fh)
{
    fclose((FILE*)fh);
}

static int stdio_read(FilioHandle fh, void* pBuf, int size)
{
    return (int)fread(pBuf, 1, size, (FILE*)fh);
}

static int stdio_write(FilioHandle fh, const void* pBuf, int size)
{
    return (int)fwrite(pBuf, 1, size, (FILE*)fh);
}

static int stdio_tell(FilioHandle fh)
{
    return (int)ftell((FILE*)fh); // Port: narrowing cast (movies are far below 2 GB)
}

static int stdio_seek(FilioHandle fh, int offset, int whence)
{
    return fseek((FILE*)fh, offset, whence);
}

static void stdio_flush(FilioHandle fh)
{
    fflush((FILE*)fh);
}
