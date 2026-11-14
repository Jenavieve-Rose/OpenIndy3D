/*
 * MemFill.c: the C translation of MemFill.asm, INSANE's byte fill blit_MemCopyB (retail 0x004F04F8-0x004F054F;
 * PORT-PLAN.md P5.4).
 *
 * blit_MemCopyB(pDst, value, size) replicates the low byte of value and stores size bytes: 20000-byte rep stosd runs,
 * then the size & 3 odd bytes, then whole dwords. Every byte written is the low byte of value, so it is memset.
 * Framestream.c's framestream_CopyB calls it.
 */
#include <string.h>

void blit_MemCopyB(void* pDst, unsigned char value, int size)
{
    if (size > 0) { // retail: a negative size ran rep stosd for ~4G dwords; no caller passes one
        memset(pDst, value, (size_t)size);
    }
}
