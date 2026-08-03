#import <UIKit/UIKit.h>
#include "hooks.hpp"
#include "menu.h"
#include "overlay.h"

// Tweak load entry point called by dynamic loader (DYLD) on inject
__attribute__((constructor))
static void MainEntry() {
    NSLog(@"[yt] ytpavlov_mc_ios dylib inject successful.");

    // Defer initialization to after application finishes loading to prevent crashes and ensure decryptions are complete
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidFinishLaunchingNotification
                                                      object:nil
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification * _Nonnull note) {
        NSLog(@"[yt] Minecraft application initialized. Initializing client layers...");
        
        // 1. Resolve code offsets dynamically and patch memory
        Hooks::Initialize();

        // 2. Initialize graphics drawing layer (transparent canvas)
        Overlay::Initialize();

        // 3. Initialize floating menu window interface
        Menu::Initialize();
    }];
}
