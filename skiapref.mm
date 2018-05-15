#import "skiad.h"

static NSString * const SkiaDaemonsUpdateNotification = @"me.qusic.skia.daemonsUpdate";

static CPDistributedMessagingCenter *messagingCenter = [CPDistributedMessagingCenter centerNamed:SkiaIdentifier];

static UIImage *imageNamed(NSString *name) {
    return [UIImage imageNamed:name inBundle:[NSBundle bundleWithPath:BundlePath] compatibleWithTraitCollection:nil];
}

@interface PSListItemsController : PSListController
@end

@interface SkiaPreferencesController : PSListController
@end

@interface SkiaDaemonController : PSListController
@end

@interface SkiaConfigController : PSViewController
@end

@interface SkiaTestController : PSListController
@end

@interface SkiaPreferencesController ()
@end

@implementation SkiaPreferencesController

- (instancetype)init {
    self = [super init];
    if (self) {
        [[NSNotificationCenter defaultCenter]addObserver:self selector:@selector(notificationAction:) name:SkiaDaemonsUpdateNotification object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter]removeObserver:self];
}

- (NSArray *)specifiers {
    if (_specifiers == nil) {
        NSMutableArray *specifiers = [NSMutableArray array];
        [specifiers addObjectsFromArray:self.daemonSpecifiers];
        [specifiers addObjectsFromArray:self.configSpecifiers];
        [specifiers addObjectsFromArray:self.aboutSpecifiers];
        _specifiers = specifiers;
    }
    return _specifiers;
}

- (NSArray *)daemonSpecifiers {
    NSMutableArray *specifiers = [NSMutableArray array];
    [specifiers addObject:[PSSpecifier groupSpecifierWithName:@"ShadowSocks Instances"]];
    [[messagingCenter sendMessageAndReceiveReplyName:DaemonsMessage userInfo:@{} error:nil]enumerateKeysAndObjectsUsingBlock:^(NSString *name, NSDictionary *properties, BOOL *stop) {
        PSSpecifier *specifier = [PSSpecifier preferenceSpecifierNamed:name target:self set:NULL get:NULL detail:SkiaDaemonController.class cell:PSLinkCell edit:Nil];
        [specifier setUserInfo:@[name, properties]];
        [specifier setProperty:[NSString stringWithFormat:@"%@:%@", properties[@"LocalAddress"], properties[@"LocalPort"]] forKey:@"cellSubtitleText"];
        [specifier setProperty:imageNamed(@"instance") forKey:@"iconImage"];
        [specifiers addObject:specifier];
    }];
    PSSpecifier *addSpecifier = [PSSpecifier preferenceSpecifierNamed:@"Add Instance" target:self set:NULL get:NULL detail:SkiaDaemonController.class cell:PSLinkCell edit:Nil];
    [addSpecifier setProperty:imageNamed(@"add") forKey:@"iconImage"];
    [specifiers addObject:addSpecifier];
    return specifiers;
}

- (NSArray *)configSpecifiers {
    PSSpecifier *viewSpecifier = [PSSpecifier preferenceSpecifierNamed:@"View Script" target:self set:NULL get:NULL detail:SkiaConfigController.class cell:PSLinkCell edit:Nil];
    [viewSpecifier setProperty:imageNamed(@"view") forKey:@"iconImage"];
    PSSpecifier *testSpecifier = [PSSpecifier preferenceSpecifierNamed:@"Result Test" target:self set:NULL get:NULL detail:SkiaTestController.class cell:PSLinkCell edit:Nil];
    [testSpecifier setProperty:imageNamed(@"test") forKey:@"iconImage"];
    return @[[PSSpecifier groupSpecifierWithName:@"Proxy Configuration"], viewSpecifier, testSpecifier];
}

- (NSArray *)aboutSpecifiers {
    PSSpecifier *twitterSpecifier = [PSSpecifier preferenceSpecifierNamed:@"@QusicS" target:self set:NULL get:NULL detail:Nil cell:PSButtonCell edit:Nil];
    twitterSpecifier.identifier = @"Twitter";
    twitterSpecifier.buttonAction = @selector(aboutAction:);
    [twitterSpecifier setProperty:imageNamed(@"twitter") forKey:@"iconImage"];
    return @[[PSSpecifier groupSpecifierWithName:@"About"], twitterSpecifier];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [super tableView:tableView cellForRowAtIndexPath:indexPath];
    PSSpecifier *specifier = [self specifierAtIndex:[self indexForRow:indexPath.row inGroup:indexPath.section]];
    NSString *subtitle = [specifier propertyForKey:@"cellSubtitleText"];
    if (subtitle != nil) {
        cell.detailTextLabel.text = subtitle;
    }
    return cell;
}

- (void)notificationAction:(NSNotification *)notification {
    if ([notification.name isEqualToString:SkiaDaemonsUpdateNotification]) {
        [self replaceContiguousSpecifiers:[self specifiersInGroup:0] withSpecifiers:self.daemonSpecifiers animated:YES];
    }
}

- (void)aboutAction:(PSSpecifier *)specifier {
    if ([specifier.identifier isEqualToString:@"Twitter"]) {
        [[UIApplication sharedApplication]openURL:[NSURL URLWithString:@"https://twitter.com/QusicS"]];
    }
}

@end

@interface SkiaDaemonController ()
@end

@implementation SkiaDaemonController {
    NSString *name;
    NSMutableDictionary<NSString *, NSString *> *properties;
}

- (NSString *)daemonName {
    return [NSString stringWithString:self.specifier.userInfo[0] ?: @""];
}

- (NSDictionary *)daemonProperties {
    return [NSDictionary dictionaryWithDictionary:self.specifier.userInfo[1] ?: @{}];
}

- (void)setSpecifier:(PSSpecifier *)specifier {
    [super setSpecifier:specifier];
    name = name ?: self.daemonName;
    properties = properties ?: self.daemonProperties.mutableCopy;
}

- (NSArray *)specifiers {
    if (_specifiers == nil) {
        NSMutableArray *specifiers = [NSMutableArray array];
        [specifiers addObjectsFromArray:self.fieldSpecifiers];
        [specifiers addObjectsFromArray:self.actionSpecifiers];
        _specifiers = specifiers;
    }
    return _specifiers;
}

- (NSArray *)fieldSpecifiers {
    PSSpecifier * (^fieldSpecifier)(NSString *, NSString *, UIKeyboardType, BOOL) = ^(NSString *identifier, NSString *displayName, UIKeyboardType keyboardType, BOOL secure) {
        PSSpecifier *specifier = [PSSpecifier preferenceSpecifierNamed:displayName target:self set:@selector(setValue:specifier:) get:@selector(getValue:) detail:Nil cell:secure ? PSSecureEditTextCell : PSEditTextCell edit:Nil];
        specifier.identifier = identifier;
        [specifier setKeyboardType:keyboardType autoCaps:UITextAutocapitalizationTypeNone autoCorrection:UITextAutocorrectionTypeNo];
        return specifier;
    };
    PSSpecifier *nameSpecifier = fieldSpecifier(@"Name", @"Name", UIKeyboardTypeASCIICapable, NO);
    PSSpecifier *serverAddressSpecifier = fieldSpecifier(@"ServerAddress", @"Server Address", UIKeyboardTypeURL, NO);
    PSSpecifier *serverPortSpecifier = fieldSpecifier(@"ServerPort", @"Server Port", UIKeyboardTypeNumberPad, NO);
    PSSpecifier *localPortSpecifier = fieldSpecifier(@"LocalPort", @"Local Port", UIKeyboardTypeNumberPad, NO);
    PSSpecifier *cipherSpecifier = [PSSpecifier preferenceSpecifierNamed:@"Encrypt Method" target:self set:@selector(setValue:specifier:) get:@selector(getValue:) detail:PSListItemsController.class cell:PSLinkListCell edit:Nil];
    cipherSpecifier.identifier = @"Cipher";
    static NSArray * const ciphers = @[@"table", @"rc4", @"rc4-md5", @"aes-128-cfb", @"aes-192-cfb", @"aes-256-cfb", @"bf-cfb", @"camellia-128-cfb", @"camellia-192-cfb", @"camellia-256-cfb", @"cast5-cfb", @"des-cfb", @"idea-cfb", @"rc2-cfb", @"seed-cfb", @"salsa20", @"chacha20"];
    [cipherSpecifier setValues:ciphers titles:ciphers];
    PSSpecifier *keySpecifier = fieldSpecifier(@"Key", @"Password", UIKeyboardTypeDefault, YES);
    return @[
        [PSSpecifier groupSpecifierWithName:nil],
        nameSpecifier, serverAddressSpecifier, serverPortSpecifier, localPortSpecifier, cipherSpecifier, keySpecifier,
    ];
}

- (NSArray *)actionSpecifiers {
    PSSpecifier * (^actionSpecifier)(NSString *, NSString *) = ^(NSString *actionName, NSString *confirmation) {
        Class specifierClass = confirmation ? PSConfirmationSpecifier.class : PSSpecifier.class;
        PSSpecifier *specifier = [specifierClass preferenceSpecifierNamed:actionName target:self set:NULL get:NULL detail:Nil cell:PSButtonCell edit:Nil];
        specifier.identifier = actionName;
        if (confirmation == nil) {
            specifier.buttonAction = @selector(specifierAction:);
        } else {
            PSConfirmationSpecifier *confirmationSpecifier = (PSConfirmationSpecifier *)specifier;
            confirmationSpecifier.confirmationAction = @selector(specifierAction:);
            confirmationSpecifier.title = actionName;
            confirmationSpecifier.prompt = confirmation;
            confirmationSpecifier.okButton = actionName;
            confirmationSpecifier.cancelButton = @"Cancel";
        }
        return specifier;
    };
    NSMutableArray *specifiers = [NSMutableArray array];
    BOOL exists = self.daemonName.length > 0;
    if (exists) {
        [specifiers addObjectsFromArray:@[
            [PSSpecifier groupSpecifierWithName:nil],
            actionSpecifier(@"Restart", @"This instance will be restarted and all active connections will be interrupted."),
        ]];
    }
    [specifiers addObjectsFromArray:@[
        [PSSpecifier groupSpecifierWithName:nil],
        actionSpecifier(@"Save", nil),
        actionSpecifier(@"Reset", @"All unsaved data will be lost."),
    ]];
    if (exists) {
        [specifiers addObjectsFromArray:@[
            [PSSpecifier groupSpecifierWithName:nil],
            actionSpecifier(@"Delete", @"This instance will be deleted and this operation cannot be undone."),
        ]];
    }
    return specifiers;
}

- (id)getValue:(PSSpecifier *)specifier {
    if ([specifier.identifier isEqualToString:@"Name"]) {
        return name;
    } else {
        return properties[specifier.identifier];
    }
}

- (void)setValue:(id)value specifier:(PSSpecifier *)specifier {
    if ([specifier.identifier isEqualToString:@"Name"]) {
        name = value;
    } else {
        properties[specifier.identifier] = value;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [super tableView:tableView cellForRowAtIndexPath:indexPath];
    if (cell.tag == PSEditTextCell || cell.tag == PSSecureEditTextCell) {
        UITextField *textField = ((PSEditableTableCell *)cell).textField;
        textField.textAlignment = NSTextAlignmentRight;
    }
    return cell;
}

- (void)scrollViewWillBeginDragging:(UIScrollView *)scrollView {
    [self.view endEditing:YES];
}

- (void)specifierAction:(PSSpecifier *)specifier {
    if ([specifier.identifier isEqualToString:@"Save"]) {
        void (^alertInvalidProperties)(NSString *) = ^(NSString *message) {
            UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Invalid Properties" message:message preferredStyle:UIAlertControllerStyleAlert];
            [alertController addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleCancel handler:NULL]];
            [self presentViewController:alertController animated:YES completion:NULL];
        };
        if (name.length == 0) {
            alertInvalidProperties(@"Name cannot be empty.");
            return;
        }
        if (properties[@"ServerAddress"].length == 0) {
            alertInvalidProperties(@"Server Address cannot be empty.");
            return;
        }
        if (properties[@"ServerPort"].length == 0) {
            alertInvalidProperties(@"Server Port cannot be empty.");
            return;
        }
        if (properties[@"LocalPort"].length == 0) {
            alertInvalidProperties(@"Local Port cannot be empty.");
            return;
        }
        if (properties[@"Cipher"].length == 0) {
            alertInvalidProperties(@"Encrypt Method cannot be empty.");
            return;
        }
        if (properties[@"Key"].length == 0) {
            alertInvalidProperties(@"Password cannot be empty.");
            return;
        }
        NSMutableDictionary *data = [NSMutableDictionary dictionary];
        data[name] = properties;
        NSString *oldName = self.daemonName;
        if (oldName.length > 0 && ![name isEqualToString:oldName]) {
            data[oldName] = @{};
        }
        [messagingCenter sendMessageAndReceiveReplyName:DaemonsMessage userInfo:data error:nil];
        [self.navigationController popViewControllerAnimated:YES];
        [[NSNotificationCenter defaultCenter]postNotificationName:SkiaDaemonsUpdateNotification object:nil];
    } else if ([specifier.identifier isEqualToString:@"Delete"]) {
        [messagingCenter sendMessageAndReceiveReplyName:DaemonsMessage userInfo:@{self.daemonName: @{}} error:nil];
        [self.navigationController popViewControllerAnimated:YES];
        [[NSNotificationCenter defaultCenter]postNotificationName:SkiaDaemonsUpdateNotification object:nil];
    } else if ([specifier.identifier isEqualToString:@"Reset"]) {
        name = self.daemonName;
        properties = self.daemonProperties.mutableCopy;
        [self reloadSpecifiers];
    } else {
        [messagingCenter sendMessageAndReceiveReplyName:OperationMessage userInfo:@{self.daemonName: specifier.identifier} error:nil];
    }
}

@end

@interface SkiaConfigController () <UIWebViewDelegate>
@end

@implementation SkiaConfigController {
    UIWebView *webView;
}

- (NSString *)configScriptContent {
    NSFileManager *fileManager = [NSFileManager defaultManager];
    if (![fileManager fileExistsAtPath:ConfigFile]) {
        [fileManager copyItemAtPath:ConfigSampleFile toPath:ConfigFile error:nil];
    }
    return [NSString stringWithContentsOfFile:ConfigFile encoding:NSUTF8StringEncoding error:nil] ?: @"";
}

- (void)setSpecifier:(PSSpecifier *)specifier {
    [super setSpecifier:specifier];
    self.title = specifier.name;
}

- (void)loadView {
    webView = [[UIWebView alloc]initWithFrame:CGRectZero];
    [webView setDelegate:self];
    [webView setAutoresizingMask:UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
    [webView setBackgroundColor:[PSEditingPane defaultBackgroundColor]];
    [webView setOpaque:NO];
    [webView setDataDetectorTypes:UIDataDetectorTypeNone];
    [webView _setDrawsCheckeredPattern:NO];
    [webView._browserView setTilesOpaque:NO];
    [webView._browserView setLoadsSynchronously:YES];
    [webView._scrollView setBackgroundColor:[UIColor clearColor]];
    [webView._scrollView setDecelerationRate:0.998];
    [webView._scrollView setShowBackgroundShadow:NO];
    self.view = webView;
}

- (void)loadContent {
    NSString *htmlFormatString = [NSString stringWithContentsOfFile:[ConfigViewBaseURL stringByAppendingPathComponent:@"index.html"] encoding:NSUTF8StringEncoding error:nil];
    [webView loadHTMLString:[NSString stringWithFormat:htmlFormatString, self.configScriptContent] baseURL:[NSURL fileURLWithPath:ConfigViewBaseURL]];
}

- (void)viewDidLoad {
    [self loadContent];
}

- (BOOL)webView:(UIWebView *)webView shouldStartLoadWithRequest:(NSURLRequest *)request navigationType:(UIWebViewNavigationType)navigationType {
    if (navigationType == UIWebViewNavigationTypeLinkClicked) {
        [[UIApplication sharedApplication]openURL:request.URL];
        return NO;
    }
    return YES;
}

@end

@interface SkiaTestController ()
@end

@implementation SkiaTestController {
    NSString *script;
}

- (void)setSpecifier:(PSSpecifier *)specifier {
    [super setSpecifier:specifier];
    script = script ?: @"queryProxy(app, host, port)";
}

- (NSArray *)specifiers {
    if (_specifiers == nil) {
        PSSpecifier *scriptSpecifier = [PSSpecifier preferenceSpecifierNamed:nil target:self set:@selector(setValue:forSpecifier:) get:@selector(getValue:) detail:Nil cell:PSEditTextViewCell edit:Nil];
        [scriptSpecifier setProperty:@0 forKey:@"textViewBottomMargin"];
        PSSpecifier *evaluateSpecifier = [PSSpecifier preferenceSpecifierNamed:@"Evaluate" target:self set:NULL get:NULL detail:Nil cell:PSButtonCell edit:Nil];
        evaluateSpecifier.buttonAction = @selector(specifierAction:);
        _specifiers = @[[PSSpecifier groupSpecifierWithName:@"Script"], scriptSpecifier, evaluateSpecifier, [PSSpecifier groupSpecifierWithName:@"Result"]];
    }
    return _specifiers;
}

- (id)getValue:(PSSpecifier *)specifier {
    return script;
}

- (void)setValue:(id)value forSpecifier:(PSSpecifier *)specifier {
    script = value;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [super tableView:tableView cellForRowAtIndexPath:indexPath];
    if (cell.tag == PSEditTextViewCell) {
        UITextView *textView = ((PSTextViewTableCell *)cell).textView;
        textView.font = [UIFont fontWithName:@"Menlo" size:[UIFont smallSystemFontSize]];
        textView.autocapitalizationType = UITextAutocapitalizationTypeNone;
        textView.autocorrectionType = UITextAutocorrectionTypeNo;
    }
    return cell;
}

- (void)specifierAction:(PSSpecifier *)specifier {
    [self.view endEditing:YES];
    NSString *result = [NSString stringWithCString:config().evaluate([script cStringUsingEncoding:NSUTF8StringEncoding]).c_str() encoding:NSUTF8StringEncoding];
    [[self specifierAtIndex:3]setProperty:result forKey:@"footerText"];
    [self reloadSpecifierAtIndex:3 animated:YES];
}

@end
