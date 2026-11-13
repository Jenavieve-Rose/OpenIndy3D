/*
 * Null INSANE platform modules and assembler entry points (PORT-PLAN.md Phase 0; replaced in Phase 5).
 *
 * The four OS-facing LEC modules (insane_timer, insane_dsnd, insane_stdfile, insane_stdio) register nothing, so the
 * services they would publish (TIMER, the DirectSound driver, FILE, FILIO) are absent and INSANE's own module startup
 * fails cleanly. The game then runs without movies.
 *
 * The six MASM objects (MemCopy.asm, MemFill.asm, BlitAsm.asm, DecodeAsm.asm, MixerAsm.asm, Bloc16Asm.asm) only run
 * while a movie decodes, which cannot happen without those services. Their entry points abort to make a missing
 * startup check obvious instead of decoding wrong pixels. Signatures follow the declarations in the C files that call
 * them; the Blocky16 tables are opaque here.
 */
#include <stddef.h>

#include "null_platform.h"

typedef struct LecShare LecShare;

/* ---- OS modules -------------------------------------------------------------------------------------------------- */
int timer_Register(LecShare* pShare, void* pParam)   { (void)pShare; (void)pParam; return 0; }
int dsnd_Register(LecShare* pShare, void* pParam)    { (void)pShare; (void)pParam; return 0; }
int stdfile_Register(LecShare* pShare, void* pParam) { (void)pShare; (void)pParam; return 0; }
int stdio_Register(LecShare* pShare, void* pParam)   { (void)pShare; (void)pParam; return 0; }

// SmushPlay configures the DirectSound driver (tag + varargs) before it starts the library; nothing to configure.
int dsnd_Configure(int tag, ...)
{
    (void)tag;
    return 0;
}

/* ---- MASM entry points ------------------------------------------------------------------------------------------- */
void blit_MemCopy(void* pDst, const void* pSrc, int size)              { (void)pDst; (void)pSrc; (void)size; null_Unreachable(__func__); }
void blit_MemCopyB(void* pDst, unsigned char value, int size)          { (void)pDst; (void)value; (void)size; null_Unreachable(__func__); }
void blit_Masm(void* pSrc, void* pDst, int a5, int a6, int a3, int a4, int width, int height, int srcPitch, int dstPitch)
{
    (void)pSrc; (void)pDst; (void)a5; (void)a6; (void)a3; (void)a4; (void)width; (void)height; (void)srcPitch; (void)dstPitch;
    null_Unreachable(__func__);
}

void decode_Lut8(void* pDst, const void* pSrc, int n, int dstSize, const void* pTable)  { (void)pDst; (void)pSrc; (void)n; (void)dstSize; (void)pTable; null_Unreachable(__func__); }
void decode_Lut16(void* pDst, const void* pSrc, int n, int dstSize, const void* pTable) { (void)pDst; (void)pSrc; (void)n; (void)dstSize; (void)pTable; null_Unreachable(__func__); }
void decode_RGBAto555(void* pDst, const void* pSrc, int n)  { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }
void decode_555toRGBA(void* pDst, const void* pSrc, int n)  { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }
void decode_RGBAto565(void* pDst, const void* pSrc, int n)  { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }
void decode_565toRGBA(void* pDst, const void* pSrc, int n)  { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }
void decode_RGBAto565B(void* pDst, const void* pSrc, int n) { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }
void decode_565BtoRGBA(void* pDst, const void* pSrc, int n) { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }

void mixer_MemCopy(void* pDst, const void* pSrc, int n) { (void)pDst; (void)pSrc; (void)n; null_Unreachable(__func__); }
void mixer_MemFill(void* pDst, int value, int n)        { (void)pDst; (void)value; (void)n; null_Unreachable(__func__); }

void dbloc16_Codec2(unsigned short* pDst, unsigned char* pSrc, ptrdiff_t offset1, ptrdiff_t offset2, int width, short* pMotion,
                    unsigned char* pParams, int height, int nextLine, void* pSmall, void* pBig, unsigned short* pPalette)
{
    (void)pDst; (void)pSrc; (void)offset1; (void)offset2; (void)width; (void)pMotion; (void)pParams; (void)height;
    (void)nextLine; (void)pSmall; (void)pBig; (void)pPalette;
    null_Unreachable(__func__);
}
