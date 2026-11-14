#ifndef J3D_PLATFORM_J3DGUI_TEXT_H
#define J3D_PLATFORM_J3DGUI_TEXT_H
/*
 * Font rasterization for j3dgui's GDI (PORT-PLAN.md P6.5). The apple build uses Core Text with the fonts macOS ships
 * (platform/apple/gui_text_ct.m): "MS Sans Serif", the dialog templates' font, is Microsoft Sans Serif there, and
 * "Times New Roman", the font the game asks for (JONES_STR_FONT), is itself. The null build
 * (platform/common/null/gui_text_null.c) has fixed metrics and draws no glyphs, so tests that lay out dialogs are the
 * same on every machine.
 *
 * Strings are the game's: Windows-1252, one byte per character.
 */
#include <stdint.h>

typedef struct J3DGuiTextFace J3DGuiTextFace;

typedef struct J3DGuiTextMetrics
{
    int height;             // tmHeight: ascent + descent
    int ascent;
    int descent;
    int internalLeading;    // tmHeight - the em size
    int externalLeading;
    int aveCharWidth;
    int maxCharWidth;
} J3DGuiTextMetrics;

// One glyph's coverage, 0 to 255, top-down. left and top place it relative to the pen at the baseline (top is the
// distance from the baseline up to its first row). The face owns the memory.
typedef struct J3DGuiGlyph
{
    int left;
    int top;
    int width;
    int height;
    int advance;
    const uint8_t* pCoverage;
} J3DGuiGlyph;

// lfHeight as LOGFONT has it: negative is the em size in pixels, positive the cell height, 0 a default (the em of
// 8-point text, 11 pixels). weight as FW_*.
J3DGuiTextFace* J3DGuiText_Open(const char* pFaceName, int lfHeight, int weight, int bItalic, int bUnderline);
void J3DGuiText_Close(J3DGuiTextFace* pFace);
void J3DGuiText_GetMetrics(J3DGuiTextFace* pFace, J3DGuiTextMetrics* pMetrics);
const J3DGuiGlyph* J3DGuiText_GetGlyph(J3DGuiTextFace* pFace, unsigned char ch);

#endif
