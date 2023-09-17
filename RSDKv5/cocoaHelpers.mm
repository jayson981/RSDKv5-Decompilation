#ifdef __APPLE__

#include <SDL2/SDL.h>

#import <Foundation/Foundation.h>

#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

#include "cocoaHelpers.hpp"

const char* getResourcesPath(void)
{
    static char pathStorage[1024] = {0};
    
    if (!strlen(pathStorage)) {
        @autoreleasepool {
            NSFileManager *fileManager = [[NSFileManager alloc] init];
            NSArray *urls = [fileManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask];
            strncpy(pathStorage, [urls[0] fileSystemRepresentation], sizeof(pathStorage));
        }
    }
    
    return pathStorage;
}

const char* getAppResourcesPath(void)
{
    static char pathStorage[1024] = {0};
    
    if (!strlen(pathStorage)) {
        @autoreleasepool {
            NSString *resourceDirectory = [[NSBundle mainBundle] resourcePath];
            
            char* str = (char*)[resourceDirectory UTF8String];
            strncpy(pathStorage, str, sizeof(pathStorage));
            
        }
    }
    
    return pathStorage;
}


bool getDLCEnabled() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:@"PLUS_DLC"] == YES;
}

void showMissingDataFileAlert() {
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "OK" },
    };
    
    const SDL_MessageBoxData messageboxdata = {
        SDL_MESSAGEBOX_INFORMATION,
        NULL,
        "Missing Game!",
        "The engine could not find a game configuration. Please copy a Data.rsdk file into the app through Files on your device, or Finder/iTunes on a PC or Mac. The engine will now halt.",
        SDL_arraysize(buttons),
        buttons,
        nullptr
    };
    
    int buttonid;
    if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0) {
        SDL_Log("error displaying message box??");
    }
}


int getDisplayRefresh() {
#if !TARGET_OS_MACCATALYST && !TARGET_OS_OSX
    UIScreen* screen = [UIScreen mainScreen];
    if([screen respondsToSelector:@selector(maximumFramesPerSecond)])
        return (int)[screen maximumFramesPerSecond];
#endif
    return 60;
}
#endif
 
