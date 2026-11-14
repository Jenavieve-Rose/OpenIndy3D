/*
 * MixerAsm.c: the C translation of MixerAsm.asm, INSANE's sound-mixer buffer helpers (retail 0x004EC560-0x004EC720;
 * PORT-PLAN.md P5.6).
 *
 * Engine.c calls mixer_MemCopy and mixer_MemFill; nothing in the image calls the other six, which are translated so
 * the file is whole. Arguments as in the assembler: pDst, pSrc or value, n, and for the table procedures pTables and a
 * table index (512-byte word tables, 256-byte byte tables). The 16-bit segment _TEXT16 (`int 1Bh; iret; iret`) has no
 * meaning outside real-mode DOS and nothing references it; it has no translation.
 *
 * Counts: the assembler's `loop` and `rep` with a count of 0 or less ran for about 2^32 iterations (`loop`) or did
 * nothing (`rep`); no caller passes one, and here such a count does nothing.
 */
#include <stdint.h>
#include <string.h>

// n bytes, ascending (rep movsw / movsb, or rep movsd when n is a multiple of 4). Engine.c's sound_Copy.
void mixer_MemCopy(void* pDst, const void* pSrc, int n)
{
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    size_t count;

    if (n <= 0) {
        return;
    }
    count = (size_t)n;
    if (d + count <= s || s + count <= d) {
        memcpy(d, s, count);
        return;
    }
    while (count--) {
        *d++ = *s++; // overlapping (no caller does): ascending, as the string instructions
    }
}

// n bytes of the low byte of value (20000-byte rep stosd runs, then words and a byte, or dwords). Engine.c's sound_Fill.
void mixer_MemFill(void* pDst, int value, int n)
{
    if (n > 0) {
        memset(pDst, value & 0xFF, (size_t)n);
    }
}

// n bytes through word table `index`: pDst[i] = pTables[index][pSrc[i]] (lodsb; mov ax,[ebx+eax*2]; stosw).
void mixer_Lut16(void* pDst, const void* pSrc, int n, const void* pTables, int index)
{
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    const unsigned char* pTable = (const unsigned char*)pTables + ((uint32_t)index << 9);

    for (; n > 0; --n) {
        memcpy(d, pTable + 2 * *s++, 2);
        d += 2;
    }
}

// As mixer_Lut16, adding to the words already in pDst (16-bit wrap-around, `add ax,[edi]`).
void mixer_Lut16Add(void* pDst, const void* pSrc, int n, const void* pTables, int index)
{
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    const unsigned char* pTable = (const unsigned char*)pTables + ((uint32_t)index << 9);

    for (; n > 0; --n) {
        uint16_t a, b;
        memcpy(&a, pTable + 2 * *s++, 2);
        memcpy(&b, d, 2);
        a = (uint16_t)(a + b);
        memcpy(d, &a, 2);
        d += 2;
    }
}

// n words of value (rep stosw).
void mixer_Fill16(void* pDst, int value, int n)
{
    unsigned char* d = (unsigned char*)pDst;
    uint16_t w = (uint16_t)value;

    for (; n > 0; --n) {
        memcpy(d, &w, 2);
        d += 2;
    }
}

// n bytes through byte table `index`: pDst[i] = pTables[index][pSrc[i]].
void mixer_Lut8(void* pDst, const void* pSrc, int n, const void* pTables, int index)
{
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    const unsigned char* pTable = (const unsigned char*)pTables + ((uint32_t)index << 8);

    for (; n > 0; --n) {
        *d++ = pTable[*s++];
    }
}

// As mixer_Lut8, adding to the bytes already in pDst (8-bit wrap-around).
void mixer_Lut8Add(void* pDst, const void* pSrc, int n, const void* pTables, int index)
{
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    const unsigned char* pTable = (const unsigned char*)pTables + ((uint32_t)index << 8);

    for (; n > 0; --n) {
        *d = (unsigned char)(*d + pTable[*s++]);
        d++;
    }
}

// Retail's test is inverted (`test ecx,3; jne` to the dword path): a count that is a multiple of 4 is stored as words
// and a byte, n bytes in all; any other count is stored as n >> 2 dwords, dropping the n & 3 odd bytes. Kept.
void mixer_FillB(void* pDst, int value, int n)
{
    if (n <= 0) {
        return;
    }
    if ((n & 3) == 0) {
        memset(pDst, value & 0xFF, (size_t)n);
    }
    else {
        memset(pDst, value & 0xFF, (size_t)(n >> 2) * 4);
    }
}
