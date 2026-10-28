// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Marconi for iPad and iPhone: a connect screen, then the other computer's screen in a zoomable view with touch and
// keyboard input. MarconiCore runs unchanged; this file is the whole UIKit side.
//
// Gestures: tap = click, double-tap = double-click, touch and hold = right click, tap then hold = drag with the left
// button, one finger moves the pointer, two fingers move and pinch to zoom the picture.

#import <UIKit/UIKit.h>
#include <os/log.h>

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/QuickConnect.h>
#include <MarconiCore/RemoteSession.h>
#include <MarconiCore/Rfb/Framebuffer.h>
#include <Log/Log.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    NSString* ToNSString(const std::string& text)
    {
        NSString* result = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
        return result ?: @"";
    }

    std::string FromNSString(NSString* text)
    {
        return text.UTF8String ? text.UTF8String : "";
    }

    NSString* HexOf(const std::vector<uint8_t>& bytes)
    {
        NSMutableString* hex = [NSMutableString stringWithCapacity:bytes.size() * 2];
        for (uint8_t b : bytes)
        {
            [hex appendFormat:@"%02x", b];
        }
        return hex;
    }

    std::vector<uint8_t> BytesOf(NSString* hex)
    {
        std::vector<uint8_t> bytes;
        const std::string text = FromNSString(hex);
        for (size_t i = 0; i + 1 < text.size(); i += 2)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
        }
        return bytes;
    }

    void LogToSystem(Log::Level level, const char* tag, const char* message)
    {
        os_log_type_t type = OS_LOG_TYPE_DEFAULT;
        switch (level)
        {
            case Log::Level::Error: type = OS_LOG_TYPE_ERROR; break;
            case Log::Level::Warning: type = OS_LOG_TYPE_DEFAULT; break;
            case Log::Level::Info: type = OS_LOG_TYPE_INFO; break;
            case Log::Level::Verbose: type = OS_LOG_TYPE_DEBUG; break;
        }
        os_log_with_type(OS_LOG_DEFAULT, type, "[%{public}s] %{public}s", tag, message);
    }
}

@class ViewerController;

// The session's listener: hands each callback to the controller on the main queue. The questions wait here, on
// the connection thread, until the controller answers; closing the screen answers every open one with "no".
class ViewerListener : public Marconi::SessionListener
{
public:
    explicit ViewerListener(ViewerController* controller) : m_controller(controller) {}

    void Close();

    void OnConnected(const Marconi::Rfb::ServerInit& serverInit) override;
    void OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects) override;
    void OnDesktopResized(int width, int height) override;
    void OnServerCutText(const std::string& latin1Text) override;
    void OnServerClipboard(const Marconi::Rfb::ClipboardContent& content) override;
    void OnBell() override;
    void OnDisconnected(const Marconi::DisconnectInfo& info) override;
    bool OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials) override;
    bool OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate) override;
    bool OnUntrustedSshHostKey(const Marconi::Net::SshHostKeyQuestion& question) override;

private:
    // Runs `show` on the main queue with a block that delivers the answer, and waits for it.
    bool Ask(void (^show)(void (^answer)(BOOL)));

    __weak ViewerController* m_controller;
    std::mutex m_mutex;
    bool m_closed = false;
    std::vector<dispatch_semaphore_t> m_pending;
    Marconi::Rfb::Credentials m_credentials; // the answer to the last credentials question
};

// The other computer's screen: a UIImageView of the picture inside a scroll view that zooms and pans with two
// fingers, taking typing as a UIKeyInput and hardware keys through presses.
@interface ScreenView : UIScrollView <UIScrollViewDelegate, UIKeyInput>
@property(nonatomic, weak) ViewerController* controller;
@property(nonatomic, strong) UIImageView* imageView;
@property(nonatomic) CGSize pictureSize;
@property(nonatomic, strong) NSMutableSet<NSNumber*>* stickyModifiers;
@end

@interface ViewerController : UIViewController
- (instancetype)initWithAddress:(NSString*)address username:(NSString*)username password:(NSString*)password;
- (void)connected:(int)width height:(int)height name:(NSString*)name;
- (void)pictureChangedX:(int)x y:(int)y width:(int)width height:(int)height;
- (void)desktopResized:(int)width height:(int)height;
- (void)serverText:(NSString*)text;
- (void)bell;
- (void)disconnected:(NSString*)status;
- (void)askCredentials:(Marconi::Rfb::CredentialsKind)kind username:(NSString*)username answer:(void (^)(BOOL, NSString*, NSString*))answer;
- (void)askTrustCertificate:(NSString*)message fingerprint:(NSString*)fingerprint answer:(void (^)(BOOL))answer;
- (void)askTrustSshHostKey:(NSString*)message answer:(void (^)(BOOL))answer;
- (void)pointerX:(int)x y:(int)y buttons:(int)buttons;
- (void)key:(uint32_t)keysym down:(BOOL)down;
- (void)typeKeysym:(uint32_t)keysym;
@end

// --- The listener ---

void ViewerListener::Close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_closed = true;
    for (dispatch_semaphore_t pending : m_pending)
    {
        dispatch_semaphore_signal(pending);
    }
}

bool ViewerListener::Ask(void (^show)(void (^answer)(BOOL)))
{
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block BOOL result = NO;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed)
        {
            return false;
        }
        m_pending.push_back(done);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        show(^(BOOL answer) {
            result = answer;
            dispatch_semaphore_signal(done);
        });
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), done), m_pending.end());
        if (m_closed)
        {
            return false;
        }
    }
    return result == YES;
}

void ViewerListener::OnConnected(const Marconi::Rfb::ServerInit& serverInit)
{
    __weak ViewerController* controller = m_controller;
    NSString* name = ToNSString(serverInit.name);
    const int width = serverInit.width;
    const int height = serverInit.height;
    dispatch_async(dispatch_get_main_queue(), ^{ [controller connected:width height:height name:name]; });
}

void ViewerListener::OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects)
{
    if (rects.empty())
    {
        return;
    }
    Marconi::Rfb::Rect all = rects.front();
    for (size_t i = 1; i < rects.size(); ++i)
    {
        all = all.Union(rects[i]);
    }
    __weak ViewerController* controller = m_controller;
    dispatch_async(dispatch_get_main_queue(), ^{
        [controller pictureChangedX:all.x y:all.y width:all.width height:all.height];
    });
}

void ViewerListener::OnDesktopResized(int width, int height)
{
    __weak ViewerController* controller = m_controller;
    dispatch_async(dispatch_get_main_queue(), ^{ [controller desktopResized:width height:height]; });
}

void ViewerListener::OnServerCutText(const std::string& latin1Text)
{
    NSString* text = [[NSString alloc] initWithBytes:latin1Text.data() length:latin1Text.size() encoding:NSISOLatin1StringEncoding];
    __weak ViewerController* controller = m_controller;
    dispatch_async(dispatch_get_main_queue(), ^{ [controller serverText:text ?: @""]; });
}

void ViewerListener::OnServerClipboard(const Marconi::Rfb::ClipboardContent& content)
{
    if (content.utf8Text.empty())
    {
        return;
    }
    NSString* text = ToNSString(content.utf8Text);
    __weak ViewerController* controller = m_controller;
    dispatch_async(dispatch_get_main_queue(), ^{ [controller serverText:text]; });
}

void ViewerListener::OnBell()
{
    __weak ViewerController* controller = m_controller;
    dispatch_async(dispatch_get_main_queue(), ^{ [controller bell]; });
}

void ViewerListener::OnDisconnected(const Marconi::DisconnectInfo& info)
{
    NSString* status;
    if (info.authenticationFailed)
    {
        status = @"The password was refused";
    }
    else if (info.error.empty())
    {
        status = @"Disconnected";
    }
    else
    {
        status = [NSString stringWithFormat:@"%@: %@", info.wasConnected ? @"The connection was lost" : @"Couldn't connect",
                                            ToNSString(info.error)];
    }
    __weak ViewerController* controller = m_controller;
    dispatch_async(dispatch_get_main_queue(), ^{ [controller disconnected:status]; });
}

bool ViewerListener::OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials)
{
    __weak ViewerController* controller = m_controller;
    NSString* username = ToNSString(credentials.username);
    __block std::string user;
    __block std::string password;
    const bool answered = Ask(^(void (^answer)(BOOL)) {
        [controller askCredentials:kind username:username answer:^(BOOL ok, NSString* name, NSString* secret) {
            user = FromNSString(name);
            password = FromNSString(secret);
            answer(ok);
        }];
    });
    if (!answered)
    {
        return false;
    }
    credentials.username = user;
    credentials.password = password;
    return true;
}

bool ViewerListener::OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate)
{
    __weak ViewerController* controller = m_controller;
    NSString* fingerprint = HexOf(certificate.sha256);
    NSString* message = [NSString stringWithFormat:@"Issued to: %@\nIssued by: %@\nValid until: %@\n\n%@\n\nSHA-256 fingerprint:\n%@\n\nConnect anyway? Marconi remembers this certificate for this computer.",
                         ToNSString(certificate.subject), ToNSString(certificate.issuer), ToNSString(certificate.validUntil),
                         ToNSString(certificate.problem), fingerprint];
    return Ask(^(void (^answer)(BOOL)) { [controller askTrustCertificate:message fingerprint:fingerprint answer:answer]; });
}

bool ViewerListener::OnUntrustedSshHostKey(const Marconi::Net::SshHostKeyQuestion& question)
{
    __weak ViewerController* controller = m_controller;
    NSString* changed = question.previous.empty() ? @""
        : [NSString stringWithFormat:@"The key trusted before was\n%@\n\nA changed key can mean another computer is answering.\n\n", ToNSString(question.previous)];
    NSString* message = [NSString stringWithFormat:@"%@ offers a %@ key with fingerprint\n%@\n\n%@Trust it and connect?",
                         ToNSString(question.host), ToNSString(question.algorithm), ToNSString(question.fingerprint), changed];
    return Ask(^(void (^answer)(BOOL)) { [controller askTrustSshHostKey:message answer:answer]; });
}

// --- The screen view ---

@implementation ScreenView
{
    int _pointerX;
    int _pointerY;
    int _buttons;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.delegate = self;
        self.backgroundColor = UIColor.blackColor;
        self.showsHorizontalScrollIndicator = NO;
        self.showsVerticalScrollIndicator = NO;
        self.bouncesZoom = NO;
        self.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
        self.panGestureRecognizer.minimumNumberOfTouches = 2; // one finger is the pointer; two move the picture
        _stickyModifiers = [NSMutableSet set];

        _imageView = [[UIImageView alloc] initWithFrame:CGRectZero];
        _imageView.userInteractionEnabled = YES;
        _imageView.contentMode = UIViewContentModeScaleToFill;
        _imageView.layer.magnificationFilter = kCAFilterLinear;
        [self addSubview:_imageView];

        UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tapped:)];
        UITapGestureRecognizer* doubleTap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(doubleTapped:)];
        doubleTap.numberOfTapsRequired = 2;
        [tap requireGestureRecognizerToFail:doubleTap];
        UILongPressGestureRecognizer* hold = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(held:)];
        UILongPressGestureRecognizer* drag = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(dragged:)];
        drag.numberOfTapsRequired = 1; // tap, then hold and move
        UIPanGestureRecognizer* move = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(moved:)];
        move.maximumNumberOfTouches = 1;
        for (UIGestureRecognizer* recognizer in @[tap, doubleTap, hold, drag, move])
        {
            [_imageView addGestureRecognizer:recognizer];
        }
    }
    return self;
}

- (void)setPictureSize:(CGSize)size
{
    _pictureSize = size;
    _imageView.frame = CGRectMake(0, 0, size.width, size.height);
    self.contentSize = size;
    [self fitPicture];
}

- (void)fitPicture
{
    if (_pictureSize.width <= 0 || _pictureSize.height <= 0 || self.bounds.size.width <= 0)
    {
        return;
    }
    CGFloat fit = std::min(self.bounds.size.width / _pictureSize.width, self.bounds.size.height / _pictureSize.height);
    self.minimumZoomScale = fit;
    self.maximumZoomScale = fit * 8;
    self.zoomScale = fit;
    [self centerPicture];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    // Keep the picture centred while it is smaller than the view
    [self centerPicture];
}

- (void)centerPicture
{
    CGSize shown = CGSizeMake(_pictureSize.width * self.zoomScale, _pictureSize.height * self.zoomScale);
    CGFloat horizontal = std::max<CGFloat>(0, (self.bounds.size.width - shown.width) / 2);
    CGFloat vertical = std::max<CGFloat>(0, (self.bounds.size.height - shown.height) / 2);
    self.contentInset = UIEdgeInsetsMake(vertical, horizontal, vertical, horizontal);
}

- (UIView*)viewForZoomingInScrollView:(UIScrollView*)scrollView
{
    return _imageView;
}

- (void)scrollViewDidZoom:(UIScrollView*)scrollView
{
    [self centerPicture];
}

// --- Touch ---

- (BOOL)pointOf:(UIGestureRecognizer*)recognizer x:(int*)x y:(int*)y
{
    if (_pictureSize.width <= 0)
    {
        return NO;
    }
    CGPoint point = [recognizer locationInView:_imageView]; // one point per picture pixel
    *x = std::clamp(static_cast<int>(point.x), 0, static_cast<int>(_pictureSize.width) - 1);
    *y = std::clamp(static_cast<int>(point.y), 0, static_cast<int>(_pictureSize.height) - 1);
    return YES;
}

- (void)moveTo:(UIGestureRecognizer*)recognizer
{
    if ([self pointOf:recognizer x:&_pointerX y:&_pointerY])
    {
        [self.controller pointerX:_pointerX y:_pointerY buttons:_buttons];
    }
}

- (void)press:(int)button
{
    _buttons |= button;
    [self.controller pointerX:_pointerX y:_pointerY buttons:_buttons];
}

- (void)release:(int)button
{
    _buttons &= ~button;
    [self.controller pointerX:_pointerX y:_pointerY buttons:_buttons];
}

- (void)click:(UIGestureRecognizer*)recognizer button:(int)button
{
    [self moveTo:recognizer];
    [self press:button];
    [self release:button];
}

- (void)tapped:(UITapGestureRecognizer*)recognizer
{
    [self click:recognizer button:1];
}

- (void)doubleTapped:(UITapGestureRecognizer*)recognizer
{
    [self click:recognizer button:1];
    [self click:recognizer button:1];
}

- (void)held:(UILongPressGestureRecognizer*)recognizer
{
    if (recognizer.state == UIGestureRecognizerStateBegan)
    {
        [self click:recognizer button:4];
    }
}

- (void)dragged:(UILongPressGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateBegan:
            [self moveTo:recognizer];
            [self press:1];
            break;
        case UIGestureRecognizerStateChanged:
            [self moveTo:recognizer];
            break;
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateCancelled:
        case UIGestureRecognizerStateFailed:
            [self moveTo:recognizer];
            [self release:1];
            break;
        default:
            break;
    }
}

- (void)moved:(UIPanGestureRecognizer*)recognizer
{
    [self moveTo:recognizer];
}

// --- Keys ---

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

- (BOOL)hasText
{
    return YES;
}

- (UIKeyboardType)keyboardType
{
    return UIKeyboardTypeASCIICapable;
}

- (UITextAutocorrectionType)autocorrectionType
{
    return UITextAutocorrectionTypeNo;
}

- (UITextAutocapitalizationType)autocapitalizationType
{
    return UITextAutocapitalizationTypeNone;
}

- (void)insertText:(NSString*)text
{
    [text enumerateSubstringsInRange:NSMakeRange(0, text.length) options:NSStringEnumerationByComposedCharacterSequences
                          usingBlock:^(NSString* character, NSRange, NSRange, BOOL*) {
        if ([character isEqualToString:@"\n"])
        {
            [self.controller typeKeysym:Marconi::Keysym::Return];
        }
        else if ([character isEqualToString:@"\t"])
        {
            [self.controller typeKeysym:Marconi::Keysym::Tab];
        }
        else
        {
            uint32_t codepoint = 0;
            [character getBytes:&codepoint maxLength:sizeof(codepoint) usedLength:nullptr encoding:NSUTF32LittleEndianStringEncoding
                        options:0 range:NSMakeRange(0, character.length) remainingRange:nullptr];
            [self.controller typeKeysym:Marconi::Keysym::FromUnicode(codepoint)];
        }
    }];
}

- (void)deleteBackward
{
    [self.controller typeKeysym:Marconi::Keysym::BackSpace];
}

// Hardware keyboards: the keys that never arrive as text, and letters with Control, Option or Command held.
- (uint32_t)keysymForPress:(UIPress*)press
{
    UIKey* key = press.key;
    if (key == nil)
    {
        return 0;
    }
    switch (key.keyCode)
    {
        case UIKeyboardHIDUsageKeyboardEscape: return Marconi::Keysym::Escape;
        case UIKeyboardHIDUsageKeyboardTab: return Marconi::Keysym::Tab;
        case UIKeyboardHIDUsageKeyboardReturnOrEnter: return Marconi::Keysym::Return;
        case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: return Marconi::Keysym::BackSpace;
        case UIKeyboardHIDUsageKeyboardDeleteForward: return Marconi::Keysym::Delete;
        case UIKeyboardHIDUsageKeyboardLeftArrow: return Marconi::Keysym::Left;
        case UIKeyboardHIDUsageKeyboardRightArrow: return Marconi::Keysym::Right;
        case UIKeyboardHIDUsageKeyboardUpArrow: return Marconi::Keysym::Up;
        case UIKeyboardHIDUsageKeyboardDownArrow: return Marconi::Keysym::Down;
        case UIKeyboardHIDUsageKeyboardHome: return Marconi::Keysym::Home;
        case UIKeyboardHIDUsageKeyboardEnd: return Marconi::Keysym::End;
        case UIKeyboardHIDUsageKeyboardPageUp: return Marconi::Keysym::PageUp;
        case UIKeyboardHIDUsageKeyboardPageDown: return Marconi::Keysym::PageDown;
        case UIKeyboardHIDUsageKeyboardInsert: return Marconi::Keysym::Insert;
        case UIKeyboardHIDUsageKeyboardLeftShift: case UIKeyboardHIDUsageKeyboardRightShift: return Marconi::Keysym::Shift_L;
        case UIKeyboardHIDUsageKeyboardLeftControl: case UIKeyboardHIDUsageKeyboardRightControl: return Marconi::Keysym::Control_L;
        case UIKeyboardHIDUsageKeyboardLeftAlt: case UIKeyboardHIDUsageKeyboardRightAlt: return Marconi::Keysym::Alt_L;
        case UIKeyboardHIDUsageKeyboardLeftGUI: case UIKeyboardHIDUsageKeyboardRightGUI: return Marconi::Keysym::Super_L;
        default: break;
    }
    if (key.keyCode >= UIKeyboardHIDUsageKeyboardF1 && key.keyCode <= UIKeyboardHIDUsageKeyboardF12)
    {
        return Marconi::Keysym::F1 + (key.keyCode - UIKeyboardHIDUsageKeyboardF1);
    }
    // A letter with Control, Option or Command held never becomes text: send it as a key
    if (key.modifierFlags & (UIKeyModifierControl | UIKeyModifierAlternate | UIKeyModifierCommand))
    {
        NSString* characters = key.charactersIgnoringModifiers;
        if (characters.length > 0)
        {
            return Marconi::Keysym::FromUnicode([characters characterAtIndex:0]);
        }
    }
    return 0;
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    BOOL handled = NO;
    for (UIPress* press in presses)
    {
        uint32_t keysym = [self keysymForPress:press];
        if (keysym != 0)
        {
            [self.controller key:keysym down:YES];
            handled = YES;
        }
    }
    if (!handled)
    {
        [super pressesBegan:presses withEvent:event];
    }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    BOOL handled = NO;
    for (UIPress* press in presses)
    {
        uint32_t keysym = [self keysymForPress:press];
        if (keysym != 0)
        {
            [self.controller key:keysym down:NO];
            handled = YES;
        }
    }
    if (!handled)
    {
        [super pressesEnded:presses withEvent:event];
    }
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self pressesEnded:presses withEvent:event];
}

@end

// --- The viewer screen ---

@implementation ViewerController
{
    NSString* _address;
    NSString* _username;
    NSString* _password;
    std::unique_ptr<ViewerListener> _listener;
    std::unique_ptr<Marconi::RemoteSession> _session;
    std::vector<uint32_t> _pixels; // our copy of the picture, 0x00RRGGBB
    int _pictureWidth;
    int _pictureHeight;
    ScreenView* _screen;
    UIToolbar* _toolbar;
    NSArray<UIBarButtonItem*>* _modifierItems;
    BOOL _leaving;
}

- (instancetype)initWithAddress:(NSString*)address username:(NSString*)username password:(NSString*)password
{
    self = [super initWithNibName:nil bundle:nil];
    if (self)
    {
        _address = address;
        _username = username;
        _password = password;
        self.title = address;
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Disconnect" style:UIBarButtonItemStylePlain
                                                                             target:self action:@selector(disconnectTapped)];

    _screen = [[ScreenView alloc] initWithFrame:CGRectZero];
    _screen.controller = self;
    _screen.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_screen];

    _toolbar = [[UIToolbar alloc] initWithFrame:CGRectZero];
    _toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_toolbar];
    [self buildToolbar];

    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_screen.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [_screen.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [_screen.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [_screen.bottomAnchor constraintEqualToAnchor:_toolbar.topAnchor],
        [_toolbar.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [_toolbar.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [_toolbar.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor],
    ]];

    _listener = std::make_unique<ViewerListener>(self);
    Marconi::QuickConnectOptions options;
    options.address = FromNSString(_address);
    options.username = FromNSString(_username);
    options.password = FromNSString(_password);
    options.clientName = "Marconi for iPad";
    NSString* trusted = [NSUserDefaults.standardUserDefaults stringForKey:[@"certificate:" stringByAppendingString:_address]];
    options.trustedCertificateSha256 = BytesOf(trusted ?: @"");
    std::string error;
    _session = Marconi::QuickConnect(options, *_listener, error);
    if (!_session)
    {
        [self disconnected:ToNSString(error)];
    }
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    static CGSize lastSize;
    if (!CGSizeEqualToSize(lastSize, _screen.bounds.size))
    {
        lastSize = _screen.bounds.size;
        [_screen fitPicture];
    }
}

- (void)buildToolbar
{
    NSMutableArray<UIBarButtonItem*>* items = [NSMutableArray array];
    [items addObject:[[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"keyboard"] style:UIBarButtonItemStylePlain
                                                     target:self action:@selector(keyboardTapped)]];
    NSMutableArray<UIBarButtonItem*>* modifiers = [NSMutableArray array];
    for (NSArray* modifier in @[@[@"Ctrl", @(Marconi::Keysym::Control_L)], @[@"Alt", @(Marconi::Keysym::Alt_L)],
                                @[@"Shift", @(Marconi::Keysym::Shift_L)], @[@"Cmd", @(Marconi::Keysym::Super_L)]])
    {
        UIBarButtonItem* item = [[UIBarButtonItem alloc] initWithTitle:modifier[0] style:UIBarButtonItemStylePlain
                                                                target:self action:@selector(modifierTapped:)];
        item.tag = [modifier[1] intValue];
        [modifiers addObject:item];
        [items addObject:item];
    }
    _modifierItems = modifiers;
    for (NSArray* key in @[@[@"Esc", @(Marconi::Keysym::Escape)], @[@"Tab", @(Marconi::Keysym::Tab)],
                           @[@"←", @(Marconi::Keysym::Left)], @[@"↑", @(Marconi::Keysym::Up)], @[@"↓", @(Marconi::Keysym::Down)],
                           @[@"→", @(Marconi::Keysym::Right)], @[@"Del", @(Marconi::Keysym::Delete)]])
    {
        UIBarButtonItem* item = [[UIBarButtonItem alloc] initWithTitle:key[0] style:UIBarButtonItemStylePlain
                                                                target:self action:@selector(keyTapped:)];
        item.tag = [key[1] intValue];
        [items addObject:item];
    }
    _toolbar.items = items;
}

- (void)keyboardTapped
{
    if (_screen.isFirstResponder)
    {
        [_screen resignFirstResponder];
    }
    else
    {
        [_screen becomeFirstResponder];
    }
}

- (void)modifierTapped:(UIBarButtonItem*)item
{
    NSNumber* keysym = @(item.tag);
    if ([_screen.stickyModifiers containsObject:keysym])
    {
        [_screen.stickyModifiers removeObject:keysym];
        item.tintColor = nil;
    }
    else
    {
        [_screen.stickyModifiers addObject:keysym];
        item.tintColor = UIColor.systemOrangeColor;
    }
}

- (void)keyTapped:(UIBarButtonItem*)item
{
    [self typeKeysym:static_cast<uint32_t>(item.tag)];
}

- (void)disconnectTapped
{
    [self disconnected:@"Disconnected"];
}

// --- Input, from the screen view ---

- (void)pointerX:(int)x y:(int)y buttons:(int)buttons
{
    if (_session)
    {
        _session->SendPointerEvent(x, y, static_cast<uint8_t>(buttons));
    }
}

- (void)key:(uint32_t)keysym down:(BOOL)down
{
    if (_session)
    {
        _session->SendKeyEvent(keysym, down == YES);
    }
}

// A press and release, inside any modifiers the toolbar holds, which it then lets go of.
- (void)typeKeysym:(uint32_t)keysym
{
    if (keysym == 0)
    {
        return;
    }
    NSArray<NSNumber*>* held = _screen.stickyModifiers.allObjects;
    for (NSNumber* modifier in held)
    {
        [self key:modifier.unsignedIntValue down:YES];
    }
    [self key:keysym down:YES];
    [self key:keysym down:NO];
    for (NSNumber* modifier in held.reverseObjectEnumerator)
    {
        [self key:modifier.unsignedIntValue down:NO];
    }
    if (held.count > 0)
    {
        [_screen.stickyModifiers removeAllObjects];
        for (UIBarButtonItem* item in _modifierItems)
        {
            item.tintColor = nil;
        }
    }
}

// --- From the listener, on the main queue ---

- (void)connected:(int)width height:(int)height name:(NSString*)name
{
    self.title = name;
    [self resizePictureTo:width height:height];
    if (_session)
    {
        NSString* encryption = _session->IsEncrypted()
            ? [NSString stringWithFormat:@"Encrypted (%@)", ToNSString(_session->GetEncryptionName())] : @"Not encrypted";
        LOG_INFO("Viewer", "Connected to %s: %s", name.UTF8String, encryption.UTF8String);
    }
}

- (void)resizePictureTo:(int)width height:(int)height
{
    _pictureWidth = width;
    _pictureHeight = height;
    _pixels.assign(static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height)), 0);
    _screen.pictureSize = CGSizeMake(width, height);
    [self showPicture];
}

- (void)pictureChangedX:(int)x y:(int)y width:(int)width height:(int)height
{
    if (!_session)
    {
        return;
    }
    Marconi::Rfb::Framebuffer& framebuffer = _session->GetFramebuffer();
    {
        std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
        if (framebuffer.GetWidth() != _pictureWidth || framebuffer.GetHeight() != _pictureHeight)
        {
            // The picture changed size under us: take the whole thing again
            _pictureWidth = framebuffer.GetWidth();
            _pictureHeight = framebuffer.GetHeight();
            _pixels.assign(static_cast<size_t>(_pictureWidth) * _pictureHeight, 0);
            _screen.pictureSize = CGSizeMake(_pictureWidth, _pictureHeight);
            x = 0;
            y = 0;
            width = _pictureWidth;
            height = _pictureHeight;
        }
        const int left = std::max(0, x);
        const int top = std::max(0, y);
        const int right = std::min(_pictureWidth, x + width);
        const int bottom = std::min(_pictureHeight, y + height);
        for (int row = top; row < bottom; ++row)
        {
            std::copy(framebuffer.Row(row) + left, framebuffer.Row(row) + right,
                      _pixels.data() + static_cast<size_t>(row) * _pictureWidth + left);
        }
    }
    [self showPicture];
}

// The picture as a CGImage over a copy of the pixels: 0x00RRGGBB little-endian is XRGB with the X first in memory
// order reversed, which kCGBitmapByteOrder32Little with "skip first" describes exactly.
- (void)showPicture
{
    if (_pictureWidth <= 0 || _pictureHeight <= 0)
    {
        _screen.imageView.image = nil;
        return;
    }
    NSData* data = [NSData dataWithBytes:_pixels.data() length:_pixels.size() * sizeof(uint32_t)];
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(_pictureWidth, _pictureHeight, 8, 32, static_cast<size_t>(_pictureWidth) * 4, colorSpace,
                                     kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, provider, nullptr, false,
                                     kCGRenderingIntentDefault);
    _screen.imageView.image = image ? [UIImage imageWithCGImage:image] : nil;
    if (image) CGImageRelease(image);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);
}

- (void)desktopResized:(int)width height:(int)height
{
    [self resizePictureTo:width height:height];
    if (_session)
    {
        _session->RequestFullUpdate();
    }
}

- (void)serverText:(NSString*)text
{
    UIPasteboard.generalPasteboard.string = text;
}

- (void)bell
{
    [[[UINotificationFeedbackGenerator alloc] init] notificationOccurred:UINotificationFeedbackTypeWarning];
}

- (void)disconnected:(NSString*)status
{
    if (_leaving)
    {
        return;
    }
    _leaving = YES;
    [_screen resignFirstResponder];
    if (_listener)
    {
        _listener->Close(); // frees the connection thread from any open question before we wait for it
    }
    if (_session)
    {
        _session->Disconnect();
        _session.reset();
    }
    [NSNotificationCenter.defaultCenter postNotificationName:@"MarconiStatus" object:status];
    [self.navigationController popViewControllerAnimated:YES];
}

- (void)viewDidDisappear:(BOOL)animated
{
    [super viewDidDisappear:animated];
    if (!_leaving)
    {
        [self disconnected:@"Disconnected"]; // the back button
    }
}

// --- Questions from the connection thread ---

- (void)askCredentials:(Marconi::Rfb::CredentialsKind)kind username:(NSString*)username answer:(void (^)(BOOL, NSString*, NSString*))answer
{
    NSString* message;
    switch (kind)
    {
        case Marconi::Rfb::CredentialsKind::Password: message = @"This computer asks for a password."; break;
        case Marconi::Rfb::CredentialsKind::SshAccount: message = @"The computer's SSH login."; break;
        default: message = @"This computer asks for a user name and password."; break;
    }
    const BOOL wantsUsername = kind != Marconi::Rfb::CredentialsKind::Password;
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:_address message:message preferredStyle:UIAlertControllerStyleAlert];
    if (wantsUsername)
    {
        [alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
            field.placeholder = @"User name";
            field.text = username;
            field.autocapitalizationType = UITextAutocapitalizationTypeNone;
            field.autocorrectionType = UITextAutocorrectionTypeNo;
        }];
    }
    [alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
        field.placeholder = @"Password";
        field.secureTextEntry = YES;
    }];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { answer(NO, @"", @""); }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        NSString* name = wantsUsername ? alert.textFields[0].text : username;
        answer(YES, name ?: @"", alert.textFields.lastObject.text ?: @"");
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)askTrustCertificate:(NSString*)message fingerprint:(NSString*)fingerprint answer:(void (^)(BOOL))answer
{
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Not a certificate this device trusts" message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { answer(NO); }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Trust and connect" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        [NSUserDefaults.standardUserDefaults setObject:fingerprint forKey:[@"certificate:" stringByAppendingString:self->_address]];
        answer(YES);
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)askTrustSshHostKey:(NSString*)message answer:(void (^)(BOOL))answer
{
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Unknown SSH server key" message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { answer(NO); }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Trust and connect" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { answer(YES); }]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end

// --- The connect screen ---

@interface ConnectController : UIViewController
- (void)connectTo:(NSString*)address;
@end

@implementation ConnectController
{
    UITextField* _address;
    UITextField* _username;
    UITextField* _password;
    UILabel* _status;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Marconi";
    self.view.backgroundColor = UIColor.systemGroupedBackgroundColor;

    UITextField* (^field)(NSString*, NSString*) = ^(NSString* placeholder, NSString* saved) {
        UITextField* f = [UITextField new];
        f.placeholder = placeholder;
        f.text = [NSUserDefaults.standardUserDefaults stringForKey:saved] ?: @"";
        f.borderStyle = UITextBorderStyleRoundedRect;
        f.autocapitalizationType = UITextAutocapitalizationTypeNone;
        f.autocorrectionType = UITextAutocorrectionTypeNo;
        return f;
    };
    _address = field(@"Computer, for example studio-pc, 192.168.1.20:1 or marconi://host", @"address");
    _address.keyboardType = UIKeyboardTypeURL;
    _username = field(@"User name (optional)", @"username");
    _password = field(@"Password (optional)", @"");
    _password.secureTextEntry = YES;
    _password.delegate = (id<UITextFieldDelegate>)self;
    _password.returnKeyType = UIReturnKeyGo;

    UIButton* connect = [UIButton buttonWithType:UIButtonTypeSystem];
    connect.configuration = [UIButtonConfiguration filledButtonConfiguration];
    [connect setTitle:@"Connect" forState:UIControlStateNormal];
    [connect addTarget:self action:@selector(connectTapped) forControlEvents:UIControlEventTouchUpInside];

    _status = [UILabel new];
    _status.numberOfLines = 0;
    _status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];

    UILabel* help = [UILabel new];
    help.text = @"Tap to click, double-tap to double-click, touch and hold for a right click. Tap, then hold and move to drag. Two fingers move and zoom the picture.";
    help.numberOfLines = 0;
    help.textColor = UIColor.secondaryLabelColor;
    help.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];

    UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[_address, _username, _password, connect, _status, help]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 14;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stack];
    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:safe.topAnchor constant:20],
        [stack.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [_address.heightAnchor constraintEqualToConstant:44],
        [_username.heightAnchor constraintEqualToConstant:44],
        [_password.heightAnchor constraintEqualToConstant:44],
    ]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(statusChanged:) name:@"MarconiStatus" object:nil];
}

- (void)statusChanged:(NSNotification*)notification
{
    _status.text = [notification.object isKindOfClass:NSString.class] ? notification.object : @"";
}

- (BOOL)textFieldShouldReturn:(UITextField*)textField
{
    [self connectTapped];
    return YES;
}

- (void)connectTapped
{
    [self connectTo:_address.text ?: @""];
}

- (void)connectTo:(NSString*)address
{
    address = [address stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (address.length == 0)
    {
        _status.text = @"Enter the computer to connect to.";
        return;
    }
    _address.text = address;
    [NSUserDefaults.standardUserDefaults setObject:address forKey:@"address"];
    [NSUserDefaults.standardUserDefaults setObject:_username.text ?: @"" forKey:@"username"];
    [self.view endEditing:YES];
    _status.text = [NSString stringWithFormat:@"Connecting to %@…", address];
    ViewerController* viewer = [[ViewerController alloc] initWithAddress:address username:_username.text ?: @""
                                                                 password:_password.text ?: @""];
    [self.navigationController pushViewController:viewer animated:YES];
}

@end

// --- The app ---

@interface MarconiAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@property(nonatomic, strong) ConnectController* connect;
@end

@implementation MarconiAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    Log::SetCallback(LogToSystem);
    self.connect = [ConnectController new];
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:self.connect];
    [self.window makeKeyAndVisible];
    return YES;
}

// vnc:// and marconi:// links from other apps
- (BOOL)application:(UIApplication*)app openURL:(NSURL*)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id>*)options
{
    [self.connect.navigationController popToRootViewControllerAnimated:NO];
    [self.connect connectTo:url.absoluteString];
    return YES;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(MarconiAppDelegate.class));
    }
}
