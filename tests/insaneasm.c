/*
 * insaneasm: the C translations of INSANE's assembler helpers against models of the assembler itself (PORT-PLAN.md
 * P5.4 to P5.6). Needs no game data. The Blocky16 block decoder (P5.7) is checked on the movies by tests/moviecheck.c.
 *
 * DecodeAsm.c states each pixel conversion as a formula. Here each converter's per-pixel instruction sequence is written
 * out from DecodeAsm.asm as register operations (x86 sub-registers, shifts and rotates, with the registers' upper bits
 * holding random garbage, as the assembler left them), and compared with the C for every 16-bit input and every RGB
 * triple. The other helpers (copies, fills, the rectangle blit, the table lookups) are checked against plain
 * definitions of what their instructions do.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void decode_Lut8(void* pDst, const void* pSrc, int n, int dstSize, const void* pTable);
void decode_Lut16(void* pDst, const void* pSrc, int n, int dstSize, const void* pTable);
void decode_RGBAto555(void* pDst, const void* pSrc, int n);
void decode_555toRGBA(void* pDst, const void* pSrc, int n);
void decode_RGBAto565(void* pDst, const void* pSrc, int n);
void decode_565toRGBA(void* pDst, const void* pSrc, int n);
void decode_RGBAto565B(void* pDst, const void* pSrc, int n);
void decode_565BtoRGBA(void* pDst, const void* pSrc, int n);
void blit_MemCopy(void* pDst, const void* pSrc, int size);
void blit_MemCopyB(void* pDst, unsigned char value, int size);
void blit_Masm(void* pSrc, void* pDst, int srcX, int srcY, int dstX, int dstY, int width, int height, int srcPitch, int dstPitch);
void mixer_MemCopy(void* pDst, const void* pSrc, int n);
void mixer_MemFill(void* pDst, int value, int n);

static int insaneasm_failures = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            if (insaneasm_failures++ < 20) {                                                                           \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
                printf(__VA_ARGS__);                                                                                   \
                printf("\n");                                                                                          \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

/* ---- x86 register helpers ----------------------------------------------------------------------------------------- */

#define AL(r) ((r) & 0xFFu)
#define AH(r) (((r) >> 8) & 0xFFu)
#define AX(r) ((r) & 0xFFFFu)
#define SET_L(r, v) ((r) = ((r) & ~0xFFu) | ((uint32_t)(v) & 0xFFu))
#define SET_H(r, v) ((r) = ((r) & ~0xFF00u) | (((uint32_t)(v) & 0xFFu) << 8))
#define SET_X(r, v) ((r) = ((r) & ~0xFFFFu) | ((uint32_t)(v) & 0xFFFFu))

static uint32_t ror32(uint32_t v, unsigned n)
{
    return (v >> n) | (v << (32 - n));
}

static uint32_t rol32(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32 - n));
}

static uint32_t insaneasm_seed = 0x12345678u;

static uint32_t insaneasm_Random(void)
{
    insaneasm_seed = insaneasm_seed * 1664525u + 1013904223u;
    return insaneasm_seed;
}

/* ---- DecodeAsm.asm, per pixel, as the instructions run --------------------------------------------------------------- */

// decode_RGBAto555, the odd-pixel block (the pair loop runs the same steps on ecx/edx)
static uint16_t model_RGBAto555(uint32_t pixel, uint32_t ebx)
{
    uint32_t eax = pixel;
    eax &= 0xF8F8F8u;             // and eax,0F8F8F8h
    SET_L(ebx, AH(eax));          // mov bl,ah
    SET_H(ebx, AL(eax));          // mov bh,al
    SET_H(ebx, AH(ebx) >> 3);     // shr bh,3
    ebx <<= 2;                    // shl ebx,2
    eax >>= 11;                   // shr eax,0Bh
    SET_L(ebx, AL(ebx) | AH(eax)); // or bl,ah
    return (uint16_t)AX(ebx);     // mov [edi],bx
}

static uint16_t model_RGBAto565(uint32_t pixel, uint32_t ebx)
{
    uint32_t eax = pixel;
    eax &= 0xF8FCF8u;
    SET_L(ebx, AH(eax));
    SET_H(ebx, AL(eax));
    SET_H(ebx, AH(ebx) >> 3);
    ebx <<= 3;
    eax >>= 11;
    SET_L(ebx, AL(ebx) | AH(eax));
    return (uint16_t)AX(ebx);
}

static uint16_t model_RGBAto565B(uint32_t pixel)
{
    uint32_t eax = pixel;
    eax >>= 3;                    // shr eax,3
    SET_X(eax, AX(eax) << 1);     // shl ax,1
    SET_L(eax, AL(eax) << 2);     // shl al,2
    SET_X(eax, AX(eax) << 2);     // shl ax,2
    eax >>= 5;                    // shr eax,5
    return (uint16_t)AX(eax);     // mov [edi],ax
}

static uint32_t model_555toRGBA(uint16_t w, uint32_t ebx)
{
    uint32_t eax = w;             // movzx eax,word ptr [esi]
    SET_L(ebx, AH(eax));          // mov bl,ah
    eax <<= 3;                    // shl eax,3
    ebx = ror32(ebx, 7);          // ror ebx,7
    SET_H(ebx, AL(eax));          // mov bh,al
    eax <<= 3;                    // shl eax,3
    SET_L(ebx, AH(eax));          // mov bl,ah
    ebx = rol32(ebx, 8);          // rol ebx,8
    return ebx | 0xFF070707u;     // or ebx,0FF070707h
}

static uint32_t model_565toRGBA(uint16_t w, uint32_t ebx)
{
    uint32_t eax = w;
    SET_L(ebx, AH(eax));
    eax <<= 3;
    ebx = ror32(ebx, 8);
    SET_H(ebx, AL(eax));
    eax <<= 2;
    SET_L(ebx, AH(eax));
    ebx = rol32(ebx, 8);
    return ebx | 0xFF070307u;
}

static uint32_t model_565BtoRGBA(uint16_t w)
{
    uint32_t eax = w;             // movzx eax,word ptr [esi]
    eax <<= 5;                    // shl eax,5
    SET_X(eax, AX(eax) >> 3);     // shr ax,3
    eax <<= 1;                    // shl eax,1
    SET_L(eax, AL(eax) >> 2);     // shr al,2
    eax <<= 2;                    // shl eax,2
    return eax;                   // mov [edi],eax
}

static void insaneasm_CheckDecode(void)
{
    uint32_t w;
    uint32_t rgb;
    unsigned char aOut[8];
    unsigned char aIn[8];

    for (w = 0; w < 0x10000; ++w) {
        uint32_t got;

        memcpy(aIn, &w, 2);
        decode_555toRGBA(aOut, aIn, 1);
        memcpy(&got, aOut, 4);
        CHECK(got == model_555toRGBA((uint16_t)w, insaneasm_Random()), "555toRGBA %04X: %08X", w, got);
        decode_565toRGBA(aOut, aIn, 1);
        memcpy(&got, aOut, 4);
        CHECK(got == model_565toRGBA((uint16_t)w, insaneasm_Random()), "565toRGBA %04X: %08X", w, got);
        decode_565BtoRGBA(aOut, aIn, 1);
        memcpy(&got, aOut, 4);
        CHECK(got == model_565BtoRGBA((uint16_t)w), "565BtoRGBA %04X: %08X", w, got);
    }

    for (rgb = 0; rgb < 0x1000000; ++rgb) {
        uint32_t pixel = rgb | (insaneasm_Random() & 0xFF000000u); // the alpha byte is ignored
        uint16_t got;

        memcpy(aIn, &pixel, 4);
        decode_RGBAto555(aOut, aIn, 1);
        memcpy(&got, aOut, 2);
        CHECK(got == model_RGBAto555(pixel, insaneasm_Random()), "RGBAto555 %08X: %04X", pixel, got);
        decode_RGBAto565(aOut, aIn, 1);
        memcpy(&got, aOut, 2);
        CHECK(got == model_RGBAto565(pixel, insaneasm_Random()), "RGBAto565 %08X: %04X", pixel, got);
        decode_RGBAto565B(aOut, aIn, 1);
        memcpy(&got, aOut, 2);
        CHECK(got == model_RGBAto565B(pixel), "RGBAto565B %08X: %04X", pixel, got);
    }

    // Several pixels at once, an odd count: the C converts them in order, as the odd-first, pairs-after assembler did.
    {
        uint16_t aWords[7];
        uint32_t aRGBA[7];
        int i;

        for (i = 0; i < 7; ++i) {
            aWords[i] = (uint16_t)insaneasm_Random();
        }
        decode_565toRGBA(aRGBA, aWords, 7);
        for (i = 0; i < 7; ++i) {
            CHECK(aRGBA[i] == model_565toRGBA(aWords[i], 0), "565toRGBA run, pixel %d", i);
        }
    }
}

static void insaneasm_CheckLut(void)
{
    unsigned char aTable[0x40000];
    unsigned char aSrc[64];
    unsigned char aDst[256 + 8];
    int size;
    int i;

    for (i = 0; i < (int)sizeof(aTable); ++i) {
        aTable[i] = (unsigned char)insaneasm_Random();
    }
    for (i = 0; i < (int)sizeof(aSrc); ++i) {
        aSrc[i] = (unsigned char)insaneasm_Random();
    }
    for (size = 1; size <= 4; ++size) {
        memset(aDst, 0xEE, sizeof(aDst));
        decode_Lut8(aDst, aSrc, 13, size, aTable);
        for (i = 0; i < 13; ++i) {
            CHECK(memcmp(aDst + i * size, aTable + aSrc[i] * size, (size_t)size) == 0, "Lut8 size %d pixel %d", size, i);
        }
        CHECK(aDst[13 * size] == 0xEE, "Lut8 size %d wrote past its last pixel", size);

        memset(aDst, 0xEE, sizeof(aDst));
        decode_Lut16(aDst, aSrc, 13, size, aTable);
        for (i = 0; i < 13; ++i) {
            unsigned index = aSrc[2 * i] | (aSrc[2 * i + 1] << 8);
            CHECK(memcmp(aDst + i * size, aTable + index * size, (size_t)size) == 0, "Lut16 size %d pixel %d", size, i);
        }
        CHECK(aDst[13 * size] == 0xEE, "Lut16 size %d wrote past its last pixel", size);
    }
}

static void insaneasm_CheckCopies(void)
{
    unsigned char aSrc[300];
    unsigned char aDst[300];
    int n;
    int i;

    for (i = 0; i < (int)sizeof(aSrc); ++i) {
        aSrc[i] = (unsigned char)insaneasm_Random();
    }
    for (n = 0; n < 40; ++n) {
        memset(aDst, 0xEE, sizeof(aDst));
        blit_MemCopy(aDst + 1, aSrc + 3, n);
        CHECK(memcmp(aDst + 1, aSrc + 3, (size_t)n) == 0 && aDst[0] == 0xEE && aDst[1 + n] == 0xEE, "blit_MemCopy %d", n);
        memset(aDst, 0xEE, sizeof(aDst));
        mixer_MemCopy(aDst + 2, aSrc + 1, n);
        CHECK(memcmp(aDst + 2, aSrc + 1, (size_t)n) == 0 && aDst[1] == 0xEE && aDst[2 + n] == 0xEE, "mixer_MemCopy %d", n);
        memset(aDst, 0xEE, sizeof(aDst));
        blit_MemCopyB(aDst + 1, 0x5A, n);
        for (i = 0; i < n; ++i) {
            CHECK(aDst[1 + i] == 0x5A, "blit_MemCopyB %d", n);
        }
        CHECK(aDst[0] == 0xEE && aDst[1 + n] == 0xEE, "blit_MemCopyB %d bounds", n);
        memset(aDst, 0xEE, sizeof(aDst));
        mixer_MemFill(aDst + 1, 0x1234, n); // the low byte of the value
        for (i = 0; i < n; ++i) {
            CHECK(aDst[1 + i] == 0x34, "mixer_MemFill %d", n);
        }
        CHECK(aDst[0] == 0xEE && aDst[1 + n] == 0xEE, "mixer_MemFill %d bounds", n);
    }

    // a 20000-byte fill and more: retail's rep stosd runs
    {
        static unsigned char aBig[50003];
        memset(aBig, 0, sizeof(aBig));
        blit_MemCopyB(aBig, 7, 50001);
        CHECK(aBig[0] == 7 && aBig[50000] == 7 && aBig[50001] == 0, "blit_MemCopyB 50001");
    }
}

static void insaneasm_CheckBlit(void)
{
    unsigned char aSrc[40 * 30];
    unsigned char aDst[50 * 30];
    int x;
    int y;

    for (x = 0; x < (int)sizeof(aSrc); ++x) {
        aSrc[x] = (unsigned char)insaneasm_Random();
    }

    // widths with and without odd bytes, equal and different pitches (the assembler's three paths)
    int widths[] = { 8, 11, 40 };
    int dstPitches[] = { 50, 40 };
    for (int wi = 0; wi < 3; ++wi) {
        for (int pi = 0; pi < 2; ++pi) {
            int width = widths[wi];
            int dstPitch = dstPitches[pi];
            if (width > dstPitch) {
                continue;
            }
            memset(aDst, 0xEE, sizeof(aDst));
            blit_Masm(aSrc, aDst, 2, 3, 1, 4, width - 2, 9, 40, dstPitch);
            for (y = 0; y < 30; ++y) {
                for (x = 0; x < dstPitch; ++x) {
                    int inside = y >= 4 && y < 13 && x >= 1 && x < 1 + width - 2;
                    unsigned char want = inside ? aSrc[(y - 4 + 3) * 40 + (x - 1 + 2)] : 0xEE;
                    if (y * dstPitch + x < (int)sizeof(aDst)) {
                        CHECK(aDst[y * dstPitch + x] == want, "blit_Masm width %d pitch %d at %d,%d", width, dstPitch, x, y);
                    }
                }
            }
        }
    }

    // a height of 0 copies one row (`dec ebp; jg`)
    memset(aDst, 0xEE, sizeof(aDst));
    blit_Masm(aSrc, aDst, 0, 0, 0, 0, 5, 0, 40, 50);
    CHECK(memcmp(aDst, aSrc, 5) == 0 && aDst[50] == 0xEE, "blit_Masm height 0");
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    insaneasm_CheckDecode();
    insaneasm_CheckLut();
    insaneasm_CheckCopies();
    insaneasm_CheckBlit();
    if (insaneasm_failures) {
        printf("insaneasm: %d check(s) failed\n", insaneasm_failures);
        return 1;
    }
    printf("insaneasm: the six pixel converters match the assembler for every 16-bit and RGB input; copies, fills, "
           "lookups and the rectangle blit as specified\n");
    return 0;
}
