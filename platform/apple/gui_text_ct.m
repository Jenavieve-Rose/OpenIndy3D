/*
 * j3dgui text through Core Text (PORT-PLAN.md P6.5, decision 20): the apple build's half of gui_text.h.
 *
 * Faces are the fonts macOS ships. The dialog templates ask for "MS Sans Serif", Windows 98's bitmap dialog font,
 * which macOS has as its TrueType successor Microsoft Sans Serif; the game asks for "Times New Roman"
 * (JONES_STR_FONT), which macOS has under that name. Anything else falls back to Microsoft Sans Serif.
 *
 * Sizes follow GDI: a negative lfHeight is the em in pixels, a positive one the cell (ascent + descent). Glyphs are
 * rasterized once per face and character into 8-bit coverage; the GDI decides how to use it (gui_gdi.c draws it
 * without anti-aliasing unless J3D_GUI_SMOOTH_TEXT is set). Characters are Windows-1252.
 */
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>

#include <platform/common/j3dgui/gui_text.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct J3DGuiTextFace
{
    CTFontRef font;
    J3DGuiTextMetrics metrics;
    J3DGuiGlyph aGlyphs[256];
    uint8_t aLoaded[256];
    int bUnderline;
};

// Windows-1252 0x80-0x9F to Unicode; the rest of the code page is Latin-1.
static const uint16_t guiText_aCp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static UniChar guiText_ToUnicode(unsigned char ch)
{
    return ch >= 0x80 && ch < 0xA0 ? guiText_aCp1252[ch - 0x80] : (UniChar)ch;
}

static CTFontRef guiText_CreateFont(const char* pFaceName, CGFloat size, int weight, int bItalic)
{
    const char* pName = "Microsoft Sans Serif";
    CFStringRef name;
    CTFontRef font;
    CTFontSymbolicTraits traits = 0;

    if ( pFaceName && *pFaceName && strcasecmp(pFaceName, "MS Sans Serif") != 0 && strcasecmp(pFaceName, "MS Shell Dlg") != 0
         && strcasecmp(pFaceName, "System") != 0 )
    {
        pName = pFaceName;
    }
    name = CFStringCreateWithCString(NULL, pName, kCFStringEncodingWindowsLatin1);
    font = CTFontCreateWithName(name, size, NULL);
    CFRelease(name);
    if ( font )
    {
        // CTFontCreateWithName substitutes silently; keep only a real match, else the dialog font
        CFStringRef family = CTFontCopyFamilyName(font);
        char aFamily[128] = { 0 };
        if ( family )
        {
            CFStringGetCString(family, aFamily, sizeof(aFamily), kCFStringEncodingUTF8);
            CFRelease(family);
        }
        if ( strcasecmp(aFamily, pName) != 0 && strcmp(pName, "Microsoft Sans Serif") != 0 )
        {
            CFRelease(font);
            font = guiText_CreateFont("MS Sans Serif", size, weight, bItalic);
            return font;
        }
    }
    if ( !font )
    {
        return NULL;
    }
    if ( weight >= 600 )
    {
        traits |= kCTFontBoldTrait;
    }
    if ( bItalic )
    {
        traits |= kCTFontItalicTrait;
    }
    if ( traits )
    {
        CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(font, size, NULL, traits, traits);
        if ( styled )
        {
            CFRelease(font);
            font = styled;
        }
    }
    return font;
}

J3DGuiTextFace* J3DGuiText_Open(const char* pFaceName, int lfHeight, int weight, int bItalic, int bUnderline)
{
    J3DGuiTextFace* p = calloc(1, sizeof(*p));
    CGFloat em;
    CGFloat ascent;
    CGFloat descent;
    if ( !p )
    {
        return NULL;
    }
    // first at a nominal size, to turn a cell height into an em
    if ( lfHeight > 0 )
    {
        CTFontRef probe = guiText_CreateFont(pFaceName, 100.0, weight, bItalic);
        CGFloat cell = probe ? CTFontGetAscent(probe) + CTFontGetDescent(probe) : 120.0;
        em = lfHeight * 100.0 / cell;
        if ( probe )
        {
            CFRelease(probe);
        }
    }
    else
    {
        em = lfHeight < 0 ? -lfHeight : 11.0;
    }
    p->font = guiText_CreateFont(pFaceName, em, weight, bItalic);
    if ( !p->font )
    {
        free(p);
        return NULL;
    }
    p->bUnderline = bUnderline;
    ascent = CTFontGetAscent(p->font);
    descent = CTFontGetDescent(p->font);
    p->metrics.ascent = (int)ceil(ascent);
    p->metrics.descent = (int)ceil(descent);
    p->metrics.height = p->metrics.ascent + p->metrics.descent;
    p->metrics.internalLeading = p->metrics.height - (int)lround(em);
    p->metrics.externalLeading = (int)lround(CTFontGetLeading(p->font));
    {
        // GDI's average width: of the lower-case letters, weighted as Windows weighted them (the plain mean here)
        UniChar aChars[26];
        CGGlyph aGlyphs[26];
        CGSize aAdvances[26];
        CGFloat total = 0.0;
        CGFloat widest = 0.0;
        int i;
        for ( i = 0; i < 26; ++i )
        {
            aChars[i] = (UniChar)('a' + i);
        }
        CTFontGetGlyphsForCharacters(p->font, aChars, aGlyphs, 26);
        CTFontGetAdvancesForGlyphs(p->font, kCTFontOrientationHorizontal, aGlyphs, aAdvances, 26);
        for ( i = 0; i < 26; ++i )
        {
            total += aAdvances[i].width;
            widest = fmax(widest, aAdvances[i].width);
        }
        p->metrics.aveCharWidth = (int)lround(total / 26.0);
        p->metrics.maxCharWidth = (int)ceil(fmax(widest, CTFontGetBoundingBox(p->font).size.width));
    }
    return p;
}

void J3DGuiText_Close(J3DGuiTextFace* p)
{
    int i;
    if ( !p )
    {
        return;
    }
    for ( i = 0; i < 256; ++i )
    {
        free((void*)p->aGlyphs[i].pCoverage);
    }
    CFRelease(p->font);
    free(p);
}

void J3DGuiText_GetMetrics(J3DGuiTextFace* p, J3DGuiTextMetrics* pMetrics)
{
    static const J3DGuiTextMetrics none = { 13, 11, 2, 2, 0, 5, 11 };
    *pMetrics = p ? p->metrics : none;
}

const J3DGuiGlyph* J3DGuiText_GetGlyph(J3DGuiTextFace* p, unsigned char ch)
{
    J3DGuiGlyph* g;
    UniChar c;
    CGGlyph glyph = 0;
    CGSize advance;
    CGRect bounds;
    int left;
    int top;
    int width;
    int height;
    uint8_t* pCoverage;
    CGColorSpaceRef gray;
    CGContextRef ctx;
    CGPoint origin;

    if ( !p )
    {
        return NULL;
    }
    g = &p->aGlyphs[ch];
    if ( p->aLoaded[ch] )
    {
        return g;
    }
    p->aLoaded[ch] = 1;
    c = guiText_ToUnicode(ch);
    CTFontGetGlyphsForCharacters(p->font, &c, &glyph, 1);
    CTFontGetAdvancesForGlyphs(p->font, kCTFontOrientationHorizontal, &glyph, &advance, 1);
    g->advance = (int)lround(advance.width); // GDI's advances are whole pixels
    if ( ch < 32 || !glyph )
    {
        return g;
    }
    bounds = CTFontGetBoundingRectsForGlyphs(p->font, kCTFontOrientationHorizontal, &glyph, NULL, 1);
    if ( bounds.size.width <= 0.0 || bounds.size.height <= 0.0 )
    {
        return g;
    }
    left = (int)floor(bounds.origin.x);
    top = (int)ceil(bounds.origin.y + bounds.size.height);
    width = (int)ceil(bounds.origin.x + bounds.size.width) - left + 1;
    height = top - (int)floor(bounds.origin.y) + 1;
    pCoverage = calloc((size_t)width * (size_t)height, 1);
    if ( !pCoverage )
    {
        return g;
    }
    gray = CGColorSpaceCreateDeviceGray();
    ctx = CGBitmapContextCreate(pCoverage, (size_t)width, (size_t)height, 8, (size_t)width, gray, (CGBitmapInfo)kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    if ( ctx )
    {
        // white glyphs on black give the coverage; whole-pixel positions, no font smoothing
        CGContextSetGrayFillColor(ctx, 1.0, 1.0);
        CGContextSetShouldSmoothFonts(ctx, false);
        CGContextSetAllowsFontSubpixelPositioning(ctx, false);
        CGContextSetShouldSubpixelPositionFonts(ctx, false);
        // the baseline at height - top in Core Graphics' upward coordinates puts the glyph's top on the buffer's first row
        origin = CGPointMake(-left, height - top);
        CTFontDrawGlyphs(p->font, &glyph, &origin, 1, ctx);
        CGContextRelease(ctx);
    }
    g->left = left;
    g->top = top;
    g->width = width;
    g->height = height;
    g->pCoverage = pCoverage;
    return g;
}
