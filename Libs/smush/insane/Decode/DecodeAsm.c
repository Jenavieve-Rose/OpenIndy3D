/*
 * DecodeAsm.c: the C translation of DecodeAsm.asm, INSANE's pixel-format converters (retail 0x004F0590-0x004F09BA;
 * PORT-PLAN.md P5.5). Decode.c calls all eight (decode_Convert, decode_ToRGBA, decode_Begin).
 *
 * RGBA here is INSANE's 32-bit byte form: R, G, B, A in memory order. The formulas below are what the register code
 * computes; each was derived bit by bit from the assembler and is checked against a literal register-level model of it
 * for every 16-bit input (tests/moviecheck.c, P5.8):
 *
 *   RGBAto555   ((R & 0xF8) << 7) | ((G & 0xF8) << 2) | (B >> 3)
 *   RGBAto565   ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)
 *   RGBAto565B  ((B & 0xF8) << 8) | ((G & 0xFC) << 3) | (R >> 3)            (565 with blue high)
 *   555toRGBA   R << 3 | 7, G << 3 | 7, B << 3 | 7, 0xFF                     (or 0xFF070707)
 *   565toRGBA   R << 3 | 7, G << 2 | 3, B << 3 | 7, 0xFF                     (or 0xFF070307)
 *   565BtoRGBA  R << 3,     G << 2,     B << 3,     0                        (no fill, alpha 0)
 *
 * Where the assembler read or wrote outside the caller's buffers the C does not, with the same result in every pixel
 * the caller asked for:
 *   - The six RGBA converters handle an odd first pixel, then pairs in a `dec ebp; jg` loop, which runs once even with
 *     no pair left: a count of 1 (blit_Fill converts one pixel) converted a second, garbage pair past both buffers.
 *   - The table lookups loop with `dec ecx; jg`, which converts one pixel for a count of 0.
 *   - The 3-byte destinations were written as dwords, one byte past each pixel, and read the table as dwords.
 */
#include <stdint.h>
#include <string.h>

static uint16_t decodeasm_Rd16(const unsigned char* p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void decodeasm_Wr16(unsigned char* p, uint16_t v)
{
    memcpy(p, &v, sizeof(v));
}

static void decodeasm_Wr32(unsigned char* p, uint32_t v)
{
    memcpy(p, &v, sizeof(v));
}

// Maps n 8-bit (Lut8) or 16-bit (Lut16) source pixels through pTable to dstSize-byte destination pixels. The table
// holds bytes, words, 3-byte entries (read at index * 3) or dwords. Any other dstSize does nothing.
static void decodeasm_Lut(unsigned char* d, const unsigned char* s, int n, int dstSize, const unsigned char* pTable, int srcSize)
{
    if (n <= 0) {
        return;
    }
    while (n-- > 0) {
        unsigned index = srcSize == 1 ? s[0] : decodeasm_Rd16(s);
        s += srcSize;
        switch (dstSize & 0xFF) { // `cmp bl,N`: only the low byte of dstSize is tested
            case 1:
                *d = pTable[index];
                break;
            case 2:
                memcpy(d, pTable + index * 2, 2);
                break;
            case 3:
                memcpy(d, pTable + index * 3, 3);
                break;
            case 4:
                memcpy(d, pTable + index * 4, 4);
                break;
            default:
                return;
        }
        d += dstSize & 0xFF;
    }
}

void decode_Lut8(void* pDst, const void* pSrc, int n, int dstSize, const void* pTable)
{
    decodeasm_Lut(pDst, pSrc, n, dstSize, pTable, 1);
}

void decode_Lut16(void* pDst, const void* pSrc, int n, int dstSize, const void* pTable)
{
    decodeasm_Lut(pDst, pSrc, n, dstSize, pTable, 2);
}

/* ---- RGBA to 16-bit ---------------------------------------------------------------------------------------------- */

static uint16_t decodeasm_To555(const unsigned char* p)
{
    return (uint16_t)(((p[0] & 0xF8) << 7) | ((p[1] & 0xF8) << 2) | (p[2] >> 3));
}

static uint16_t decodeasm_To565(const unsigned char* p)
{
    return (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
}

static uint16_t decodeasm_To565B(const unsigned char* p)
{
    return (uint16_t)(((p[2] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[0] >> 3));
}

static void decodeasm_FromRGBA(unsigned char* d, const unsigned char* s, int n, uint16_t (*pfConvert)(const unsigned char*))
{
    for (; n > 0; --n) {
        decodeasm_Wr16(d, pfConvert(s));
        s += 4;
        d += 2;
    }
}

void decode_RGBAto555(void* pDst, const void* pSrc, int n)
{
    decodeasm_FromRGBA(pDst, pSrc, n, decodeasm_To555);
}

void decode_RGBAto565(void* pDst, const void* pSrc, int n)
{
    decodeasm_FromRGBA(pDst, pSrc, n, decodeasm_To565);
}

void decode_RGBAto565B(void* pDst, const void* pSrc, int n)
{
    decodeasm_FromRGBA(pDst, pSrc, n, decodeasm_To565B);
}

/* ---- 16-bit to RGBA ---------------------------------------------------------------------------------------------- */

static uint32_t decodeasm_From555(uint16_t w)
{
    uint32_t r = (w >> 10) & 0x1F, g = (w >> 5) & 0x1F, b = w & 0x1F;
    return ((r << 3) | 7) | (((g << 3) | 7) << 8) | (((b << 3) | 7) << 16) | 0xFF000000u;
}

static uint32_t decodeasm_From565(uint16_t w)
{
    uint32_t r = (w >> 11) & 0x1F, g = (w >> 5) & 0x3F, b = w & 0x1F;
    return ((r << 3) | 7) | (((g << 2) | 3) << 8) | (((b << 3) | 7) << 16) | 0xFF000000u;
}

static uint32_t decodeasm_From565B(uint16_t w)
{
    uint32_t b = (w >> 11) & 0x1F, g = (w >> 5) & 0x3F, r = w & 0x1F;
    return (r << 3) | ((g << 2) << 8) | ((b << 3) << 16);
}

static void decodeasm_ToRGBA(unsigned char* d, const unsigned char* s, int n, uint32_t (*pfConvert)(uint16_t))
{
    for (; n > 0; --n) {
        decodeasm_Wr32(d, pfConvert(decodeasm_Rd16(s)));
        s += 2;
        d += 4;
    }
}

void decode_555toRGBA(void* pDst, const void* pSrc, int n)
{
    decodeasm_ToRGBA(pDst, pSrc, n, decodeasm_From555);
}

void decode_565toRGBA(void* pDst, const void* pSrc, int n)
{
    decodeasm_ToRGBA(pDst, pSrc, n, decodeasm_From565);
}

void decode_565BtoRGBA(void* pDst, const void* pSrc, int n)
{
    decodeasm_ToRGBA(pDst, pSrc, n, decodeasm_From565B);
}
