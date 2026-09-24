// Compiled on the user's Mac with the standalone Command Line Tools and AppKit.
#import <AppKit/AppKit.h>

@interface ZvLauncher : NSObject <NSApplicationDelegate>
@property NSMutableArray<NSTask *> *viewers;
@property BOOL openedDuringLaunch;
@property BOOL finishedLaunching;
@end

@implementation ZvLauncher
- (instancetype)init {
    if ((self = [super init])) _viewers = [NSMutableArray array];
    return self;
}

- (void)openPaths:(NSArray<NSString *> *)paths {
    NSString *binary = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ZvExecutable"];
    NSError *error = nil;
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:binary];
    task.currentDirectoryURL = [NSURL fileURLWithPath:NSHomeDirectory()];
    task.arguments = [@[@"--"] arrayByAddingObjectsFromArray:paths];
    // Keep a direct parent/child relationship for macOS privacy attribution.
    // Do not detach, invoke a shell, or hand the process to launchd.
    task.terminationHandler = ^(NSTask *child) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.viewers removeObject:child];
            if (self.viewers.count == 0) [NSApp terminate:nil];
        });
    };
    if ([task launchAndReturnError:&error]) {
        [self.viewers addObject:task];
    } else {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Unable to start zv";
        alert.informativeText = [NSString stringWithFormat:
            @"%@\n\nExpected the installed viewer at:\n%@\n\nIf you moved or removed it, reinstall zv and run zv --install-desktop again.",
            error.localizedDescription, binary];
        [alert runModal];
        if (self.viewers.count == 0) [NSApp terminate:nil];
    }
}

- (void)application:(NSApplication *)application openFiles:(NSArray<NSString *> *)paths {
    self.openedDuringLaunch = YES;
    [self openPaths:paths];
    [application replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    self.finishedLaunching = YES;
    // AppKit delivers the initial open-files event before didFinishLaunching.
    if (!self.openedDuringLaunch) [self openPaths:@[]];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)application hasVisibleWindows:(BOOL)visible {
    if (!self.finishedLaunching) return YES;
    if (self.viewers.count == 0) {
        [self openPaths:@[]];
    } else {
        NSTask *task = self.viewers.lastObject;
        NSRunningApplication *viewer = [NSRunningApplication runningApplicationWithProcessIdentifier:task.processIdentifier];
        [viewer activateWithOptions:NSApplicationActivateAllWindows];
    }
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application {
    for (NSTask *task in [self.viewers copy]) {
        if (task.running) [task terminate];
    }
    return NSTerminateNow;
}
@end

int main(void) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        // The viewer provides the Dock icon and menu; the launcher only handles
        // Finder events and remains alive as the responsible parent process.
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        ZvLauncher *delegate = [[ZvLauncher alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
