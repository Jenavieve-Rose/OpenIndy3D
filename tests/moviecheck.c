/*
 * moviecheck: every movie decoded headlessly, every frame checked (PORT-PLAN.md P5.8).
 *
 *   moviecheck <data root>     the folder holding RESOURCE/
 *
 * The game's movies are the five intro movies in RESOURCE/ (jonesopn.snm and its _3, _4, _5 and _8 sizes); the GOBs hold
 * none (P5.12). For each, INSANE is started with SmushPlay's module list, except that the sound output (insane_dsnd) is
 * replaced by a driver that mixes nothing on its own: after every decoded frame the test pulls one frame's worth of
 * audio (22,050 / 15 = 1470 sample frames) through the sound engine and the mixer itself. The frame stream is then run
 * as fast as it decodes, without the player's real-time pacing, so a run takes seconds and is the same every time.
 *
 * Checked, for every movie:
 *   - the file holds the frames its header announces, every frame that holds a picture is decoded and shown (a few
 *     frames carry only sound), and the stream ends cleanly;
 *   - every codec-2 frame (the C translation of Bloc16Asm.asm, P5.7) matches a second decoder, a C port of ScummVM's
 *     Blocky16 (engines/grim/movie/codecs/blocky16.cpp, GPLv3+), run on copies of the same three frame buffers;
 *   - the audio is not silent, and the frames' and the audio's checksums match the values below, taken from this build
 *     after the frames were looked at (MOVIECHECK_DUMP, below). The Windows build's checksums under DREAMM, which the
 *     plan names as the reference, could not be captured; ScummVM's decoder is the independent check.
 *
 * MOVIECHECK_DUMP=<dir> writes every 50th frame of each movie there as a 24-bit BMP, and its mixed audio as a WAV, to
 * look at and listen to.
 */
#include <smush/insane/lec.h>
#include <smush/insane/animplay.h>
#include <smush/insane/Lec/LecShare.h>
#include <smush/insane/Bitmap/Bitmap.h>
#include <smush/insane/Memory/Memory.h>
#include <smush/insane/File/Cdstream.h>
#include <smush/insane/File/Framestream.h>
#include <smush/insane/Sound/Dsnd.h>
#include <smush/insane/Sound/Mixer.h>
#include <smush/insane/Flobject/D_bloc16.h>

#include "harness.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int readauto_Register(LecShare* pShare, void* pParam);
extern int animplay_Register(LecShare* pShare, void* pParam);
extern int animframe_Register(LecShare* pShare, void* pParam);
extern int streamer_Register(LecShare* pShare, void* pParam);
extern int sound_Register(LecShare* pShare, void* pParam);
extern int dwave_Register(LecShare* pShare, void* pParam);
extern int dbloc16_Register(LecShare* pShare, void* pParam);
extern int stdfile_Register(LecShare* pShare, void* pParam);
extern int zfilio_Register(LecShare* pShare, void* pParam);
extern int stdio_Register(LecShare* pShare, void* pParam);
extern int bitmap_Register(LecShare* pShare, void* pParam);
extern int timer_Register(LecShare* pShare, void* pParam);
extern int memory_Register(LecShare* pShare, void* pParam);
extern int zfilio_Configure(int tag, ...);
extern int dbloc16_Configure(int tag, ...);
extern char* lec_GetLog(int bFlush);

#define MOVIE_AUDIOFRAMES 1470 // 22,050 Hz / 15 frames per second

typedef struct MovieInfo
{
    const char* pName;
    uint64_t videoSum; // FNV-1a over every shown frame's pixels
    uint64_t audioSum; // FNV-1a over the mixed audio
} MovieInfo;

static const MovieInfo moviecheck_aMovies[] = {
    // jonesopn_3, _4, _5 and _8 carry the same sound track; jonesopn.snm, one frame shorter, its own mix of it
    { "jonesopn.snm",   0x5666c3136f3bfe15ull, 0xd1816bd6b31a0142ull },
    { "jonesopn_3.snm", 0x4b0127f06c924da6ull, 0x377925d7ba062989ull },
    { "jonesopn_4.snm", 0x298b3f1a20f3a5e0ull, 0x377925d7ba062989ull },
    { "jonesopn_5.snm", 0xa78029d0e80e5ba3ull, 0x377925d7ba062989ull },
    { "jonesopn_8.snm", 0xb2d9216991fb71c3ull, 0x377925d7ba062989ull },
};

static int moviecheck_failures = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            moviecheck_failures++;                                                                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static uint64_t moviecheck_Fnv(uint64_t h, const void* pData, size_t size)
{
    const unsigned char* p = pData;
    while (size--) {
        h ^= *p++;
        h *= 0x100000001B3ull;
    }
    return h;
}

/* ---- The test's sound driver: mixes when asked -------------------------------------------------------------------- */

static LecShare* testsnd_pShare;
static MemoryDebugInterface* testsnd_pMemory;
static void (*testsnd_pfnFill)(int, SoundFeed);

static int testsnd_Register(LecShare* pShare, void* pParam);
static void testsnd_Unregister(void)
{
    testsnd_pShare->Unpublish("SNDDRIVER=moviecheck");
    testsnd_pShare->Unpublish("LECMODULE=moviecheck_snd");
}

static int testsnd_Startup(void)
{
    testsnd_pMemory = testsnd_pShare->Subscribe("MEMORY.mixertable", NULL);
    return testsnd_pMemory ? 0 : -1;
}

static void testsnd_Shutdown(void)
{
    testsnd_pShare->Unsubscribe(testsnd_pMemory);
}

static int testsnd_Start(int rate, int tracks, void (*pfnFill)(int, SoundFeed))
{
    (void)rate;
    (void)tracks;
    testsnd_pfnFill = pfnFill;
    return mixer_Init(testsnd_pMemory, 4, 0x400, 0, 1, 1);
}

static void testsnd_Stop(void)
{
    testsnd_pfnFill = NULL;
    mixer_Done(testsnd_pMemory);
}

static void testsnd_SetVolume(int volume)
{
    (void)volume;
}

static int testsnd_GetVolume(void)
{
    return 127;
}

static int testsnd_SetRate(int rate)
{
    (void)rate;
    return 0;
}

static int testsnd_GetRate(void)
{
    return 22050;
}

static LecModule testsnd_module = { testsnd_Register, testsnd_Unregister, testsnd_Startup, testsnd_Shutdown };
static SndDriver testsnd_interface = { testsnd_Start, testsnd_Stop, testsnd_SetVolume, testsnd_GetVolume, testsnd_SetRate, testsnd_GetRate };

static int testsnd_Register(LecShare* pShare, void* pParam)
{
    (void)pParam;
    testsnd_pShare = pShare;
    pShare->Publish("LECMODULE=moviecheck_snd", &testsnd_module);
    pShare->Publish("SNDDRIVER=moviecheck", &testsnd_interface);
    return 0;
}

/* ---- ScummVM's Blocky16 codec 2, the reference (engines/grim/movie/codecs/blocky16.cpp) ------------------------------
 *
 * ScummVM - Graphic Adventure Engine. ScummVM is the legal property of its developers, whose names are too numerous to
 * list here. Please refer to the COPYRIGHT file distributed with this source distribution. This program is free
 * software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at your option) any later version. This program is
 * distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * Blocky16::level1, level2, level3 and decode2, from C++ to C: the members became the struct below, the alignment
 * macros became byte copies. The tables are the ones the game's decoder builds (D_bloc16.c), passed in as the
 * assembler takes them, so only the block decoding is compared.
 */
typedef struct RefBlocky16
{
    const unsigned char* pSrc;      // _d_src
    const unsigned char* pParams;   // _paramPtr + 2 * 0xF9
    const unsigned char* pPalette;  // _param6_7Ptr
    const short* pTable;            // _table
    const unsigned char* pSmall;    // _tableSmall
    const unsigned char* pBig;      // _tableBig
    long offset1;                   // _offset1, bytes
    long offset2;                   // _offset2, bytes
    long pitch;                     // _d_pitch
} RefBlocky16;

static uint16_t ref_Rd16(const unsigned char* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t ref_Rd32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void ref_Wr16(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void ref_Wr32(unsigned char* p, uint32_t v)
{
    ref_Wr16(p, v);
    ref_Wr16(p + 2, v >> 16);
}

static void ref_Copy4(unsigned char* d, const unsigned char* s)
{
    memcpy(d, s, 4);
}

static void ref_Level3(RefBlocky16* b, unsigned char* d)
{
    long tmp2;
    uint32_t t;
    unsigned char code = *b->pSrc++;
    int i;

    if (code <= 0xF5) {
        if (code == 0xF5) {
            tmp2 = (int16_t)ref_Rd16(b->pSrc) * 2;
            b->pSrc += 2;
        } else {
            tmp2 = b->pTable[code] * 2;
        }
        tmp2 += b->offset1;
        for (i = 0; i < 2; i++) {
            ref_Copy4(d, d + tmp2);
            d += b->pitch;
        }
    } else if (code == 0xFF || code == 0xF8) {
        ref_Wr16(d + 0, ref_Rd16(b->pSrc + 0));
        ref_Wr16(d + 2, ref_Rd16(b->pSrc + 2));
        d += b->pitch;
        ref_Wr16(d + 0, ref_Rd16(b->pSrc + 4));
        ref_Wr16(d + 2, ref_Rd16(b->pSrc + 6));
        b->pSrc += 8;
    } else if (code == 0xFD) {
        t = *b->pSrc++;
        t = ref_Rd16(b->pPalette + t * 2);
        t = (t << 16) | t;
        for (i = 0; i < 2; i++) {
            ref_Wr32(d, t);
            d += b->pitch;
        }
    } else if (code == 0xFE) {
        t = ref_Rd16(b->pSrc);
        b->pSrc += 2;
        t = (t << 16) | t;
        for (i = 0; i < 2; i++) {
            ref_Wr32(d, t);
            d += b->pitch;
        }
    } else if (code == 0xF6) {
        for (i = 0; i < 2; i++) {
            ref_Copy4(d, d + b->offset2);
            d += b->pitch;
        }
    } else if (code == 0xF7) {
        uint32_t v = ref_Rd32(b->pSrc);
        b->pSrc += 4;
        ref_Wr16(d + 0, ref_Rd16(b->pPalette + (unsigned char)v * 2));
        ref_Wr16(d + 2, ref_Rd16(b->pPalette + (unsigned char)(v >> 8) * 2));
        v >>= 16;
        d += b->pitch;
        ref_Wr16(d + 0, ref_Rd16(b->pPalette + (unsigned char)v * 2));
        ref_Wr16(d + 2, ref_Rd16(b->pPalette + (unsigned char)(v >> 8) * 2));
    } else if (code >= 0xF9 && code <= 0xFC) {
        t = ref_Rd16(b->pParams + (code - 0xF9) * 2);
        t = (t << 16) | t;
        for (i = 0; i < 2; i++) {
            ref_Wr32(d, t);
            d += b->pitch;
        }
    }
}

// The glyph of level1 and level2: "the disasm writes at least one entry, even when the list is empty" (ScummVM's note).
static void ref_Glyph(RefBlocky16* b, unsigned char* d, unsigned char code, const unsigned char* pTables, int entrySize, int countAt, int secondAt)
{
    unsigned char index = *b->pSrc++;
    uint32_t val;
    const unsigned char* p;
    const unsigned char* q;
    int l;

    if (code == 0xF8) {
        val = ref_Rd32(b->pSrc);
        b->pSrc += 4;
    } else {
        uint32_t tmp2 = ref_Rd16(b->pSrc);
        val = (uint32_t)ref_Rd16(b->pPalette + (unsigned char)(tmp2 >> 8) * 2) << 16;
        val |= ref_Rd16(b->pPalette + (unsigned char)tmp2 * 2);
        b->pSrc += 2;
    }
    p = pTables + index * entrySize;
    l = p[countAt];
    q = p;
    do {
        ref_Wr16(d + ref_Rd16(q) * 2, val);
        q += 2;
    } while (l-- > 1);
    l = p[countAt + 1];
    val >>= 16;
    q = p + secondAt;
    do {
        ref_Wr16(d + ref_Rd16(q) * 2, val);
        q += 2;
    } while (l-- > 1);
}

static uint32_t ref_FillValue(RefBlocky16* b, unsigned char code)
{
    uint32_t t = 0;
    if (code == 0xFD) {
        t = *b->pSrc++;
        t = ref_Rd16(b->pPalette + t * 2);
    } else if (code == 0xFE) {
        t = ref_Rd16(b->pSrc);
        b->pSrc += 2;
    } else if (code >= 0xF9 && code <= 0xFC) {
        t = ref_Rd16(b->pParams + (code - 0xF9) * 2);
    }
    return (t << 16) | t;
}

static void ref_Level2(RefBlocky16* b, unsigned char* d)
{
    long tmp2;
    unsigned char code = *b->pSrc++;
    int i;

    if (code <= 0xF5) {
        if (code == 0xF5) {
            tmp2 = (int16_t)ref_Rd16(b->pSrc) * 2;
            b->pSrc += 2;
        } else {
            tmp2 = b->pTable[code] * 2;
        }
        tmp2 += b->offset1;
        for (i = 0; i < 4; i++) {
            ref_Copy4(d + 0, d + tmp2 + 0);
            ref_Copy4(d + 4, d + tmp2 + 4);
            d += b->pitch;
        }
    } else if (code == 0xFF) {
        ref_Level3(b, d);
        d += 4;
        ref_Level3(b, d);
        d += b->pitch * 2 - 4;
        ref_Level3(b, d);
        d += 4;
        ref_Level3(b, d);
    } else if (code == 0xF6) {
        for (i = 0; i < 4; i++) {
            ref_Copy4(d + 0, d + b->offset2 + 0);
            ref_Copy4(d + 4, d + b->offset2 + 4);
            d += b->pitch;
        }
    } else if (code == 0xF7 || code == 0xF8) {
        ref_Glyph(b, d, code, b->pSmall, 128, 96, 32);
    } else if (code >= 0xF9) {
        uint32_t t = ref_FillValue(b, code);
        for (i = 0; i < 4; i++) {
            ref_Wr32(d + 0, t);
            ref_Wr32(d + 4, t);
            d += b->pitch;
        }
    }
}

static void ref_Level1(RefBlocky16* b, unsigned char* d)
{
    long tmp2;
    unsigned char code = *b->pSrc++;
    int i;
    int k;

    if (code <= 0xF5) {
        if (code == 0xF5) {
            tmp2 = (int16_t)ref_Rd16(b->pSrc) * 2;
            b->pSrc += 2;
        } else {
            tmp2 = b->pTable[code] * 2;
        }
        tmp2 += b->offset1;
        for (i = 0; i < 8; i++) {
            for (k = 0; k < 16; k += 4) {
                ref_Copy4(d + k, d + tmp2 + k);
            }
            d += b->pitch;
        }
    } else if (code == 0xFF) {
        ref_Level2(b, d);
        d += 8;
        ref_Level2(b, d);
        d += b->pitch * 4 - 8;
        ref_Level2(b, d);
        d += 8;
        ref_Level2(b, d);
    } else if (code == 0xF6) {
        for (i = 0; i < 8; i++) {
            for (k = 0; k < 16; k += 4) {
                ref_Copy4(d + k, d + b->offset2 + k);
            }
            d += b->pitch;
        }
    } else if (code == 0xF7 || code == 0xF8) {
        ref_Glyph(b, d, code, b->pBig, 388, 384, 128);
    } else if (code >= 0xF9) {
        uint32_t t = ref_FillValue(b, code);
        for (i = 0; i < 8; i++) {
            for (k = 0; k < 16; k += 4) {
                ref_Wr32(d + k, t);
            }
            d += b->pitch;
        }
    }
}

static void ref_Decode2(RefBlocky16* b, unsigned char* dst, int width, int height)
{
    int bh = (height + 7) / 8;
    long nextLine = (long)width * 2 * 7;

    b->pitch = (long)width * 2;
    do {
        int bw = (width + 7) / 8;
        do {
            ref_Level1(b, dst);
            dst += 16;
        } while (--bw);
        dst += nextLine;
    } while (--bh);
}

/* ---- Codec 2, checked ---------------------------------------------------------------------------------------------- */

static int moviecheck_codec2Frames;
static int moviecheck_codec2Mismatches;

static void moviecheck_Codec2(unsigned short* pDst, unsigned char* pSrc, ptrdiff_t offset1, ptrdiff_t offset2, int width, short* pMotion,
                              unsigned char* pParams, int height, int nextLine, Bl16Small* pSmall, Bl16Big* pBig, unsigned short* pPalette)
{
    // The three frames, each with the guard rows D_bloc16.c gives it above and below (vectors near the edges read
    // them), copied into one arena: current, previous (offset1) and delta (offset2).
    const size_t pixels = (size_t)width * (size_t)((height + 7) & ~7);
    const size_t guard = (size_t)width * (size_t)dbloc16_GuardRows(width);
    const size_t region = pixels + 2 * guard;
    unsigned short* pArena = malloc(region * 3 * sizeof(unsigned short));
    RefBlocky16 ref;

    memcpy(pArena, pDst - guard, region * 2);
    memcpy(pArena + region, pDst + offset1 - guard, region * 2);
    memcpy(pArena + 2 * region, pDst + offset2 - guard, region * 2);

    dbloc16_Codec2(pDst, pSrc, offset1, offset2, width, pMotion, pParams, height, nextLine, pSmall, pBig, pPalette);

    ref.pSrc = pSrc;
    ref.pParams = pParams;
    ref.pPalette = (const unsigned char*)pPalette;
    ref.pTable = pMotion;
    ref.pSmall = (const unsigned char*)pSmall;
    ref.pBig = (const unsigned char*)pBig;
    ref.offset1 = (long)region * 2;
    ref.offset2 = (long)region * 4;
    ref_Decode2(&ref, (unsigned char*)(pArena + guard), width, height);
    unsigned short* pRefFrame = pArena + guard;

    moviecheck_codec2Frames++;
    if (memcmp(pRefFrame, pDst, pixels * 2) != 0) {
        if (moviecheck_codec2Mismatches++ < 5) {
            size_t i = 0;
            while (pRefFrame[i] == pDst[i]) {
                i++;
            }
            printf("FAIL codec 2 frame %d differs from the reference at pixel %zu (%d,%zu): %04X, reference %04X\n",
                   moviecheck_codec2Frames, i, (int)(i % (size_t)width), i / (size_t)width, pDst[i], pRefFrame[i]);
        }
    }
    free(pArena);
}

/* ---- Frames ---------------------------------------------------------------------------------------------------------- */

static uint64_t moviecheck_videoSum;
static int moviecheck_shown;
static const char* moviecheck_pDumpDir;
static const char* moviecheck_pMovieName;

static void moviecheck_WriteBmp(const LecBitmapHeader* pBitmap, int frame)
{
    char aPath[1024];
    FILE* pFile;
    int width = pBitmap->width;
    int height = pBitmap->height;
    int rowSize = (width * 3 + 3) & ~3;
    unsigned char aHeader[54] = { 'B', 'M' };
    unsigned char* pRow = calloc(1, (size_t)rowSize);
    int x;
    int y;

    snprintf(aPath, sizeof(aPath), "%s/%s-%04d.bmp", moviecheck_pDumpDir, moviecheck_pMovieName, frame);
    pFile = fopen(aPath, "wb");
    if (!pFile) {
        free(pRow);
        return;
    }
    uint32_t fileSize = 54u + (uint32_t)rowSize * (uint32_t)height;
    memcpy(aHeader + 2, &fileSize, 4);
    aHeader[10] = 54;
    aHeader[14] = 40;
    memcpy(aHeader + 18, &width, 4);
    memcpy(aHeader + 22, &height, 4);
    aHeader[26] = 1;
    aHeader[28] = 24;
    fwrite(aHeader, 1, sizeof(aHeader), pFile);
    for (y = height - 1; y >= 0; --y) {
        const unsigned char* pIn = (const unsigned char*)pBitmap->pPixels + (size_t)y * (size_t)pBitmap->pitch;
        for (x = 0; x < width; ++x) {
            uint16_t p = (uint16_t)(pIn[2 * x] | (pIn[2 * x + 1] << 8)); // RGB565
            pRow[3 * x + 0] = (unsigned char)((p & 0x1F) << 3);
            pRow[3 * x + 1] = (unsigned char)(((p >> 5) & 0x3F) << 2);
            pRow[3 * x + 2] = (unsigned char)((p >> 11) << 3);
        }
        fwrite(pRow, 1, (size_t)rowSize, pFile);
    }
    fclose(pFile);
    free(pRow);
}

// blocky16's blit callback (SmushPlay's smushplay_Blit in the game): one call per shown frame, with the decoded bitmap.
static int moviecheck_Blit(LecBitmapHeader* pDst, MixerRect* pClip, int x, int y, LecBitmapHeader* pSrc, void* pPalette, void* pTable)
{
    int row;

    (void)pDst;
    (void)pClip;
    (void)x;
    (void)y;
    (void)pPalette;
    (void)pTable;
    for (row = 0; row < pSrc->height; ++row) {
        moviecheck_videoSum = moviecheck_Fnv(moviecheck_videoSum, (const unsigned char*)pSrc->pPixels + (size_t)row * (size_t)pSrc->pitch,
                                             (size_t)pSrc->width * (size_t)pSrc->pixelSize);
    }
    if (moviecheck_pDumpDir && moviecheck_shown % 50 == 0) {
        moviecheck_WriteBmp(pSrc, moviecheck_shown);
    }
    moviecheck_shown++;
    return 0;
}

/* ---- The run ----------------------------------------------------------------------------------------------------------- */

static void* moviecheck_pMemory;
static void* moviecheck_pTimer;
static AnimPlayTable* moviecheck_pAnimPlay;
static BitmapInterface* moviecheck_pBitmap;
static StreamerInterface* moviecheck_pStreamer;

static LecBatch moviecheck_services[] = {
    { &moviecheck_pMemory, "MEMORY.app", 0 },
    { &moviecheck_pTimer, "TIMER.app", 0 },
    { (void**)&moviecheck_pBitmap, "BITMAP.app", 0 },
    { (void**)&moviecheck_pStreamer, "STREAMER.app", 0 },
    { (void**)&moviecheck_pAnimPlay, "ANIMPLAY.app", 0 },
    { 0, 0, 0 }
};

// SmushPlay's list, with the test's sound driver in place of insane_dsnd
static LecModuleEntry moviecheck_modules[] = {
    { memory_Register, 0 },
    { timer_Register, 0 },
    { bitmap_Register, 0 },
    { stdio_Register, 0 },
    { zfilio_Register, "FILIO=insane_stdio" },
    { stdfile_Register, 0 },
    { dbloc16_Register, 0 },
    { dwave_Register, 0 },
    { testsnd_Register, 0 },
    { sound_Register, 0 },
    { streamer_Register, 0 },
    { animframe_Register, 0 },
    { animplay_Register, 0 },
    { readauto_Register, 0 },
    { 0, 0 }
};

static uint32_t moviecheck_Be32(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Reads the movie's chunks: the frame count in the stream header (SHDR, the 16-bit count after its version), the FRME
// chunks, and those FRME chunks that hold a picture (a Bl16 chunk; a few hold only sound). Returns 0 if it is not a
// SANM movie.
static int moviecheck_ScanFile(const char* pPath, int* pHeaderFrames, int* pFrames, int* pPictures)
{
    FILE* pFile = fopen(pPath, "rb");
    unsigned char* pData;
    long size;
    long pos;

    *pHeaderFrames = *pFrames = *pPictures = 0;
    if (!pFile) {
        return 0;
    }
    fseek(pFile, 0, SEEK_END);
    size = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);
    pData = malloc((size_t)size);
    if (!pData || fread(pData, 1, (size_t)size, pFile) != (size_t)size || size < 20 || memcmp(pData, "SANM", 4) != 0
        || memcmp(pData + 8, "SHDR", 4) != 0) {
        free(pData);
        fclose(pFile);
        return 0;
    }
    fclose(pFile);

    *pHeaderFrames = pData[18] | (pData[19] << 8);
    for (pos = 8; pos + 8 <= size;) {
        uint32_t chunk = moviecheck_Be32(pData + pos + 4);
        if (memcmp(pData + pos, "FRME", 4) == 0) {
            long sub = pos + 8;
            long end = pos + 8 + (long)chunk;
            int bPicture = 0;

            while (sub + 8 <= end && sub + 8 <= size) {
                uint32_t subSize = moviecheck_Be32(pData + sub + 4);
                bPicture |= memcmp(pData + sub, "Bl16", 4) == 0;
                sub += 8 + (long)subSize + (long)(subSize & 1);
            }
            (*pFrames)++;
            *pPictures += bPicture;
        }
        pos += 8 + (long)chunk + (long)(chunk & 1);
    }
    free(pData);
    return 1;
}

static void moviecheck_PrintLog(void)
{
    const char* pLine;
    while ((pLine = lec_GetLog(0)) != NULL) {
        printf("  INSANE: %s\n", pLine);
    }
}

static int moviecheck_Movie(const char* pDataRoot, const MovieInfo* pMovie)
{
    char aPath[1024];
    LecShare* pShare;
    LecBitmapHeader* pBitmap = NULL;
    AnimPlay* pPlay = NULL;
    unsigned char aPalette[0x400];
    static int16_t aAudio[MOVIE_AUDIOFRAMES * 2];
    FILE* pWav = NULL;
    uint32_t audioBytes = 0;
    double audioEnergy = 0.0;
    uint64_t audioSum = 0xCBF29CE484222325ull;
    int headerFrames;
    int frames;
    int pictures;
    int ticks = 0;
    int r;

    snprintf(aPath, sizeof(aPath), "%s/RESOURCE/%s", pDataRoot, pMovie->pName);
    if (!moviecheck_ScanFile(aPath, &headerFrames, &frames, &pictures)) {
        printf("%s: not found or not a SANM movie, skipped\n", pMovie->pName);
        return 0;
    }

    moviecheck_pMovieName = pMovie->pName;
    moviecheck_videoSum = 0xCBF29CE484222325ull;
    moviecheck_shown = 0;
    moviecheck_codec2Frames = 0;
    moviecheck_codec2Mismatches = 0;

    pShare = LecShare_Init(lec_Log, moviecheck_modules, "READANIM=insane_auto FILIO=insane_zfilio", "");
    CHECK(pShare != NULL, "LecShare_Init");
    zfilio_Configure(1, ".snm", 0);
    zfilio_Configure(1, ".znm", 0);
    if (LecShare_Startup() != 0 || ((int (*)(LecBatch*, const char*))pShare->pfn10)(moviecheck_services, 0) != 0) {
        CHECK(0, "%s: INSANE did not start", pMovie->pName);
        moviecheck_PrintLog();
        return 1;
    }

    CHECK(moviecheck_pBitmap->Create(&pBitmap, 640, 480, 3) == 0, "frame bitmap");
    dbloc16_Configure(1, moviecheck_Blit, 0);
    dbloc16_pfnCodec2 = moviecheck_Codec2;

    if (moviecheck_pDumpDir) {
        char aWav[1024];
        static const unsigned char aWavHeader[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                                      0x22, 0x56, 0, 0, 0x88, 0x58, 0x01, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
        snprintf(aWav, sizeof(aWav), "%s/%s.wav", moviecheck_pDumpDir, pMovie->pName);
        pWav = fopen(aWav, "wb");
        if (pWav) {
            fwrite(aWavHeader, 1, sizeof(aWavHeader), pWav);
        }
    }

    r = moviecheck_pAnimPlay->Create(&pPlay, aPath, 15, 1000000, 1000000, 0xD); // as SmushPlay_PlayMovie
    CHECK(r == 0, "%s: AnimPlay Create", pMovie->pName);
    if (r == 0) {
        // the pacer's loop without the pacing: fill the stream, run the frame stream, mix one frame of audio
        do {
            int v1 = 0;
            int v2 = 0;

            moviecheck_pStreamer->Fill(pPlay->pStream, 1 << 20);
            r = framestream_Tick(pPlay, pBitmap, 0, 0, 0, aPalette, &v1, &v2, (int)pPlay->flags, -1, 0);
            if (r >= 0 && r <= 4 && testsnd_pfnFill) {
                mixer_Begin(MOVIE_AUDIOFRAMES);
                testsnd_pfnFill(MOVIE_AUDIOFRAMES, mixer_Feed);
                mixer_Mix(aAudio, MOVIE_AUDIOFRAMES);
                audioSum = moviecheck_Fnv(audioSum, aAudio, sizeof(aAudio));
                for (int k = 0; k < MOVIE_AUDIOFRAMES * 2; ++k) {
                    audioEnergy += (double)aAudio[k] * aAudio[k];
                }
                if (pWav) {
                    audioBytes += (uint32_t)fwrite(aAudio, 1, sizeof(aAudio), pWav);
                }
            }
        } while (r >= 0 && r != 4 && ++ticks < 1000000);
        CHECK(r == 4, "%s: the stream ended with %d after %d steps", pMovie->pName, r, ticks);
        moviecheck_pAnimPlay->Destroy(&pPlay);
    }
    if (pWav) {
        uint32_t riffSize = 36 + audioBytes;
        fseek(pWav, 4, SEEK_SET);
        fwrite(&riffSize, 4, 1, pWav);
        fseek(pWav, 40, SEEK_SET);
        fwrite(&audioBytes, 4, 1, pWav);
        fclose(pWav);
    }

    moviecheck_pBitmap->Destroy(&pBitmap);
    dbloc16_pfnCodec2 = dbloc16_Codec2;
    LecShare_Shutdown();
    LecShare_UnregisterAll();
    ((int (*)(LecBatch*))pShare->pfn14)(moviecheck_services);

    printf("%-15s %3d frames, %3d pictures shown, codec 2 %3d checked against the reference, video %016llx audio %016llx\n",
           pMovie->pName, frames, moviecheck_shown, moviecheck_codec2Frames, (unsigned long long)moviecheck_videoSum,
           (unsigned long long)audioSum);
    {
        double rms = ticks ? sqrt(audioEnergy / ((double)moviecheck_shown * MOVIE_AUDIOFRAMES * 2)) : 0.0;
        printf("%-15s audio RMS %.0f (of 32768)\n", "", rms);
        CHECK(rms > 100.0, "%s: the audio is silent (RMS %.1f)", pMovie->pName, rms);
    }
    CHECK(frames == headerFrames, "%s: %d frames in the file, the header says %d", pMovie->pName, frames, headerFrames);
    CHECK(moviecheck_shown == pictures, "%s: %d pictures shown, the file holds %d", pMovie->pName, moviecheck_shown, pictures);
    CHECK(moviecheck_codec2Frames > 0, "%s: no codec 2 frame", pMovie->pName);
    CHECK(moviecheck_codec2Mismatches == 0, "%s: %d codec 2 frames differ from the reference", pMovie->pName, moviecheck_codec2Mismatches);
    if (pMovie->videoSum) {
        CHECK(moviecheck_videoSum == pMovie->videoSum, "%s: video checksum %016llx, expected %016llx", pMovie->pName,
              (unsigned long long)moviecheck_videoSum, (unsigned long long)pMovie->videoSum);
        CHECK(audioSum == pMovie->audioSum, "%s: audio checksum %016llx, expected %016llx", pMovie->pName, (unsigned long long)audioSum,
              (unsigned long long)pMovie->audioSum);
    }
    return 1;
}

int main(int argc, char** argv)
{
    size_t i;
    int played = 0;

    harness_InstallHandlers();
    if (argc < 2) {
        fprintf(stderr, "usage: moviecheck <data root>\n");
        return 2;
    }
    moviecheck_pDumpDir = getenv("MOVIECHECK_DUMP");

    for (i = 0; i < sizeof(moviecheck_aMovies) / sizeof(moviecheck_aMovies[0]); ++i) {
        played += moviecheck_Movie(argv[1], &moviecheck_aMovies[i]);
    }
    CHECK(played > 0, "no movie found under %s/RESOURCE", argv[1]);
    if (moviecheck_failures) {
        printf("moviecheck: %d check(s) failed\n", moviecheck_failures);
        return 1;
    }
    printf("moviecheck: %d movie(s) decoded, every frame shown and every codec 2 frame equal to the reference\n", played);
    return 0;
}
