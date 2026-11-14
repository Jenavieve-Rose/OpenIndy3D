/*
 * MemCopy.c: the C translation of MemCopy.asm, INSANE's block copy blit_MemCopy (retail 0x004F04D0-0x004F04F5;
 * PORT-PLAN.md P5.4). The assembler source stays in the tree as the decomp's record; this file replaces it in the port.
 *
 * blit_MemCopy(pDst, pSrc, size) copies size bytes, the size & 3 odd bytes first (rep movsb), then size >> 2 dwords
 * (rep movsd), both ascending. Framestream.c's framestream_Copy calls it. Every caller copies between separate regions;
 * an overlapping call is kept to the ascending order the string instructions used.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void blit_MemCopy(void* pDst, const void* pSrc, int size)
{
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    size_t n;

    if (size <= 0) {
        return; // retail: a negative count made rep movsd run for ~4G dwords; no caller passes one
    }
    n = (size_t)size;
    if (d + n <= s || s + n <= d) {
        memcpy(d, s, n); // the same bytes in the same places as the two ascending string copies
        return;
    }
    while (n--) {
        *d++ = *s++; // overlapping: ascending, as rep movs
    }
}
