/*
 * Host facts for the Apple platform build (PORT-PLAN.md P2.1); see host_apple.h.
 *
 * This is the first Objective-C file of the port. It touches Foundation, Metal and SDL3 so that a build with
 * J3D_PLATFORM=apple proves the Objective-C compiler, the frameworks and the SDL3 package are all wired in.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>

#include <SDL3/SDL.h>

#include <platform/apple/host_apple.h>

void J3DApple_LogHostInfo(void)
{
    @autoreleasepool
    {
        NSString* osVersion = [[NSProcessInfo processInfo] operatingSystemVersionString];

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        NSString* deviceName = device ? device.name : @"none";

        const int sdlVersion = SDL_GetVersion();
        printf("Host: macOS %s, Metal device %s, SDL %d.%d.%d\n",
               osVersion.UTF8String,
               deviceName.UTF8String,
               SDL_VERSIONNUM_MAJOR(sdlVersion),
               SDL_VERSIONNUM_MINOR(sdlVersion),
               SDL_VERSIONNUM_MICRO(sdlVersion));
        fflush(stdout);
    }
}
