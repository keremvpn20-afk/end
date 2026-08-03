#import "menu.h"
#include "hooks.hpp"
#import <UIKit/UIKit.h>
#import <mach-o/dyld.h>

@interface ClickGUIWindow : UIWindow
@property (nonatomic, strong) UIButton *floatingButton;
@property (nonatomic, strong) UIView *menuPanel;
@property (nonatomic, strong) UIView *settingsPanel; // Expandable filtration drawer
@property (nonatomic, strong) UIPanGestureRecognizer *panGesture;
@property (nonatomic, strong) UILabel *debugLabel;
@end

static ClickGUIWindow *gGuiWindow = nil;

@implementation ClickGUIWindow

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.windowLevel = UIWindowLevelAlert + 1;
        self.backgroundColor = [UIColor clearColor];
        self.hidden = NO; // Display window without stealing key focus from game window
        
        [self createFloatingBubble];
        [self createMenuPanel];
        [self createSettingsPanel];
        
        // Start stats refresh timer
        [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(updateDebugStats) userInfo:nil repeats:YES];
    }
    return self;
}

// 1. Draggable Floating Bubble
- (void)createFloatingBubble {
    self.floatingButton = [UIButton buttonWithType:UIButtonTypeCustom];
    self.floatingButton.frame = CGRectMake(80, 80, 55, 55);
    self.floatingButton.layer.cornerRadius = 27.5;
    
    // Meteor Client color aesthetic: Dark grey base with purple glow borders
    self.floatingButton.backgroundColor = [UIColor colorWithRed:0.12 green:0.12 blue:0.14 alpha:0.95];
    self.floatingButton.layer.borderWidth = 2.0;
    self.floatingButton.layer.borderColor = [UIColor colorWithRed:0.62 green:0.12 blue:0.94 alpha:1.0].CGColor;
    
    [self.floatingButton setTitle:@"M" forState:UIControlStateNormal];
    [self.floatingButton setTitleColor:[UIColor colorWithRed:0.94 green:0.45 blue:0.12 alpha:1.0] forState:UIControlStateNormal]; // Orange title
    self.floatingButton.titleLabel.font = [UIFont systemFontOfSize:22.0 weight:UIFontWeightBold];
    
    // Add shadow effects
    self.floatingButton.layer.shadowColor = [UIColor colorWithRed:0.62 green:0.12 blue:0.94 alpha:1.0].CGColor;
    self.floatingButton.layer.shadowOpacity = 0.5;
    self.floatingButton.layer.shadowOffset = CGSizeMake(0, 0);
    self.floatingButton.layer.shadowRadius = 8.0;
    
    [self.floatingButton addTarget:self action:@selector(toggleMenu) forControlEvents:UIControlEventTouchUpInside];
    
    // Setup pan gesture to drag the button
    self.panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    [self.floatingButton addGestureRecognizer:self.panGesture];
    
    [self addSubview:self.floatingButton];
}

- (void)handlePan:(UIPanGestureRecognizer *)sender {
    CGPoint translation = [sender translationInView:self];
    CGPoint newCenter = CGPointMake(sender.view.center.x + translation.x, sender.view.center.y + translation.y);
    
    // Clamp to screen bounds
    CGSize screenSize = [UIScreen mainScreen].bounds.size;
    newCenter.x = fmax(sender.view.frame.size.width / 2, fmin(screenSize.width - sender.view.frame.size.width / 2, newCenter.x));
    newCenter.y = fmax(sender.view.frame.size.height / 2, fmin(screenSize.height - sender.view.frame.size.height / 2, newCenter.y));
    
    sender.view.center = newCenter;
    [sender setTranslation:CGPointZero inView:self];
}

// 2. Sliding ClickGUI Module Selection Panel
- (void)createMenuPanel {
    CGSize screenSize = [UIScreen mainScreen].bounds.size;
    self.menuPanel = [[UIView alloc] initWithFrame:CGRectMake((screenSize.width - 320) / 2, (screenSize.height - 290) / 2, 320, 290)];
    self.menuPanel.backgroundColor = [UIColor colorWithRed:0.08 green:0.08 blue:0.10 alpha:0.95];
    self.menuPanel.layer.cornerRadius = 10;
    self.menuPanel.layer.borderWidth = 1.5;
    self.menuPanel.layer.borderColor = [UIColor colorWithRed:0.94 green:0.45 blue:0.12 alpha:1.0].CGColor; // Orange accent border
    self.menuPanel.hidden = YES;
    
    // Title header
    UILabel *titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(15, 10, 200, 24)];
    titleLabel.text = @"METEOR CLIENT PE";
    titleLabel.textColor = [UIColor whiteColor];
    titleLabel.font = [UIFont systemFontOfSize:15.0 weight:UIFontWeightBold];
    [self.menuPanel addSubview:titleLabel];
    
    // ----------------- Storage ESP Toggle Row -----------------
    UIView *espRow = [[UIView alloc] initWithFrame:CGRectMake(15, 55, 290, 45)];
    espRow.backgroundColor = [UIColor colorWithRed:0.14 green:0.14 blue:0.16 alpha:0.8];
    espRow.layer.cornerRadius = 6;
    
    UILabel *espLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 10, 150, 25)];
    espLabel.text = @"Storage ESP";
    espLabel.textColor = [UIColor whiteColor];
    espLabel.font = [UIFont systemFontOfSize:14.0 weight:UIFontWeightMedium];
    [espRow addSubview:espLabel];
    
    // Toggle switch
    UISwitch *espSwitch = [[UISwitch alloc] initWithFrame:CGRectMake(225, 7, 51, 31)];
    espSwitch.on = Hooks::storageEspEnabled;
    espSwitch.onTintColor = [UIColor colorWithRed:0.94 green:0.45 blue:0.12 alpha:1.0];
    [espSwitch addTarget:self action:@selector(espToggled:) forControlEvents:UIControlEventValueChanged];
    [espRow addSubview:espSwitch];
    
    // Three Dots button "..."
    UIButton *dotsButton = [UIButton buttonWithType:UIButtonTypeCustom];
    dotsButton.frame = CGRectMake(180, 7, 35, 30);
    [dotsButton setTitle:@"..." forState:UIControlStateNormal];
    [dotsButton setTitleColor:[UIColor colorWithRed:0.62 green:0.12 blue:0.94 alpha:1.0] forState:UIControlStateNormal];
    dotsButton.titleLabel.font = [UIFont systemFontOfSize:22.0 weight:UIFontWeightBold];
    [dotsButton addTarget:self action:@selector(openSettingsPanel) forControlEvents:UIControlEventTouchUpInside];
    [espRow addSubview:dotsButton];
    
    [self.menuPanel addSubview:espRow];
    
    // ----------------- Debug Statistics Label -----------------
    UIView *debugRow = [[UIView alloc] initWithFrame:CGRectMake(15, 110, 290, 115)];
    debugRow.backgroundColor = [UIColor colorWithRed:0.04 green:0.04 blue:0.06 alpha:0.85];
    debugRow.layer.cornerRadius = 6;
    debugRow.layer.borderWidth = 1.0;
    debugRow.layer.borderColor = [UIColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:1.0].CGColor;
    
    self.debugLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 5, 270, 105)];
    self.debugLabel.textColor = [UIColor colorWithRed:0.0 green:0.94 blue:0.45 alpha:1.0]; // Bright green font
    self.debugLabel.font = [UIFont fontWithName:@"Courier" size:9.0] ?: [UIFont systemFontOfSize:9.0 weight:UIFontWeightBold];
    self.debugLabel.numberOfLines = 0;
    self.debugLabel.text = @"DEBUG STATS:\nRetrieving binary info...\nWaiting for application launch hooks...";
    [debugRow addSubview:self.debugLabel];
    [self.menuPanel addSubview:debugRow];
    
    // Close button
    UIButton *closeBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    closeBtn.frame = CGRectMake(15, 240, 290, 35);
    closeBtn.backgroundColor = [UIColor colorWithRed:0.94 green:0.45 blue:0.12 alpha:0.3];
    closeBtn.layer.cornerRadius = 6;
    [closeBtn setTitle:@"CLOSE MENU" forState:UIControlStateNormal];
    [closeBtn setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    closeBtn.titleLabel.font = [UIFont systemFontOfSize:12.0 weight:UIFontWeightBold];
    [closeBtn addTarget:self action:@selector(toggleMenu) forControlEvents:UIControlEventTouchUpInside];
    [self.menuPanel addSubview:closeBtn];
    
    [self addSubview:self.menuPanel];
}

// 3. Filtration Drawer (The Sub-Settings Panel for ESP and Tracers)
- (void)createSettingsPanel {
    CGSize screenSize = [UIScreen mainScreen].bounds.size;
    
    // Appears on the right side of the main menu panel
    self.settingsPanel = [[UIView alloc] initWithFrame:CGRectMake((screenSize.width - 320) / 2 + 330, (screenSize.height - 360) / 2, 280, 360)];
    self.settingsPanel.backgroundColor = [UIColor colorWithRed:0.06 green:0.06 blue:0.08 alpha:0.98];
    self.settingsPanel.layer.cornerRadius = 10;
    self.settingsPanel.layer.borderWidth = 1.5;
    self.settingsPanel.layer.borderColor = [UIColor colorWithRed:0.62 green:0.12 blue:0.94 alpha:1.0].CGColor; // Purple border
    self.settingsPanel.hidden = YES;
    
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(15, 12, 200, 20)];
    title.text = @"ESP CONFIGURATION";
    title.textColor = [UIColor colorWithRed:0.62 green:0.12 blue:0.94 alpha:1.0];
    title.font = [UIFont systemFontOfSize:13.0 weight:UIFontWeightBold];
    [self.settingsPanel addSubview:title];
    
    // Scroll view for settings
    UIScrollView *scrollView = [[UIScrollView alloc] initWithFrame:CGRectMake(10, 45, 260, 300)];
    scrollView.contentSize = CGSizeMake(260, 380);
    
    float yOffset = 5;
    
    // Tracer Switch
    [self addSwitchToView:scrollView title:@"Draw Tracers" value:Hooks::drawTracers action:@selector(tracerToggled:) y:&yOffset];
    
    // Separator line
    UIView *sep = [[UIView alloc] initWithFrame:CGRectMake(5, yOffset + 5, 240, 1)];
    sep.backgroundColor = [UIColor colorWithRed:0.2 green:0.2 blue:0.2 alpha:1.0];
    [scrollView addSubview:sep];
    yOffset += 15;
    
    UILabel *filterTitle = [[UILabel alloc] initWithFrame:CGRectMake(5, yOffset, 200, 16)];
    filterTitle.text = @"Block Entity Filters:";
    filterTitle.textColor = [UIColor lightGrayColor];
    filterTitle.font = [UIFont systemFontOfSize:11.0 weight:UIFontWeightBold];
    [scrollView addSubview:filterTitle];
    yOffset += 22;
    
    // Filter toggles
    [self addSwitchToView:scrollView title:@"Chests" value:Hooks::filterChest action:@selector(chestFilterToggled:) y:&yOffset];
    [self addSwitchToView:scrollView title:@"Ender Chests" value:Hooks::filterEnderChest action:@selector(enderFilterToggled:) y:&yOffset];
    [self addSwitchToView:scrollView title:@"Shulker Boxes" value:Hooks::filterShulker action:@selector(shulkerFilterToggled:) y:&yOffset];
    [self addSwitchToView:scrollView title:@"Hoppers" value:Hooks::filterHopper action:@selector(hopperFilterToggled:) y:&yOffset];
    [self addSwitchToView:scrollView title:@"Monster Spawners" value:Hooks::filterSpawner action:@selector(spawnerFilterToggled:) y:&yOffset];
    [self addSwitchToView:scrollView title:@"Barrels" value:Hooks::filterBarrel action:@selector(barrelFilterToggled:) y:&yOffset];
    
    [self.settingsPanel addSubview:scrollView];
    [self addSubview:self.settingsPanel];
}

// Helper to quickly build checkbox rows
- (void)addSwitchToView:(UIView *)view title:(NSString *)title value:(bool)val action:(SEL)act y:(float *)y {
    UIView *row = [[UIView alloc] initWithFrame:CGRectMake(5, *y, 240, 36)];
    row.backgroundColor = [UIColor colorWithRed:0.12 green:0.12 blue:0.14 alpha:0.6];
    row.layer.cornerRadius = 4;
    
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(10, 8, 140, 20)];
    label.text = title;
    label.textColor = [UIColor whiteColor];
    label.font = [UIFont systemFontOfSize:12.0 weight:UIFontWeightRegular];
    [row addSubview:label];
    
    UISwitch *sw = [[UISwitch alloc] initWithFrame:CGRectMake(180, 3, 51, 31)];
    sw.on = val;
    sw.transform = CGAffineTransformMakeScale(0.75, 0.75); // Compact fit
    sw.onTintColor = [UIColor colorWithRed:0.62 green:0.12 blue:0.94 alpha:1.0];
    [sw addTarget:self action:act forControlEvents:UIControlEventValueChanged];
    [row addSubview:sw];
    
    [view addSubview:row];
    *y += 42;
}

// ----------------- Action Event Observers -----------------
- (void)updateDebugStats {
    uintptr_t baseAddr = (uintptr_t)_dyld_get_image_header(0);
    
    const char* hookStatus;
    if (Hooks::gTickAddressResolved == 0)          hookStatus = "FAILED (not in world)";
    else if (Hooks::gTickAddressResolved == 0xDEAD) hookStatus = "ClientInstance NOT found";
    else if (Hooks::gTickAddressResolved == 0xBEEF) hookStatus = "Calibrating offset...";
    else                                            hookStatus = "HOOKED (auto-calibrated)";
    
    self.debugLabel.text = [NSString stringWithFormat:@"DEBUG STATS:\nBase: 0x%lx\nOffset: 0x%lx\nStatus: %s\nPlayer: %s\nBS: 0x%lx (List: %d)\nEntities: %d",
                            baseAddr,
                            Hooks::gTickAddressResolved,
                            hookStatus,
                            (Hooks::gLocalPlayer ? "FOUND" : "NOT FOUND"),
                            Hooks::gDebugBlockSource,
                            Hooks::gDebugListSize,
                            Hooks::gScannedEntitiesCount];
}

- (void)espToggled:(UISwitch *)sender {
    Hooks::storageEspEnabled = sender.on;
}

- (void)tracerToggled:(UISwitch *)sender {
    Hooks::drawTracers = sender.on;
}

- (void)chestFilterToggled:(UISwitch *)sender {
    Hooks::filterChest = sender.on;
}

- (void)enderFilterToggled:(UISwitch *)sender {
    Hooks::filterEnderChest = sender.on;
}

- (void)shulkerFilterToggled:(UISwitch *)sender {
    Hooks::filterShulker = sender.on;
}

- (void)hopperFilterToggled:(UISwitch *)sender {
    Hooks::filterHopper = sender.on;
}

- (void)spawnerFilterToggled:(UISwitch *)sender {
    Hooks::filterSpawner = sender.on;
}

- (void)barrelFilterToggled:(UISwitch *)sender {
    Hooks::filterBarrel = sender.on;
}

- (void)toggleMenu {
    self.menuPanel.hidden = !self.menuPanel.hidden;
    if (self.menuPanel.hidden) {
        self.settingsPanel.hidden = YES;
    }
}

- (void)openSettingsPanel {
    self.settingsPanel.hidden = !self.settingsPanel.hidden;
}

// Override hitTest to allow interaction pass-through to game viewport on background clicks
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hitView = [super hitTest:point withEvent:event];
    if (hitView == self) {
        return nil; // Pass clicks to game
    }
    return hitView;
}

@end

namespace Menu {
    void Initialize() {
        dispatch_async(dispatch_get_main_queue(), ^{
            gGuiWindow = [[ClickGUIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
        });
    }
    
    void SetVisible(bool visible) {
        dispatch_async(dispatch_get_main_queue(), ^{
            gGuiWindow.floatingButton.hidden = !visible;
            if (!visible) {
                gGuiWindow.menuPanel.hidden = YES;
                gGuiWindow.settingsPanel.hidden = YES;
            }
        });
    }
    
    bool IsVisible() {
        return gGuiWindow && !gGuiWindow.floatingButton.hidden;
    }
}
