#import "skiad.h"

@interface SkiaService : NSObject
@end

@implementation SkiaService {
    CPDistributedMessagingCenter *messagingCenter;
}

+ (instancetype)sharedInstance {
    static id instance;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc]init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        messagingCenter = [CPDistributedMessagingCenter centerNamed:SkiaIdentifier];
    }
    return self;
}

- (void)run {
    [messagingCenter runServerOnCurrentThread];
    [messagingCenter registerForMessageName:DaemonsMessage target:self selector:@selector(processDaemonsRequest:data:)];
    [messagingCenter registerForMessageName:OperationMessage target:self selector:@selector(processOperationRequest:data:)];
}

- (BOOL)validateDaemonName:(NSString *)name {
    return YES
    && name.length > 0
    && [name rangeOfString:@"^\\w+$" options:NSRegularExpressionSearch].location != NSNotFound;
}

- (BOOL)validateDaemonConfig:(NSDictionary<NSString *, NSString *> *)config {
    return YES
    && config[@"ServerAddress"].length > 0
    && config[@"ServerPort"].length > 0
    && config[@"LocalPort"].length > 0
    && config[@"Cipher"].length > 0
    && config[@"Key"].length > 0;
}

- (NSString *)daemonPlistFileForName:(NSString *)name {
    return [DaemonPlistDirectory stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.%@.plist", ShadowSocksIdentifier, name.lowercaseString]];
}

- (NSString *)daemonIdentifierForName:(NSString *)name {
    return [NSString stringWithFormat:@"%@.%@", ShadowSocksIdentifier, name.lowercaseString];
}

- (NSDictionary<NSString *, id> *)daemonPlistDictionaryForName:(NSString *)name config:(NSDictionary<NSString *, NSString *> *)config {
    NSMutableDictionary *daemonPlist = [NSMutableDictionary dictionary];
    daemonPlist[@"Label"] = [self daemonIdentifierForName:name];
    daemonPlist[@"RunAtLoad"] = @(YES);
    daemonPlist[@"KeepAlive"] = @(YES);
    daemonPlist[@"ProgramArguments"] = @[
        @"/usr/bin/ss-local",
        @"-s", config[@"ServerAddress"],
        @"-p", config[@"ServerPort"],
        @"-b", @"127.0.0.1",
        @"-l", config[@"LocalPort"],
        @"-m", config[@"Cipher"],
        @"-k", config[@"Key"],
        @"-t", @"300",
    ];
    return daemonPlist;
}

- (NSDictionary<NSString *, NSDictionary<NSString *, NSString *> *> *)daemonsStatusDictionary {
    NSMutableDictionary *daemonsStatus = [NSMutableDictionary dictionary];
    [[[NSFileManager defaultManager]contentsOfDirectoryAtPath:DaemonPlistDirectory error:nil]enumerateObjectsUsingBlock:^(NSString *filename, NSUInteger index, BOOL *stop) {
        if ([filename hasPrefix:ShadowSocksIdentifier] && [filename.pathExtension isEqualToString:@"plist"]) {
            NSString *name = [filename.stringByDeletingPathExtension substringFromIndex:ShadowSocksIdentifier.length + 1];
            NSDictionary *daemonPlist = [NSDictionary dictionaryWithContentsOfFile:[DaemonPlistDirectory stringByAppendingPathComponent:filename]];
            NSArray<NSString *> *daemonArguments = daemonPlist[@"ProgramArguments"];
            daemonsStatus[name] = @{
                @"ServerAddress": daemonArguments[[daemonArguments indexOfObject:@"-s"] + 1],
                @"ServerPort": daemonArguments[[daemonArguments indexOfObject:@"-p"] + 1],
                @"LocalAddress": daemonArguments[[daemonArguments indexOfObject:@"-b"] + 1],
                @"LocalPort": daemonArguments[[daemonArguments indexOfObject:@"-l"] + 1],
                @"Cipher": daemonArguments[[daemonArguments indexOfObject:@"-m"] + 1],
                @"Key": daemonArguments[[daemonArguments indexOfObject:@"-k"] + 1],
            };
        }
    }];
    return daemonsStatus;
}

- (void)runLaunchCtlCommand:(NSString *)command target:(NSString *)target {
    [[NSTask launchedTaskWithLaunchPath:LaunchCtl arguments:@[command, target]]waitUntilExit];
}

- (NSDictionary *)processDaemonsRequest:(NSString *)request data:(NSDictionary<NSString *, NSDictionary<NSString *, NSString *> *> *)data {
    if (data.count > 0) {
        [data enumerateKeysAndObjectsUsingBlock:^(NSString *name, NSDictionary<NSString *, NSString *> *config, BOOL *stop) {
            if ([self validateDaemonName:name]) {
                NSString *daemonPlistFile = [self daemonPlistFileForName:name];
                if (config.count > 0) {
                    if ([self validateDaemonConfig:config]) {
                        [self runLaunchCtlCommand:@"unload" target:daemonPlistFile];
                        [[self daemonPlistDictionaryForName:name config:config]writeToFile:daemonPlistFile atomically:YES];
                        [self runLaunchCtlCommand:@"load" target:daemonPlistFile];
                    }
                } else {
                    [self runLaunchCtlCommand:@"unload" target:daemonPlistFile];
                    [[NSFileManager defaultManager]removeItemAtPath:daemonPlistFile error:nil];
                }
            }
        }];
        return nil;
    } else {
        return [self daemonsStatusDictionary];
    }
}

- (NSDictionary *)processOperationRequest:(NSString *)request data:(NSDictionary<NSString *, NSString *> *)data {
    [data enumerateKeysAndObjectsUsingBlock:^(NSString *name, NSString *operation, BOOL *stop) {
        if ([self validateDaemonName:name]) {
            NSString *daemonIdentifier = [self daemonIdentifierForName:name];
            if ([operation isEqualToString:@"Start"]) {
                [self runLaunchCtlCommand:@"start" target:daemonIdentifier];
            } else if ([operation isEqualToString:@"Stop"]) {
                [self runLaunchCtlCommand:@"stop" target:daemonIdentifier];
            } else if ([operation isEqualToString:@"Restart"]) {
                [self runLaunchCtlCommand:@"stop" target:daemonIdentifier];
                [self runLaunchCtlCommand:@"start" target:daemonIdentifier];
            }
        }
    }];
    return nil;
}

@end

int main() {
    @autoreleasepool {
        [[SkiaService sharedInstance]run];
        [[NSRunLoop currentRunLoop]run];
    }
    return 0;
}