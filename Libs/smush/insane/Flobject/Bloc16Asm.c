/*
 * Bloc16Asm.c: the C translation of Bloc16Asm.asm, codec 2 of the blocky16 frame-object decoder (retail
 * 0x004EDEF0-0x004EE6A0, data 0x0054DC70-0x0054DCA8; PORT-PLAN.md P5.7). D_bloc16.c calls dbloc16_Codec2.
 *
 * The frame is decoded in 8x8 blocks of 16-bit pixels, left to right and top to bottom. Each block starts with an
 * opcode byte in the stream; a block of 8 can split into four of 4, and a block of 4 into four of 2:
 *
 *   0x00-0xF4  copy the block from the previous frame, moved by motion vector `op` (the table D_bloc16.c builds)
 *   0xF5       the same with the vector, in pixels, as a signed 16-bit word after the opcode
 *   0xF6       copy the block from the delta frame, unmoved
 *   0xF7       8 and 4: a two-colour glyph: glyph index, then two palette indices (8-bit to 16-bit palette)
 *              2: four palette indices, one per pixel
 *   0xF8       8 and 4: a two-colour glyph: glyph index, then two 16-bit colours
 *              2: four 16-bit pixels
 *   0xF9-0xFC  fill with one of four colours from the chunk header (its `params`)
 *   0xFD       fill with a palette colour (one index byte)
 *   0xFE       fill with a 16-bit colour (one word)
 *   0xFF       8 and 4: split into four blocks of half the size; 2: four 16-bit pixels, as 0xF8
 *
 * A glyph's pixels are listed in the tables D_bloc16.c builds (D_bloc16.h): the first colour goes to its n1 listed
 * pixels, the second to its n2. The assembler wrote each list in a `dec; jg` loop, so an empty list still wrote one
 * pixel, at that list's first offset; that is kept, as what retail decodes.
 *
 * Order matters only where a copy's source and destination overlap, which cannot happen (the source is another of the
 * three frames); the copies still go in the assembler's order: rows 4 and 0, then 5 and 1, and so on, a dword at a time.
 *
 * The offsets to the previous and delta frames are byte distances between separate allocations, ptrdiff_t here (on
 * 64-bit they need not fit in 32 bits; retail kept them as 32-bit values in .data).
 *
 * dbloc16_Interpolate, a 2x upscaler through a pair-interpolation table that the assembler file also holds, has no
 * caller in the image and is not translated.
 */
#include <stdint.h>
#include <string.h>
#include "D_bloc16.h"

typedef struct Codec2
{
    ptrdiff_t prevOffset;           // previous frame - current frame, in bytes (dbloc16_prevOffset)
    ptrdiff_t deltaOffset;          // delta frame - current frame, in bytes (dbloc16_deltaOffset)
    ptrdiff_t pitch;                // one row in bytes: width * 2 (ebp in the block procedures)
    const short* pMotion;           // motion vectors as pixel offsets (dbloc16_motion)
    const unsigned char* pParams;   // the four fill colours of 0xF9-0xFC (dbloc16_colors + 2 * 0xF9)
    const Bl16Small* pGlyph4;
    const Bl16Big* pGlyph8;
    const unsigned short* pPalette;
} Codec2;

static uint16_t bloc16_Rd16(const unsigned char* p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t bloc16_Rd32(const unsigned char* p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void bloc16_Wr16(unsigned char* p, uint16_t v)
{
    memcpy(p, &v, sizeof(v));
}

static void bloc16_Wr32(unsigned char* p, uint32_t v)
{
    memcpy(p, &v, sizeof(v));
}

// Copies a size x size block (size 8 or 4) from d + offset, in the assembler's order: rows size/2 and 0, then
// size/2 + 1 and 1, ..., a dword at a time across each row.
static void bloc16_CopyBlock(const Codec2* c, unsigned char* d, ptrdiff_t offset, int size)
{
    const int half = size / 2;
    const int rowBytes = size * 2;
    int r;
    int k;

    for (r = 0; r < half; ++r) {
        unsigned char* pLow = d + (r + half) * c->pitch;
        unsigned char* pHigh = d + r * c->pitch;

        for (k = 0; k < rowBytes; k += 4) {
            bloc16_Wr32(pLow + k, bloc16_Rd32(pLow + k + offset));
            bloc16_Wr32(pHigh + k, bloc16_Rd32(pHigh + k + offset));
        }
    }
}

static void bloc16_FillBlock(const Codec2* c, unsigned char* d, uint16_t color, int size)
{
    int r;
    int x;

    for (r = 0; r < size; ++r) {
        for (x = 0; x < size; ++x) {
            bloc16_Wr16(d + r * c->pitch + x * 2, color);
        }
    }
}

// One list of a glyph: `do { write; } while (--n > 0)`, so n == 0 writes the first offset once. Offsets are read as
// unsigned words (movzx), in pixels.
static void bloc16_GlyphList(unsigned char* d, const short* pOffsets, unsigned n, uint16_t color)
{
    int left = (int)n;
    const short* p = pOffsets;

    do {
        bloc16_Wr16(d + (size_t)(uint16_t)*p * 2, color);
        p++;
    } while (--left > 0);
}

// The 0xF7 and 0xF8 operands: the glyph index and the two colours. Returns the index and advances *ps.
static unsigned bloc16_GlyphColors(const Codec2* c, const unsigned char** ps, uint16_t* pColor1, uint16_t* pColor2)
{
    const unsigned char* s = *ps;
    unsigned index = s[1];

    if (s[0] == 0xF8) {
        uint32_t colors = bloc16_Rd32(s + 2);
        *pColor1 = (uint16_t)colors;
        *pColor2 = (uint16_t)(colors >> 16);
        *ps = s + 6;
    }
    else { // 0xF7
        *pColor1 = c->pPalette[s[2]];
        *pColor2 = c->pPalette[s[3]];
        *ps = s + 4;
    }
    return index;
}

// The fill colour of 0xF9-0xFE. Advances *ps.
static uint16_t bloc16_FillColor(const Codec2* c, const unsigned char** ps)
{
    const unsigned char* s = *ps;

    switch (s[0]) {
        case 0xFE:
            *ps = s + 3;
            return bloc16_Rd16(s + 1);
        case 0xFD:
            *ps = s + 2;
            return c->pPalette[s[1]];
        default: // 0xF9-0xFC
            *ps = s + 1;
            return bloc16_Rd16(c->pParams + 2 * (s[0] - 0xF9));
    }
}

// Opcodes 0x00-0xF5: the vector's offset to the source in the previous frame, in bytes. Advances *ps.
static ptrdiff_t bloc16_MotionOffset(const Codec2* c, const unsigned char** ps)
{
    const unsigned char* s = *ps;
    ptrdiff_t vector;

    if (s[0] == 0xF5) {
        vector = (int16_t)bloc16_Rd16(s + 1);
        *ps = s + 3;
    }
    else {
        vector = c->pMotion[s[0]];
        *ps = s + 1;
    }
    return vector * 2 + c->prevOffset;
}

static void bloc16_Block2(const Codec2* c, const unsigned char** ps, unsigned char* d)
{
    const unsigned char* s = *ps;
    const unsigned char op = s[0];
    ptrdiff_t offset;
    uint16_t color;

    if (op <= 0xF5) {
        offset = bloc16_MotionOffset(c, ps);
        bloc16_Wr32(d, bloc16_Rd32(d + offset));
        bloc16_Wr32(d + c->pitch, bloc16_Rd32(d + c->pitch + offset));
        return;
    }
    switch (op) {
        case 0xFF:
        case 0xF8:
            bloc16_Wr32(d, bloc16_Rd32(s + 1));
            bloc16_Wr32(d + c->pitch, bloc16_Rd32(s + 5));
            *ps = s + 9;
            return;
        case 0xF7:
            bloc16_Wr16(d, c->pPalette[s[1]]);
            bloc16_Wr16(d + 2, c->pPalette[s[2]]);
            bloc16_Wr16(d + c->pitch, c->pPalette[s[3]]);
            bloc16_Wr16(d + c->pitch + 2, c->pPalette[s[4]]);
            *ps = s + 5;
            return;
        case 0xF6:
            *ps = s + 1;
            bloc16_Wr32(d, bloc16_Rd32(d + c->deltaOffset));
            bloc16_Wr32(d + c->pitch, bloc16_Rd32(d + c->pitch + c->deltaOffset));
            return;
        default: // 0xF9-0xFE
            color = bloc16_FillColor(c, ps);
            bloc16_FillBlock(c, d, color, 2);
            return;
    }
}

static void bloc16_Block4(const Codec2* c, const unsigned char** ps, unsigned char* d)
{
    const unsigned char op = (*ps)[0];
    const Bl16Small* pGlyph;
    uint16_t color1;
    uint16_t color2;

    if (op <= 0xF5) {
        bloc16_CopyBlock(c, d, bloc16_MotionOffset(c, ps), 4);
        return;
    }
    switch (op) {
        case 0xFF:
            *ps += 1;
            bloc16_Block2(c, ps, d);
            bloc16_Block2(c, ps, d + 4);
            bloc16_Block2(c, ps, d + 2 * c->pitch);
            bloc16_Block2(c, ps, d + 2 * c->pitch + 4);
            return;
        case 0xF8:
        case 0xF7:
            pGlyph = &c->pGlyph4[bloc16_GlyphColors(c, ps, &color1, &color2)];
            bloc16_GlyphList(d, pGlyph->off1, pGlyph->n1, color1);
            bloc16_GlyphList(d, pGlyph->off2, pGlyph->n2, color2);
            return;
        case 0xF6:
            *ps += 1;
            bloc16_CopyBlock(c, d, c->deltaOffset, 4);
            return;
        default: // 0xF9-0xFE
            bloc16_FillBlock(c, d, bloc16_FillColor(c, ps), 4);
            return;
    }
}

static void bloc16_Block8(const Codec2* c, const unsigned char** ps, unsigned char* d)
{
    const unsigned char op = (*ps)[0];
    const Bl16Big* pGlyph;
    uint16_t color1;
    uint16_t color2;

    if (op <= 0xF5) {
        bloc16_CopyBlock(c, d, bloc16_MotionOffset(c, ps), 8);
        return;
    }
    switch (op) {
        case 0xFF:
            *ps += 1;
            bloc16_Block4(c, ps, d);
            bloc16_Block4(c, ps, d + 8);
            bloc16_Block4(c, ps, d + 4 * c->pitch);
            bloc16_Block4(c, ps, d + 4 * c->pitch + 8);
            return;
        case 0xF8:
        case 0xF7:
            pGlyph = &c->pGlyph8[bloc16_GlyphColors(c, ps, &color1, &color2)];
            bloc16_GlyphList(d, pGlyph->off1, pGlyph->n1, color1);
            bloc16_GlyphList(d, pGlyph->off2, pGlyph->n2, color2);
            return;
        case 0xF6:
            *ps += 1;
            bloc16_CopyBlock(c, d, c->deltaOffset, 8);
            return;
        default: // 0xF9-0xFE
            bloc16_FillBlock(c, d, bloc16_FillColor(c, ps), 8);
            return;
    }
}

void dbloc16_Codec2(unsigned short* pDst, unsigned char* pSrc, ptrdiff_t offset1, ptrdiff_t offset2, int width, short* pMotion, unsigned char* pParams,
                    int height, int nextLine, Bl16Small* pSmall, Bl16Big* pBig, unsigned short* pPalette)
{
    Codec2 c;
    const unsigned char* s = pSrc;
    unsigned char* d = (unsigned char*)pDst;
    // The block counts are bytes, counted down with `dec byte; jne`: a count of 0 runs 256 times.
    uint8_t blocksY = (uint8_t)((unsigned)(height + 7) >> 3);
    const uint8_t blocksX = (uint8_t)((unsigned)(width + 7) >> 3);
    ptrdiff_t rowSkip;

    (void)nextLine; // retail reads the argument slot but never uses it
    c.prevOffset = offset1 * 2;
    c.deltaOffset = offset2 * 2;
    c.pitch = (ptrdiff_t)width * 2;
    c.pMotion = pMotion;
    c.pParams = pParams;
    c.pGlyph4 = pSmall;
    c.pGlyph8 = pBig;
    c.pPalette = pPalette;
    rowSkip = c.pitch * 8 - c.pitch; // from the end of a row of blocks to the start of the next

    do {
        uint8_t countX = blocksX;
        do {
            bloc16_Block8(&c, &s, d);
            d += 16;
        } while (--countX != 0);
        d += rowSkip;
    } while (--blocksY != 0);
}
