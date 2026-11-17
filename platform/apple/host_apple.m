/*
 * Host facts for the Apple platform build (PORT-PLAN.md P2.1); see host_apple.h.
 *
 * This is the first Objective-C file of the port. It touches Foundation, Metal and SDL3 so that a build with
 * J3D_PLATFORM=apple proves the Objective-C compiler, the frameworks and the SDL3 package are all wired in.
 */
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include <platform/apple/host_apple.h>
#include <platform/common/host_product.h>

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

int J3DApple_IsOptionHeld(void)
{
    return ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
}

int J3DApple_AskForData(const char* pProblem, char* pRoot, size_t size)
{
    if ( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
    {
        fprintf(stderr, "J3DApple_AskForData: SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return 0;
    }

    int bChosen = 0;
    @autoreleasepool
    {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @J3D_PRODUCT_NAME " needs your copy of Indiana Jones and the Infernal Machine";
        NSString* info = @"Choose the game's folder: the one that holds the Resource folder, from the Steam or GOG "
                         @"release, DREAMM, or a CD installation with CD1.GOB and CD2.GOB copied beside JONES3D.GOB. "
                         @"Nothing in it is changed.";
        if ( pProblem && *pProblem )
        {
            info = [NSString stringWithFormat:@"%@\n\n%@", [NSString stringWithUTF8String:pProblem], info];
        }
        alert.informativeText = info;
        [alert addButtonWithTitle:@"Choose Folder…"];
        [alert addButtonWithTitle:@"Quit"];
        // For tests, as the menus (MenuHost.swift): J3D_MENU_AUTOCANCEL=<seconds> answers the alert with Quit
        const char* pAutoCancel = getenv("J3D_MENU_AUTOCANCEL");
        if ( pAutoCancel && atof(pAutoCancel) > 0 )
        {
            NSTimer* timer = [NSTimer timerWithTimeInterval:atof(pAutoCancel) repeats:NO block:^(NSTimer* t) {
                (void)t;
                [NSApp abortModal];
            }];
            [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
        }

        // Cancelling the open panel goes back to the alert, whose Quit is the way out
        while ( !bChosen && [alert runModal] == NSAlertFirstButtonReturn )
        {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseDirectories = YES;
            panel.canChooseFiles = NO;
            panel.allowsMultipleSelection = NO;
            panel.message = @"Choose the folder of Indiana Jones and the Infernal Machine, the one that holds Resource";
            panel.prompt = @"Choose";
            if ( [panel runModal] == NSModalResponseOK && panel.URL.fileSystemRepresentation )
            {
                snprintf(pRoot, size, "%s", panel.URL.fileSystemRepresentation);
                bChosen = 1;
            }
        }
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return bChosen;
}

void J3DApple_ShowLastCrash(const char* pLogPath)
{
    if ( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
    {
        return;
    }

    @autoreleasepool
    {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];

        NSString* path = [NSString stringWithUTF8String:pLogPath];
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @J3D_PRODUCT_NAME " quit unexpectedly the last time it ran";
        alert.informativeText = [NSString stringWithFormat:@"What the game was doing is in %@. If you report the "
                                 @"problem, please include that file, and the crash report macOS made.", path];
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Show in Finder"];

        const char* pAutoCancel = getenv("J3D_MENU_AUTOCANCEL"); // for tests, as J3DApple_AskForData
        if ( pAutoCancel && atof(pAutoCancel) > 0 )
        {
            NSTimer* timer = [NSTimer timerWithTimeInterval:atof(pAutoCancel) repeats:NO block:^(NSTimer* t) {
                (void)t;
                [NSApp abortModal];
            }];
            [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
        }

        if ( [alert runModal] == NSAlertSecondButtonReturn )
        {
            [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:path] ]];
        }
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
