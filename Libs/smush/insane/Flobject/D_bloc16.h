#ifndef SMUSH_INSANE_D_BLOC16_H
#define SMUSH_INSANE_D_BLOC16_H

/*
 * Port: the blocky16 glyph tables D_bloc16.c builds and codec 2's block decoder (Bloc16Asm.c, the C translation of
 * Bloc16Asm.asm) reads, moved here from D_bloc16.c so both files see one definition (PORT-PLAN.md P5.7).
 */
#include <stddef.h>

/* the glyph tables: per (from-edge, to-edge) pair, the pixel offsets of the two halves of a 4x4 or 8x8 glyph */
typedef struct Bl16Small
{
    short off1[16];                 /* 0x00 */
    short off2[16];                 /* 0x20 */
    unsigned char idx1[16];         /* 0x40 */
    unsigned char idx2[16];         /* 0x50 */
    unsigned char n1;               /* 0x60 */
    unsigned char n2;               /* 0x61 */
    unsigned char pad[30];
} Bl16Small;
_Static_assert(sizeof(Bl16Small) == 0x80, "glyph4 entry"); // Port: Bloc16Asm.c steps through these tables by entry (P5.1)

typedef struct Bl16Big
{
    short off1[64];                 /* 0x000 */
    short off2[64];                 /* 0x080 */
    unsigned char idx1[64];         /* 0x100 */
    unsigned char idx2[64];         /* 0x140 */
    unsigned char n1;               /* 0x180 */
    unsigned char n2;               /* 0x181 */
    unsigned char pad[2];
} Bl16Big;
_Static_assert(sizeof(Bl16Big) == 0x184, "glyph8 entry");

/* codec 2's block decoder (retail MASM at 0x004EDF70; Bloc16Asm.c). nextLine is not read, as in retail. */
void dbloc16_Codec2(unsigned short* pDst, unsigned char* pSrc, ptrdiff_t offset1, ptrdiff_t offset2, int width, /* Port: ptrdiff_t offsets */ short* pMotion, unsigned char* pParams,
                    int height, int nextLine, Bl16Small* pSmall, Bl16Big* pBig, unsigned short* pPalette);

// Port: what D_bloc16.c calls codec 2 through (dbloc16_Codec2 unless a test replaces it).
typedef void (*Bl16Codec2Func)(unsigned short* pDst, unsigned char* pSrc, ptrdiff_t offset1, ptrdiff_t offset2, int width, short* pMotion,
                               unsigned char* pParams, int height, int nextLine, Bl16Small* pSmall, Bl16Big* pBig, unsigned short* pPalette);
extern Bl16Codec2Func dbloc16_pfnCodec2;

// Port: the black rows above and below each of the codec's frames (D_bloc16.c).
int dbloc16_GuardRows(int width);

#endif
