#import "overlay.h"
#include "hooks.hpp"
#include "sdk.hpp"
#include "memory.hpp"
#import <UIKit/UIKit.h>
#include <thread>
#include <chrono>

@interface MainESPView : UIView
@property (nonatomic, strong) CADisplayLink *displayLink;
@end

static MainESPView *gEspView = nil;

@implementation MainESPView

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = NO;
        
        _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(redraw)];
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
    }
    return self;
}

- (void)redraw {
    // Hook yok - scanner dogrudan global pointer'dan okur (jailbreak gerekmez)
    Hooks::ProcessContainerScanning(nullptr);
    if (Hooks::storageEspEnabled) {
        [self setNeedsDisplay];
    } else {
        self.layer.sublayers = nil;
    }
}

// Konteyner tipine gore renk
static UIColor* ColorForContainer(int type) {
    switch (type) {
        case 1: return [UIColor colorWithRed:0.94 green:0.62 blue:0.12 alpha:1.0]; // Chest: Altin
        case 2: return [UIColor colorWithRed:0.12 green:0.82 blue:0.82 alpha:1.0]; // EnderChest: Cyan
        case 3: return [UIColor colorWithRed:0.55 green:0.55 blue:0.58 alpha:1.0]; // Hopper: Gri
        case 4: return [UIColor colorWithRed:0.12 green:0.94 blue:0.12 alpha:1.0]; // Spawner: Yesil
        case 5: return [UIColor colorWithRed:0.82 green:0.12 blue:0.82 alpha:1.0]; // Shulker: Magenta
        case 6: return [UIColor colorWithRed:0.72 green:0.52 blue:0.32 alpha:1.0]; // Barrel: Kahve
        default: return [UIColor whiteColor];
    }
}

static NSString* NameForContainer(int type) {
    switch (type) {
        case 1: return @"Chest";
        case 2: return @"Ender Chest";
        case 3: return @"Hopper";
        case 4: return @"Spawner";
        case 5: return @"Shulker Box";
        case 6: return @"Barrel";
        default: return @"Container";
    }
}

- (void)drawRect:(CGRect)rect {
    CGContextRef context = UIGraphicsGetCurrentContext();
    if (!context) return;

    // View matrix - oyunun render matrix'i
    SDK::Matrix viewMatrix = *(SDK::Matrix*)(Memory::GetBaseAddress() + 0x2A00000);

    CGRect screenBounds = [UIScreen mainScreen].bounds;
    float width = screenBounds.size.width;
    float height = screenBounds.size.height;
    CGPoint screenCenter = CGPointMake(width / 2.0f, height / 2.0f);

    std::vector<Hooks::MappedContainer> localList;
    {
        std::lock_guard<std::mutex> lock(Hooks::containerMutex);
        localList = Hooks::detectedContainers;
    }

    for (const auto& obj : localList) {
        SDK::Vector2 screen{};
        if (SDK::WorldToScreen(obj.worldPos, screen, viewMatrix, width, height)) {
            
            UIColor *color = ColorForContainer(obj.type);
            NSString *label = [NSString stringWithFormat:@"%@ [%.1fm]",
                               NameForContainer(obj.type), obj.distance];

            // ESP kutusu (30x30)
            CGRect boxRect = CGRectMake(screen.x - 15, screen.y - 15, 30, 30);
            
            // Dis siyah golge
            CGContextSetStrokeColorWithColor(context, [UIColor blackColor].CGColor);
            CGContextSetLineWidth(context, 2.5);
            CGContextStrokeRect(context, boxRect);

            // Ana renkli cerceve
            CGContextSetStrokeColorWithColor(context, color.CGColor);
            CGContextSetLineWidth(context, 1.2);
            CGContextStrokeRect(context, boxRect);

            // Etiket
            NSDictionary *attrs = @{
                NSFontAttributeName: [UIFont systemFontOfSize:9.0 weight:UIFontWeightBold],
                NSForegroundColorAttributeName: color
            };
            [label drawAtPoint:CGPointMake(screen.x - 15, screen.y - 28) withAttributes:attrs];

            // Tracer cizgileri
            if (Hooks::drawTracers) {
                CGContextBeginPath(context);
                CGContextMoveToPoint(context, screenCenter.x, screenCenter.y);
                CGContextAddLineToPoint(context, screen.x, screen.y);
                CGContextSetStrokeColorWithColor(context, [UIColor colorWithRed:0 green:0 blue:0 alpha:0.4].CGColor);
                CGContextSetLineWidth(context, 2.0);
                CGContextStrokePath(context);

                CGContextBeginPath(context);
                CGContextMoveToPoint(context, screenCenter.x, screenCenter.y);
                CGContextAddLineToPoint(context, screen.x, screen.y);
                CGContextSetStrokeColorWithColor(context, color.CGColor);
                CGContextSetLineWidth(context, 0.8);
                CGContextStrokePath(context);
            }
        }
    }
}

@end

namespace Overlay {
    void Initialize() {
        dispatch_async(dispatch_get_main_queue(), ^{
            gEspView = [[MainESPView alloc] initWithFrame:[UIScreen mainScreen].bounds];
            
            id delegate = [UIApplication sharedApplication].delegate;
            if (delegate && [delegate respondsToSelector:@selector(window)]) {
                UIWindow *win = [delegate performSelector:@selector(window)];
                if (win) {
                    [win addSubview:gEspView];
                    [win bringSubviewToFront:gEspView];
                }
            }
        });
    }
    
    void SetVisible(bool visible) {
        dispatch_async(dispatch_get_main_queue(), ^{
            gEspView.hidden = !visible;
        });
    }
}
