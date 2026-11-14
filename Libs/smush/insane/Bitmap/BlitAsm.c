/*
 * BlitAsm.c: the C translation of BlitAsm.asm, INSANE's rectangle copy blit_Masm (retail 0x004F2220-0x004F22B7;
 * PORT-PLAN.md P5.4).
 *
 * blit_Masm(pSrc, pDst, srcX, srcY, dstX, dstY, width, height, srcPitch, dstPitch) copies a width x height rectangle
 * of bytes, row by row. Blit.c's blit_Wrapper calls it (for Mixer.c's clipped blit), always with the four offsets 0.
 *
 * What the assembler did, kept here:
 *   - The start addresses are pSrc + srcX + srcY * srcPitch and pDst + dstX + dstY * dstPitch, each product taken as
 *     a 16 x 16-bit multiply (mul bx / mul dx) whose 32-bit result is dx:ax.
 *   - The row loop is `dec ebp; jg`, so a height of 0 or less still copies one row.
 */
#include <stdint.h>
#include <string.h>

// eax:16 x 16 -> dx:ax as `mul r16` leaves it, then `shl edx,16; or eax,edx`: the upper half of eax is kept and or'ed in.
static uint32_t blitasm_Mul16(uint32_t a, uint32_t b)
{
    uint32_t product = (a & 0xFFFF) * (b & 0xFFFF);
    return (a & 0xFFFF0000u) | product;
}

void blit_Masm(void* pSrc, void* pDst, int srcX, int srcY, int dstX, int dstY, int width, int height, int srcPitch, int dstPitch)
{
    const unsigned char* s = (const unsigned char*)pSrc + srcX + blitasm_Mul16((uint32_t)srcY, (uint32_t)srcPitch);
    unsigned char* d = (unsigned char*)pDst + dstX + blitasm_Mul16((uint32_t)dstY, (uint32_t)(uint16_t)dstPitch);

    do {
        memcpy(d, s, (size_t)(unsigned)width); // rep movsd, then the width & 3 odd bytes (rep movsb)
        s += srcPitch;
        d += dstPitch;
    } while (--height > 0);
}
