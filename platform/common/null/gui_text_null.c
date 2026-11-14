/*
 * j3dgui text with no rasterizer (PORT-PLAN.md P6.5): the null build's half of gui_text.h. Every face has fixed
 * metrics from its size and draws no glyphs, so dialogs lay out the same everywhere and the null build's tests do not
 * depend on the fonts a machine has. The apple build uses platform/apple/gui_text_ct.m instead.
 */
#include <platform/common/j3dgui/gui_text.h>

#include <stdlib.h>

struct J3DGuiTextFace
{
    J3DGuiTextMetrics metrics;
    J3DGuiGlyph glyph;
};

J3DGuiTextFace* J3DGuiText_Open(const char* pFaceName, int lfHeight, int weight, int bItalic, int bUnderline)
{
    J3DGuiTextFace* p = calloc(1, sizeof(*p));
    int em;
    (void)pFaceName;
    (void)weight;
    (void)bItalic;
    (void)bUnderline;
    if ( !p )
    {
        return NULL;
    }
    em = lfHeight < 0 ? -lfHeight : lfHeight > 0 ? lfHeight * 85 / 100 : 11;
    if ( em < 1 )
    {
        em = 1;
    }
    // the proportions of MS Sans Serif 8 (11-pixel em: ascent 11, descent 2, average width 5)
    p->metrics.ascent = em;
    p->metrics.descent = (em + 4) / 5;
    p->metrics.height = p->metrics.ascent + p->metrics.descent;
    p->metrics.internalLeading = p->metrics.height - em;
    p->metrics.aveCharWidth = (em * 5 + 5) / 11;
    p->metrics.maxCharWidth = em;
    p->glyph.advance = p->metrics.aveCharWidth;
    return p;
}

void J3DGuiText_Close(J3DGuiTextFace* pFace)
{
    free(pFace);
}

void J3DGuiText_GetMetrics(J3DGuiTextFace* pFace, J3DGuiTextMetrics* pMetrics)
{
    static const J3DGuiTextMetrics none = { 13, 11, 2, 2, 0, 5, 11 };
    *pMetrics = pFace ? pFace->metrics : none;
}

const J3DGuiGlyph* J3DGuiText_GetGlyph(J3DGuiTextFace* pFace, unsigned char ch)
{
    (void)ch;
    return pFace ? &pFace->glyph : NULL;
}
