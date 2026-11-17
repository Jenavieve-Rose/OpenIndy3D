/*
 * Textures from and to image files (phase 9, higher-resolution textures from loose files); see render_metal.h.
 *
 * Images are decoded and encoded with ImageIO. Core Graphics draws only into premultiplied bitmaps, while the game's
 * textures carry straight alpha (blended source alpha over inverse source alpha), so a decoded image is unpremultiplied
 * before upload. An image is drawn into a bitmap in its own colour space, so its bytes are taken as
 * they are, as the game's own textures are: no colour management, as on a 1999 card. Written images are tagged sRGB.
 */
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>

#include <stdlib.h>
#include <string.h>

#include "display_metal_internal.h"
#include "render_metal.h"

#define TEXTURE_IMAGE_MAX_SIZE 16384 // the largest 2D texture of every Metal GPU family the port runs on

J3DRenderTexture* J3DRender_CreateTextureFromImage(const void* pData, size_t size, int bAlpha, int bMipmaps,
    uint32_t* pWidth, uint32_t* pHeight)
{
    if ( !pData || !size || !J3DMetal_Device() )
    {
        return NULL;
    }

    J3DRenderTexture* pTexture = NULL;
    @autoreleasepool
    {
        CFDataRef        data   = CFDataCreate(NULL, (const UInt8*)pData, (CFIndex)size);
        CGImageSourceRef source = data ? CGImageSourceCreateWithData(data, NULL) : NULL;
        CGImageRef       image  = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
        const size_t     width  = image ? CGImageGetWidth(image) : 0;
        const size_t     height = image ? CGImageGetHeight(image) : 0;

        uint8_t* pPixels = NULL;
        if ( width && height && width <= TEXTURE_IMAGE_MAX_SIZE && height <= TEXTURE_IMAGE_MAX_SIZE )
        {
            pPixels = (uint8_t*)calloc(width * height, 4);
        }

        CGColorSpaceRef space = image ? CGImageGetColorSpace(image) : NULL;
        CGColorSpaceRef owned = NULL;
        if ( !space || CGColorSpaceGetModel(space) != kCGColorSpaceModelRGB )
        {
            space = owned = CGColorSpaceCreateWithName(kCGColorSpaceSRGB); // greyscale and indexed images
        }

        // BGRA in memory: 32-bit little-endian words with alpha first. Decoded with alpha whatever the material, so an
        // image with alpha for a material without keeps its colours (an opaque texture ignores the alpha byte).
        const uint32_t  info    = kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst;
        CGContextRef    context = pPixels ? CGBitmapContextCreate(pPixels, width, height, 8, width * 4, space, info) : NULL;
        if ( context )
        {
            CGContextSetBlendMode(context, kCGBlendModeCopy);
            CGContextSetInterpolationQuality(context, kCGInterpolationNone);
            CGContextDrawImage(context, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height), image);
            CGContextRelease(context);

            for ( size_t i = 0; i < width * height; ++i )
            {
                uint8_t*       p = pPixels + i * 4;
                const unsigned a = p[3];
                if ( a && a < 255 )
                {
                    for ( int c = 0; c < 3; ++c )
                    {
                        const unsigned v = (p[c] * 255u + a / 2) / a;
                        p[c] = (uint8_t)(v > 255 ? 255 : v);
                    }
                }
            }

            size_t numLevels = 1;
            if ( bMipmaps )
            {
                for ( size_t largest = width > height ? width : height; largest > 1; largest >>= 1 )
                {
                    ++numLevels;
                }
            }

            pTexture = J3DRender_CreateTexture((uint32_t)width, (uint32_t)height, numLevels, bAlpha);
            if ( pTexture )
            {
                J3DRender_UploadTextureLevel(pTexture, 0, pPixels, width * 4);
                id<MTLTexture> texture = (__bridge id<MTLTexture>)(void*)pTexture;
                if ( texture.mipmapLevelCount > 1 )
                {
                    // The queue runs this before any later draw that samples the texture.
                    id<MTLCommandBuffer>      commands = [J3DMetal_Queue() commandBuffer];
                    id<MTLBlitCommandEncoder> blit     = [commands blitCommandEncoder];
                    [blit generateMipmapsForTexture:texture];
                    [blit endEncoding];
                    J3DMetal_Commit(commands);
                }
                *pWidth  = (uint32_t)width;
                *pHeight = (uint32_t)height;
            }
        }

        free(pPixels);
        if ( owned )
        {
            CGColorSpaceRelease(owned);
        }
        if ( image )
        {
            CGImageRelease(image);
        }
        if ( source )
        {
            CFRelease(source);
        }
        if ( data )
        {
            CFRelease(data);
        }
    }
    return pTexture;
}

int J3DRender_WriteTextureImage(J3DRenderTexture* pTexture, const char* pPath, int bAlpha)
{
    id<MTLTexture> texture = (__bridge id<MTLTexture>)(void*)pTexture;
    if ( !texture || !pPath )
    {
        return 1;
    }

    int result = 1;
    @autoreleasepool
    {
        const size_t width  = texture.width;
        const size_t height = texture.height;
        uint8_t*     pPixels = (uint8_t*)malloc(width * height * 4);
        if ( !pPixels )
        {
            return 1;
        }

        J3DMetal_SyncForCPU(texture);
        [texture getBytes:pPixels bytesPerRow:width * 4 fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];

        CGColorSpaceRef   space    = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CFDataRef         data     = CFDataCreateWithBytesNoCopy(NULL, pPixels, (CFIndex)(width * height * 4), kCFAllocatorNull);
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
        const uint32_t    info     = kCGBitmapByteOrder32Little
                                   | (uint32_t)(bAlpha ? kCGImageAlphaFirst : kCGImageAlphaNoneSkipFirst);
        CGImageRef image = CGImageCreate(width, height, 8, 32, width * 4, space, info, provider, NULL, false,
            kCGRenderingIntentDefault);

        NSURL*                  url  = [NSURL fileURLWithPath:[NSString stringWithUTF8String:pPath]];
        CGImageDestinationRef   dest = url ? CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL)
                                           : NULL;
        if ( dest && image )
        {
            CGImageDestinationAddImage(dest, image, NULL);
            result = CGImageDestinationFinalize(dest) ? 0 : 1;
        }

        if ( dest )
        {
            CFRelease(dest);
        }
        CGImageRelease(image);
        CGDataProviderRelease(provider);
        CFRelease(data);
        CGColorSpaceRelease(space);
        free(pPixels);
    }
    return result;
}
