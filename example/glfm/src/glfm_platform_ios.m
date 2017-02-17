/*
 GLFM
 https://github.com/brackeen/glfm
 Copyright (c) 2014-2017 David Brackeen
 
 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#include "glfm.h"

#ifdef GLFM_PLATFORM_IOS

#import <UIKit/UIKit.h>

#include "glfm_platform.h"

#define MAX_SIMULTANEOUS_TOUCHES 10

#define CHECK_GL_ERROR() do { GLenum error = glGetError(); if (error != GL_NO_ERROR) \
NSLog(@"OpenGL error 0x%04x at glfm_platform_ios.m:%i", error, __LINE__); } while(0)

#pragma mark - EAGLView

@interface EAGLView : UIView {
    GLint _drawableWidth;
    GLint _drawableHeight;
    GLuint _defaultFramebuffer;
    GLuint _colorRenderbuffer;
    GLuint _attachmentRenderbuffer;
    GLuint _msaaFramebuffer;
    GLuint _msaaRenderbuffer;
}

@property(nonatomic, strong) EAGLContext *context;
@property(nonatomic, assign) NSString *colorFormat;
@property(nonatomic, assign) BOOL preserveBackbuffer;
@property(nonatomic, assign) NSUInteger depthBits;
@property(nonatomic, assign) NSUInteger stencilBits;
@property(nonatomic, assign) BOOL multisampling;
@property(nonatomic, readonly) NSUInteger drawableWidth;
@property(nonatomic, readonly) NSUInteger drawableHeight;

- (void)createDrawable;
- (void)deleteDrawable;
- (void)prepareRender;
- (void)finishRender;

@end

@implementation EAGLView

@dynamic drawableWidth, drawableHeight;

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (void)dealloc {
    [self deleteDrawable];
}

- (NSUInteger)drawableWidth {
    return (NSUInteger)_drawableWidth;
}

- (NSUInteger)drawableHeight {
    return (NSUInteger)_drawableHeight;
}

- (void)createDrawable {
    if (_defaultFramebuffer != 0 || !self.context) {
        return;
    }

    if (!self.colorFormat) {
        self.colorFormat = kEAGLColorFormatRGBA8;
    }

    [EAGLContext setCurrentContext:self.context];

    CAEAGLLayer *eaglLayer = (CAEAGLLayer *)self.layer;
    eaglLayer.opaque = YES;
    eaglLayer.drawableProperties =
        @{ kEAGLDrawablePropertyRetainedBacking : @(self.preserveBackbuffer),
           kEAGLDrawablePropertyColorFormat : self.colorFormat };

    glGenFramebuffers(1, &_defaultFramebuffer);
    glGenRenderbuffers(1, &_colorRenderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _defaultFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);

    // iPhone 6 Display Zoom hack
    CGRect oldBounds = eaglLayer.bounds;
    if (eaglLayer.contentsScale == 2.343750) {
        if (eaglLayer.bounds.size.width == 320.0 && eaglLayer.bounds.size.height == 568.0) {
            eaglLayer.bounds = CGRectMake(eaglLayer.bounds.origin.x, eaglLayer.bounds.origin.y,
                                          eaglLayer.bounds.size.width,
                                          1334 / eaglLayer.contentsScale);
        } else if (eaglLayer.bounds.size.width == 568.0 && eaglLayer.bounds.size.height == 320.0) {
            eaglLayer.bounds = CGRectMake(eaglLayer.bounds.origin.x, eaglLayer.bounds.origin.y,
                                          1334 / eaglLayer.contentsScale,
                                          eaglLayer.bounds.size.height);
        }
    }

    if (![self.context renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglLayer]) {
        NSLog(@"Error: Call to renderbufferStorage failed");
    }

    eaglLayer.bounds = oldBounds;

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              _colorRenderbuffer);

    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_drawableWidth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_drawableHeight);

    if (_multisampling) {
        glGenFramebuffers(1, &_msaaFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, _msaaFramebuffer);

        glGenRenderbuffers(1, &_msaaRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, _msaaRenderbuffer);

        GLenum internalformat = GL_RGBA8_OES;
        if ([kEAGLColorFormatRGB565 isEqualToString:_colorFormat]) {
            internalformat = GL_RGB565;
        }

        glRenderbufferStorageMultisampleAPPLE(GL_RENDERBUFFER, 4, internalformat,
                                              _drawableWidth, _drawableHeight);

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  _msaaRenderbuffer);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            NSLog(@"Error: Couldn't create multisample framebuffer: 0x%04x", status);
        }
    }

    if (_depthBits > 0 || _stencilBits > 0) {
        glGenRenderbuffers(1, &_attachmentRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, _attachmentRenderbuffer);

        GLenum internalformat;
        if (_depthBits > 0 && _stencilBits > 0) {
            internalformat = GL_DEPTH24_STENCIL8_OES;
        } else if (_depthBits >= 24) {
            internalformat = GL_DEPTH_COMPONENT24_OES;
        } else if (_depthBits > 0) {
            internalformat = GL_DEPTH_COMPONENT16;
        } else {
            internalformat = GL_STENCIL_INDEX8;
        }

        if (_multisampling) {
            glRenderbufferStorageMultisampleAPPLE(GL_RENDERBUFFER, 4, internalformat,
                                                  _drawableWidth, _drawableHeight);
        } else {
            glRenderbufferStorage(GL_RENDERBUFFER, internalformat, _drawableWidth, _drawableHeight);
        }

        if (_depthBits > 0) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      _attachmentRenderbuffer);
        }
        if (_stencilBits > 0) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                      _attachmentRenderbuffer);
        }
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        NSLog(@"Error: Framebuffer incomplete: 0x%04x", status);
    }

    CHECK_GL_ERROR();
}

- (void)deleteDrawable {
    if (_defaultFramebuffer) {
        glDeleteFramebuffers(1, &_defaultFramebuffer);
        _defaultFramebuffer = 0;
    }
    if (_colorRenderbuffer) {
        glDeleteRenderbuffers(1, &_colorRenderbuffer);
        _colorRenderbuffer = 0;
    }
    if (_attachmentRenderbuffer) {
        glDeleteRenderbuffers(1, &_attachmentRenderbuffer);
        _attachmentRenderbuffer = 0;
    }
    if (_msaaRenderbuffer) {
        glDeleteRenderbuffers(1, &_msaaRenderbuffer);
        _msaaRenderbuffer = 0;
    }
    if (_msaaFramebuffer) {
        glDeleteFramebuffers(1, &_msaaFramebuffer);
        _msaaFramebuffer = 0;
    }
}

- (void)prepareRender {
    [EAGLContext setCurrentContext:self.context];
    if (_multisampling) {
        glBindFramebuffer(GL_FRAMEBUFFER, _msaaFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, _msaaRenderbuffer);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, _defaultFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    }
    CHECK_GL_ERROR();
}

- (void)finishRender {
    if (_multisampling) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER_APPLE, _msaaFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER_APPLE, _defaultFramebuffer);
        glResolveMultisampleFramebufferAPPLE();
    }

    static bool checked_GL_EXT_discard_framebuffer = false;
    static bool has_GL_EXT_discard_framebuffer = false;
    if (!checked_GL_EXT_discard_framebuffer) {
        checked_GL_EXT_discard_framebuffer = true;
        if (glfmExtensionSupported("GL_EXT_discard_framebuffer")) {
            has_GL_EXT_discard_framebuffer = true;
        }
    }
    if (has_GL_EXT_discard_framebuffer) {
        GLenum target = GL_FRAMEBUFFER;
        GLenum attachments[3];
        GLsizei numAttachments = 0;
        if (_multisampling) {
            target = GL_READ_FRAMEBUFFER_APPLE;
            attachments[numAttachments++] = GL_COLOR_ATTACHMENT0;
        }
        if (_depthBits > 0) {
            attachments[numAttachments++] = GL_DEPTH_ATTACHMENT;
        }
        if (_stencilBits > 0) {
            attachments[numAttachments++] = GL_STENCIL_ATTACHMENT;
        }
        if (numAttachments > 0) {
            if (_multisampling) {
                glBindFramebuffer(GL_FRAMEBUFFER, _msaaFramebuffer);
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, _defaultFramebuffer);
            }
            glDiscardFramebufferEXT(target, numAttachments, attachments);
        }
    }

    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    [self.context presentRenderbuffer:GL_RENDERBUFFER];

    CHECK_GL_ERROR();
}

- (void)layoutSubviews {
    NSUInteger newDrawableWidth = (NSUInteger)(self.bounds.size.width * self.contentScaleFactor);
    NSUInteger newDrawableHeight = (NSUInteger)(self.bounds.size.height * self.contentScaleFactor);

    // iPhone 6 Display Zoom hack
    if (self.contentScaleFactor == 2.343750) {
        if (newDrawableWidth == 750 && newDrawableHeight == 1331) {
            newDrawableHeight = 1334;
        } else if (newDrawableWidth == 1331 && newDrawableHeight == 750) {
            newDrawableWidth = 1334;
        }
    }

    if (self.drawableWidth != newDrawableWidth || self.drawableHeight != newDrawableHeight) {
        [self deleteDrawable];
        [self createDrawable];
    }
}

@end

#pragma mark - GLFMViewController

@interface GLFMViewController : UIViewController {
    const void *activeTouches[MAX_SIMULTANEOUS_TOUCHES];
}

@property(nonatomic, strong) EAGLContext *context;
@property(nonatomic, strong) CADisplayLink *displayLink;
@property(nonatomic, assign) GLFMDisplay *glfmDisplay;
@property(nonatomic, assign) CGSize drawableSize;
@property(nonatomic, assign) BOOL multipleTouchEnabled;

@end

@implementation GLFMViewController

- (id)init {
    if ((self = [super init])) {
        [self clearTouches];
        _glfmDisplay = calloc(1, sizeof(GLFMDisplay));
        _glfmDisplay->platformData = (__bridge void *)self;
        self.drawableSize = [self preferredDrawableSize];
        const char *path = glfmGetDirectoryPath(GLFMDirectoryApp);
        if (path) {
            chdir(path);
        }
        glfmMain(_glfmDisplay);
    }
    return self;
}

- (BOOL)animating {
    return (self.displayLink != nil);
}

- (void)setAnimating:(BOOL)animating {
    if (self.animating != animating) {
        if (!animating) {
            [self.displayLink invalidate];
            self.displayLink = nil;
        } else {
            self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                           selector:@selector(render:)];
            [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        }
    }
}

- (BOOL)prefersStatusBarHidden {
    return _glfmDisplay->uiChrome != GLFMUserInterfaceChromeNavigationAndStatusBar;
}

- (CGSize)preferredDrawableSize {
    // TODO: Remove deprecated interfaceOrientation, but wait until Split View is implemented.
    BOOL isPortrait = UIInterfaceOrientationIsPortrait(self.interfaceOrientation);
    // NOTE: [UIScreen mainScreen].nativeBounds is always in portrait orientation,
    // and [UIScreen mainScreen].bounds is orientation-aware in iOS 8 and newer.
    CGSize size = [UIScreen mainScreen].nativeBounds.size;

    /*
     iPhone 6 Display Zoom hack
     Device (Mode)                scale   bounds    nativeScale         nativeBounds   drawable
     iPhone 6 (Display Zoom)        2.0   320x568   2.34375             750x1331.25    750x1331
     iPhone 6 (Standard)            2.0   375x667   2.0                 750x1334       750x1334
     iPhone 6 Plus (Display Zoom)   3.0   375x667   2.88                1080x1920.96   1080x1920
     iPhone 6 Plus (Standard)       3.0   414x736   2.608695652173913   1080x1920      1080x1920
     */
    if (size.width == 750.0 && size.height == 1331.25) {
        size.height = 1334;
    } else if (size.width == 1080.0 && size.height == 1920.96) {
        size.height = 1920;
    }
    if (isPortrait) {
        return CGSizeMake(size.width, size.height);
    } else {
        return CGSizeMake(size.height, size.width);
    }
}

- (void)loadView {
    self.view = [[EAGLView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]];
    self.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view.contentScaleFactor = [UIScreen mainScreen].nativeScale;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    if (_glfmDisplay->preferredAPI >= GLFMRenderingAPIOpenGLES3) {
        self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    }
    if (!self.context) {
        self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    }

    if (!self.context) {
        reportSurfaceError(_glfmDisplay, "Failed to create ES context");
        return;
    }

    EAGLView *view = (EAGLView *)self.view;
    view.multipleTouchEnabled = self.multipleTouchEnabled;
    view.context = self.context;

    [self setNeedsStatusBarAppearanceUpdate];

    switch (_glfmDisplay->colorFormat) {
        case GLFMColorFormatRGB565:
            view.colorFormat = kEAGLColorFormatRGB565;
            break;
        case GLFMColorFormatRGBA8888:
        default:
            view.colorFormat = kEAGLColorFormatRGBA8;
            break;
    }

    switch (_glfmDisplay->depthFormat) {
        case GLFMDepthFormatNone:
        default:
            view.depthBits = 0;
            break;
        case GLFMDepthFormat16:
            view.depthBits = 16;
            break;
        case GLFMDepthFormat24:
            view.depthBits = 24;
            break;
    }

    switch (_glfmDisplay->stencilFormat) {
        case GLFMStencilFormatNone:
        default:
            view.stencilBits = 0;
            break;
        case GLFMStencilFormat8:
            view.stencilBits = 8;
            break;
    }

    view.multisampling = _glfmDisplay->multisample != GLFMMultisampleNone;

    [view createDrawable];

    if (view.drawableWidth > 0 && view.drawableHeight > 0) {
        self.drawableSize = CGSizeMake(view.drawableWidth, view.drawableHeight);
        if (_glfmDisplay->surfaceCreatedFunc) {
            _glfmDisplay->surfaceCreatedFunc(_glfmDisplay, (int)self.drawableSize.width,
                                             (int)self.drawableSize.height);
        }
        self.animating = YES;
    }
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    BOOL isTablet = (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad);
    GLFMUserInterfaceOrientation uiOrientations = _glfmDisplay->allowedOrientations;
    if (uiOrientations == GLFMUserInterfaceOrientationAny) {
        if (isTablet) {
            return UIInterfaceOrientationMaskAll;
        } else {
            return UIInterfaceOrientationMaskAllButUpsideDown;
        }
    } else if (uiOrientations == GLFMUserInterfaceOrientationPortrait) {
        if (isTablet) {
            return (UIInterfaceOrientationMask)(UIInterfaceOrientationMaskPortrait |
                    UIInterfaceOrientationMaskPortraitUpsideDown);
        } else {
            return UIInterfaceOrientationMaskPortrait;
        }
    } else {
        return UIInterfaceOrientationMaskLandscape;
    }
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    if (_glfmDisplay->lowMemoryFunc) {
        _glfmDisplay->lowMemoryFunc(_glfmDisplay);
    }
}

- (void)dealloc {
    [self setAnimating:NO];
    if ([EAGLContext currentContext] == self.context) {
        [EAGLContext setCurrentContext:nil];
    }
    if (_glfmDisplay->surfaceDestroyedFunc) {
        _glfmDisplay->surfaceDestroyedFunc(_glfmDisplay);
    }
    free(_glfmDisplay);
}

- (void)render:(CADisplayLink *)displayLink {
    EAGLView *view = (EAGLView *)self.view;

    CGSize newDrawableSize = CGSizeMake(view.drawableWidth, view.drawableHeight);
    if (!CGSizeEqualToSize(self.drawableSize, newDrawableSize)) {
        self.drawableSize = newDrawableSize;
        if (_glfmDisplay->surfaceResizedFunc) {
            _glfmDisplay->surfaceResizedFunc(_glfmDisplay, (int)self.drawableSize.width,
                                             (int)self.drawableSize.height);
        }
    }

    [view prepareRender];
    if (_glfmDisplay->mainLoopFunc) {
        _glfmDisplay->mainLoopFunc(_glfmDisplay, displayLink.timestamp);
    }
    [view finishRender];
}

#pragma mark - UIResponder

- (void)clearTouches {
    for (int i = 0; i < MAX_SIMULTANEOUS_TOUCHES; i++) {
        activeTouches[i] = NULL;
    }
}

- (void)addTouchEvent:(UITouch *)touch withType:(GLFMTouchPhase)phase {
    int firstNullIndex = -1;
    int index = -1;
    for (int i = 0; i < MAX_SIMULTANEOUS_TOUCHES; i++) {
        if (activeTouches[i] == (__bridge const void *)touch) {
            index = i;
            break;
        } else if (firstNullIndex == -1 && activeTouches[i] == NULL) {
            firstNullIndex = i;
        }
    }
    if (index == -1) {
        if (firstNullIndex == -1) {
            // Shouldn't happen
            return;
        }
        index = firstNullIndex;
        activeTouches[index] = (__bridge const void *)touch;
    }

    if (_glfmDisplay->touchFunc) {
        CGPoint currLocation = [touch locationInView:self.view];
        currLocation.x *= self.view.contentScaleFactor;
        currLocation.y *= self.view.contentScaleFactor;

        _glfmDisplay->touchFunc(_glfmDisplay, index, phase,
                                (int)currLocation.x, (int)currLocation.y);
    }

    if (phase == GLFMTouchPhaseEnded || phase == GLFMTouchPhaseCancelled) {
        activeTouches[index] = NULL;
    }
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        [self addTouchEvent:touch withType:GLFMTouchPhaseBegan];
    }
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        [self addTouchEvent:touch withType:GLFMTouchPhaseMoved];
    }
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        [self addTouchEvent:touch withType:GLFMTouchPhaseEnded];
    }
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        [self addTouchEvent:touch withType:GLFMTouchPhaseCancelled];
    }
}

#pragma mark - Key commands

// Key input only works when a key is pressed, and has no repeat events or release events.
// Not ideal, but useful for prototyping.

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (NSArray *)keyCommands {
    static NSArray *keyCommands = NULL;
    if (!keyCommands) {
        keyCommands = @[ [UIKeyCommand keyCommandWithInput:UIKeyInputUpArrow
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:UIKeyInputDownArrow
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:UIKeyInputLeftArrow
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:UIKeyInputRightArrow
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:UIKeyInputEscape
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:@" "
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:@"\r"
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:@"\t"
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)],
                         [UIKeyCommand keyCommandWithInput:@"\b"
                                             modifierFlags:(UIKeyModifierFlags)0
                                                    action:@selector(keyPressed:)] ];
    };

    return keyCommands;
}

- (void)keyPressed:(UIKeyCommand *)keyCommand {
    if (_glfmDisplay->keyFunc) {
        NSString *key = [keyCommand input];
        GLFMKey keyCode = (GLFMKey)0;
        if (key == UIKeyInputUpArrow) {
            keyCode = GLFMKeyUp;
        } else if (key == UIKeyInputDownArrow) {
            keyCode = GLFMKeyDown;
        } else if (key == UIKeyInputLeftArrow) {
            keyCode = GLFMKeyLeft;
        } else if (key == UIKeyInputRightArrow) {
            keyCode = GLFMKeyRight;
        } else if (key == UIKeyInputEscape) {
            keyCode = GLFMKeyEscape;
        } else if ([@" " isEqualToString:key]) {
            keyCode = GLFMKeySpace;
        } else if ([@"\t" isEqualToString:key]) {
            keyCode = GLFMKeyTab;
        } else if ([@"\b" isEqualToString:key]) {
            keyCode = GLFMKeyBackspace;
        } else if ([@"\r" isEqualToString:key]) {
            keyCode = GLFMKeyEnter;
        }

        if (keyCode != 0) {
            _glfmDisplay->keyFunc(_glfmDisplay, keyCode, GLFMKeyActionPressed, 0);
        }
    }
}

@end

#pragma mark - Application Delegate

@interface GLFMAppDelegate : NSObject <UIApplicationDelegate>

@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, assign) BOOL active;

@end

@implementation GLFMAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    _active = YES;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[GLFMViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)setActive:(BOOL)active {
    if (_active != active) {
        _active = active;

        GLFMViewController *vc = (GLFMViewController *)[self.window rootViewController];
        [vc clearTouches];
        vc.animating = active;
        if (vc.glfmDisplay) {
            if (_active && vc.glfmDisplay->resumingFunc) {
                vc.glfmDisplay->resumingFunc(vc.glfmDisplay);
            } else if (!_active && vc.glfmDisplay->pausingFunc) {
                vc.glfmDisplay->pausingFunc(vc.glfmDisplay);
            }
        }
    }
}

- (void)applicationWillResignActive:(UIApplication *)application {
    self.active = NO;
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    self.active = NO;
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
    self.active = YES;
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
    self.active = YES;
}

- (void)applicationWillTerminate:(UIApplication *)application {
    self.active = NO;
}

@end

#pragma mark - Main

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(0, NULL, nil, NSStringFromClass([GLFMAppDelegate class]));
    }
}

#pragma mark - GLFM implementation

const char *glfmGetDirectoryPath(GLFMDirectory directory) {
    static char *appPath = NULL;
    static char *docPath = NULL;

    switch (directory) {
        case GLFMDirectoryApp: default:
            if (!appPath) {
                appPath = strdup([NSBundle mainBundle].bundlePath.fileSystemRepresentation);
            }
            return appPath;
        case GLFMDirectoryDocuments:
            if (!docPath) {
                NSArray<NSString *> *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                                                NSUserDomainMask,
                                                                                YES);
                if (path.count > 0) {
                    docPath = strdup(path[0].fileSystemRepresentation);
                }
            }
            return docPath;
    }
}

void glfmSetUserInterfaceOrientation(GLFMDisplay *display,
                                     GLFMUserInterfaceOrientation allowedOrientations) {
    if (display) {
        if (display->allowedOrientations != allowedOrientations) {
            display->allowedOrientations = allowedOrientations;

            // HACK: Notify that the value of supportedInterfaceOrientations has changed
            GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
            if (vc.view.window) {
                UIViewController *dummyVC = [[UIViewController alloc] init];
                dummyVC.view = [[UIView alloc] init];
                [vc presentViewController:dummyVC animated:NO completion:^{
                  [vc dismissViewControllerAnimated:NO completion:NULL];
                }];
            }
        }
    }
}

int glfmGetDisplayWidth(GLFMDisplay *display) {
    if (display && display->platformData) {
        GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
        return (int)vc.drawableSize.width;
    } else {
        return 0;
    }
}

int glfmGetDisplayHeight(GLFMDisplay *display) {
    if (display && display->platformData) {
        GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
        return (int)vc.drawableSize.height;
    } else {
        return 0;
    }
}

double glfmGetDisplayScale(GLFMDisplay *display) {
    if (display && display->platformData) {
        GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
        return vc.view.contentScaleFactor;
    } else {
        return [UIScreen mainScreen].scale;
    }
}

GLFMRenderingAPI glfmGetRenderingAPI(GLFMDisplay *display) {
    if (display && display->platformData) {
        GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
        if (vc.context.API == kEAGLRenderingAPIOpenGLES3) {
            return GLFMRenderingAPIOpenGLES3;
        } else {
            return GLFMRenderingAPIOpenGLES2;
        }
    } else {
        return GLFMRenderingAPIOpenGLES2;
    }
}

bool glfmHasTouch(GLFMDisplay *display) {
    // This will need to change, for say, TV apps
    return true;
}

void glfmSetMouseCursor(GLFMDisplay *display, GLFMMouseCursor mouseCursor) {
    // Do nothing
}

void glfmSetMultitouchEnabled(GLFMDisplay *display, bool multitouchEnabled) {
    if (display) {
        GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
        vc.multipleTouchEnabled = (BOOL)multitouchEnabled;
        if (vc.isViewLoaded) {
            vc.view.multipleTouchEnabled = (BOOL)multitouchEnabled;
        }
    }
}

bool glfmGetMultitouchEnabled(GLFMDisplay *display) {
    if (display) {
        GLFMViewController *vc = (__bridge GLFMViewController *)display->platformData;
        return vc.multipleTouchEnabled;
    } else {
        return false;
    }
}

const char *glfmGetLanguageInternal() {
    return [[[NSLocale autoupdatingCurrentLocale] localeIdentifier] UTF8String];
}

#endif
