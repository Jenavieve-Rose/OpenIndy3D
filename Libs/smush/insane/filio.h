#ifndef SMUSH_INSANE_FILIO_H
#define SMUSH_INSANE_FILIO_H

/*
 * INSANE's file-I/O interface (`FILIO=<module>`): seven entry points, implemented by insane_stdio (C stdio),
 * insane_zfilio (its `.znm` layer) and reached through insane_stdfile (`FILE=insane_stdfile`), which adds the
 * derived operations. The handle is an opaque 32-bit value (retail's getc/putc take the address of the handle
 * parameter itself as the byte buffer, and mask the result with 0xFF).
 *
 * Port: the handle is intptr_t. Every implementation returns a pointer (a FILE* or a ZfileHandle*) in it, which a
 * 32-bit int truncates on arm64.
 */
#include <stdint.h>

typedef intptr_t FilioHandle;

typedef struct FilioInterface
{
    FilioHandle (*Open)(const char* pPath, const char* pMode);
    void (*Close)(FilioHandle fh);
    int  (*Read)(FilioHandle fh, void* pBuf, int size);
    int  (*Write)(FilioHandle fh, const void* pBuf, int size);
    int  (*Tell)(FilioHandle fh);
    int  (*Seek)(FilioHandle fh, int offset, int whence);
    void (*Flush)(FilioHandle fh);
} FilioInterface;

#endif
