/*
 GLFM
 Copyright (c) 2014 David Brackeen
 
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
#ifndef _GLFM_H_
#define _GLFM_H_

#define GLFM_VERSION_MAJOR    0
#define GLFM_VERSION_MINOR    7
#define GLFM_VERSION_REVISION 0

//
// Platform, which defines one of:
// GLFM_PLATFORM_IOS
// GLFM_PLATFORM_ANDROID
// GLFM_PLATFORM_EMSCRIPTEN
//

#if defined(ANDROID)
  #define GLFM_PLATFORM_ANDROID
#elif defined(__EMSCRIPTEN__)
  #define GLFM_PLATFORM_EMSCRIPTEN
  #define _POSIX_SOURCE // For fileno()
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    #define GLFM_PLATFORM_IOS
  #else
    #error Unknown Apple platform
  #endif
#else
  #error Unknown platform
#endif

#include <stddef.h> // For size_t

//
// OpenGL ES 2.0 includes
//

#ifdef __APPLE__
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
    
    // MARK: Enums
    
    typedef enum {
        GLFMColorFormatRGBA8888 = 0,
        GLFMColorFormatRGB565,
    } GLFMColorFormat;
    
    typedef enum {
        GLFMDepthFormatNone = 0,
        GLFMDepthFormat16,
        GLFMDepthFormat24,
    } GLFMDepthFormat;
    
    typedef enum {
        GLFMStencilFormatNone = 0,
        GLFMStencilFormat8,
    } GLFMStencilFormat;
    
    typedef enum {
        GLFMMultisampleNone = 0,
        GLFMMultisample4X,
    } GLFMMultisample;
    
    /// GLFMUserInterfaceChrome defines whether system UI chrome (status bar, navigation bar) is shown.
    /// This value is ignored on Emscripten.
    /// GLFMUserInterfaceChromeFullscreen
    ///  - Android 2.3: Fullscreen
    ///  - Android 4.0 - 4.3: Navigation bar dimmed
    ///  - Android 4.4: Fullscreen immersive mode
    ///  - iOS: Fullscreen
    /// GLFMUserInterfaceChromeNavigation
    ///  - Android: Show the navigation bar
    ///  - iOS: Fullscreen
    /// GLFMUserInterfaceChromeNavigationAndStatusBar:
    ///  - Android: Show the navigation bar and status bar
    ///  - iOS: Show status bar
    typedef enum {
        GLFMUserInterfaceChromeFullscreen = 0,
        GLFMUserInterfaceChromeNavigation,
        GLFMUserInterfaceChromeNavigationAndStatusBar,
    } GLFMUserInterfaceChrome;
    
    typedef enum {
        GLFMUserInterfaceOrientationAny = 0,
        GLFMUserInterfaceOrientationPortrait,
        GLFMUserInterfaceOrientationLandscape,
    } GLFMUserInterfaceOrientation;
    
    typedef enum {
        GLFMTouchPhaseHover = 0,
        GLFMTouchPhaseBegan,
        GLFMTouchPhaseMoved,
        GLFMTouchPhaseEnded,
        GLFMTouchPhaseCancelled,
    } GLFMTouchPhase;
    
    typedef enum {
        GLFMMouseCursorAuto = 0,
        GLFMMouseCursorNone,
        GLFMMouseCursorDefault,
        GLFMMouseCursorPointer,
        GLFMMouseCursorCrosshair,
        GLFMMouseCursorText
    } GLFMMouseCursor;
    
    typedef enum {
        GLFMKeyBackspace = 0x08,
        GLFMKeyTab       = 0x09,
        GLFMKeyEnter     = 0x0d,
        GLFMKeyEscape    = 0x1b,
        GLFMKeySpace     = 0x20,
        GLFMKeyLeft      = 0x25,
        GLFMKeyUp        = 0x26,
        GLFMKeyRight     = 0x27,
        GLFMKeyDown      = 0x28,
        GLFMKeyNavBack   = 0x1000,
        GLFMKeyNavMenu   = 0x1001,
    } GLFMKey;
    
    typedef enum {
        GLFMKeyModifierShift = (1 << 0),
        GLFMKeyModifierCtrl  = (1 << 1),
        GLFMKeyModifierAlt   = (1 << 2),
        GLFMKeyModifierMeta  = (1 << 3),
    } GLFMKeyModifier;
    
    typedef enum {
        GLFMKeyActionPressed = 0,
        GLFMKeyActionRepeated,
        GLFMKeyActionReleased,
    } GLFMKeyAction;

    typedef enum {
        GLFMAssetSeekSet,
        GLFMAssetSeekCur,
        GLFMAssetSeekEnd,
    } GLFMAssetSeek;
    
    // MARK: Structs and function pointers
    
    typedef struct GLFMDisplay GLFMDisplay;
    typedef struct GLFMAsset GLFMAsset;
    
    /// Main loop callback function. The frame time is in seconds, and is not related to wall time.
    typedef void (*GLFMMainLoopFunc)(GLFMDisplay *display, const double frameTime);
    
    /// Callback function for mouse or touch events. The (x,y) values are in pixels.
    /// The function should return GL_TRUE if the event was handled, and false otherwise.
    typedef GLboolean (*GLFMTouchFunc)(GLFMDisplay *display, const int touch, const GLFMTouchPhase phase,
                                       const int x, const int y);
    
    /// Callback function for key events.
    /// The function should return GL_TRUE if the event was handled, and false otherwise.
    typedef GLboolean (*GLFMKeyFunc)(GLFMDisplay *display, const GLFMKey keyCode, const GLFMKeyAction action,
                                     const int modifiers);
    
    /// Callback when the surface could not be created.
    typedef void (*GLFMSurfaceErrorFunc)(GLFMDisplay *display, const char *message);
    
    /// Callback function when the OpenGL surface is created
    typedef void (*GLFMSurfaceCreatedFunc)(GLFMDisplay *display, const int width, const int height);
    
    /// Callback function when the OpenGL surface is resized (or rotated).
    typedef void (*GLFMSurfaceResizedFunc)(GLFMDisplay *display, const int width, const int height);
    
    /// Callback function when the OpenGL surface is destroyed.
    typedef void (*GLFMSurfaceDestroyedFunc)(GLFMDisplay *display);
    
    /// Callback function when the system recieves a low memory warning.
    typedef void (*GLFMMemoryWarningFunc)(GLFMDisplay *display);
    
    typedef void (*GLFMAppPausingFunc)(GLFMDisplay *display);
    
    typedef void (*GLFMAppResumingFunc)(GLFMDisplay *display);
    
    // MARK: Functions
    
    /// Main entry point for the app, where the display can be initialized and the GLFMMainLoopFunc can be set.
    extern void glfmMain(GLFMDisplay *display);
    
    /// Init the display condifuration. Should only be called in glfmMain.
    void glfmSetDisplayConfig(GLFMDisplay *display,
                              const GLFMColorFormat colorFormat,
                              const GLFMDepthFormat depthFormat,
                              const GLFMStencilFormat stencilFormat,
                              const GLFMMultisample multisample,
                              const GLFMUserInterfaceChrome uiChrome);
    
    void glfmSetUserData(GLFMDisplay *display, void *userData);
    
    void *glfmGetUserData(GLFMDisplay *display);
    
    /// Sets the allowed user interface orientations
    void glfmSetUserInterfaceOrientation(GLFMDisplay *display, const GLFMUserInterfaceOrientation allowedOrientations);
    
    /// Returns the allowed user interface orientations
    GLFMUserInterfaceOrientation glfmGetUserInterfaceOrientation(GLFMDisplay *display);
    
    /// Sets whether multitouch input is enabled. By default, multitouch is disabled.
    void glfmSetMultitouchEnabled(GLFMDisplay *display, const GLboolean multitouchEnabled);
    
    /// Gets whether multitouch input is enabled. By default, multitouch is disabled.
    GLboolean glfmGetMultitouchEnabled(GLFMDisplay *display);
    
    /// Gets the display width, in pixels. The result will only be valid after the surface is created,
    /// or in GLFMMainLoopFunc
    int glfmGetDisplayWidth(GLFMDisplay *display);
    
    /// Gets the display height, in pixels. The result will only be valid after the surface is created,
    /// or in GLFMMainLoopFunc
    int glfmGetDisplayHeight(GLFMDisplay *display);
    
    /// Gets the display scale. On Apple devices, the value will be 1.0 for non-retina displays and 2.0 for retina.
    float glfmGetDisplayScale(GLFMDisplay *display);
    
    /// Gets whether the display has touch capabilities.
    GLboolean glfmHasTouch(GLFMDisplay *display);
    
    /// Sets the mouse cursor (only on platforms with a mouse)
    void glfmSetMouseCursor(GLFMDisplay *display, GLFMMouseCursor mouseCursor);
    
    /// Checks if a named OpenGL extension is supported
    GLboolean glfmExtensionSupported(const char *extension);
    
    /// Sets the function to call before each frame is displayed.
    void glfmSetMainLoopFunc(GLFMDisplay *display, GLFMMainLoopFunc mainLoopFunc);
    
    /// Sets the function to call when a mouse or touch event occurs.
    void glfmSetTouchFunc(GLFMDisplay *display, GLFMTouchFunc touchFunc);
    
    /// Sets the function to call when a key event occurs.
    /// Note, on iOS, only pressed events are sent (no repeated or released events) and with no modifiers.
    void glfmSetKeyFunc(GLFMDisplay *display, GLFMKeyFunc keyFunc);
    
    /// Sets the function to call when the surface could not be created.
    /// For example, the browser does not support WebGL.
    void glfmSetSurfaceErrorFunc(GLFMDisplay *display, GLFMSurfaceErrorFunc surfaceErrorFunc);
    
    void glfmSetSurfaceCreatedFunc(GLFMDisplay *display, GLFMSurfaceCreatedFunc surfaceCreatedFunc);
    
    /// Sets the function to call when the surface was resized (or rotated).
    void glfmSetSurfaceResizedFunc(GLFMDisplay *display, GLFMSurfaceResizedFunc surfaceResizedFunc);
    
    /// Sets the function to call when the surface was destroyed. For example, OpenGL context loss.
    /// All OpenGL resources should be deleted in this call.
    void glfmSetSurfaceDestroyedFunc(GLFMDisplay *display, GLFMSurfaceDestroyedFunc surfaceDestroyedFunc);
    
    void glfmSetMemoryWarningFunc(GLFMDisplay *display, GLFMMemoryWarningFunc lowMemoryFunc);
    
    void glfmSetAppPausingFunc(GLFMDisplay *display, GLFMAppPausingFunc pausingFunc);
    
    void glfmSetAppResumingFunc(GLFMDisplay *display, GLFMAppResumingFunc resumingFunc);
    
    void glfmLog(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
    
    /// Sets the preference value of the specified key.
    /// If 'value' is NULL, any existing value for the key is cleared.
    /// If 'key' is NULL, this function does nothing.
    /// Both key are value are assumed to be UTF8 encoded.
    void glfmSetPreference(const char *key, const char *value);
    
    /// Returns a newly allocated string representing the preference value of the specified key.
    /// Returns NULL if no value exists for this key, or if 'key' is NULL.
    /// If the return value is not NULL, it should be freed by the calling code.
    /// Both key are value are assumed to be UTF8 encoded.
    char *glfmGetPreference(const char *key);
    
    /// Gets the preferred user language. The return value is a static variable and should not be freed.
    /// The return value is a RFC-4646 language code. Valid examples are "en", "en-US", "zh-Hans", "zh-Hans-HK".
    /// Some systems (Safari browser) may return values in all lowercase ("en-us" instead of "en-US").
    /// This function never returns NULL. If the language cannot be determined, returns "en".
    const char *glfmGetLanguage(void);
    
    // MARK: Assets (File input)
    // NOTE: Normal file operations (fopen, fread, fseek) can't be used on regular Android assets inside the APK.
    
    /// Opens an asset (from the "bundle" on iOS, "assets" on Android). The asset must be closed with glfmAssetClose().
    /// If the asset cannot be opened (for example, the file was not found), returns NULL.
    GLFMAsset *glfmAssetOpen(const char *name);
    
    /// Gets the asset name (the original name passed to the glfmAssetOpen function).
    /// The name is freed in glfmAssetClose().
    const char *glfmAssetGetName(GLFMAsset *asset);
    
    size_t glfmAssetGetLength(GLFMAsset *asset);
    
    /// Reads 'count' bytes from the file. Returns number of bytes read.
    size_t glfmAssetRead(GLFMAsset *asset, void *buffer, size_t count);
    
    /// Sets the position of the asset.
    /// Returns 0 on success.
    int glfmAssetSeek(GLFMAsset *asset, long offset, GLFMAssetSeek whence);
    
    /// Closes the asset, releasing any resources.
    void glfmAssetClose(GLFMAsset *asset);
    
    /// Gets the asset contents as a buffer, memory-mapping if possible. The buffer is freed in glfmAssetClose().
    const void *glfmAssetGetBuffer(GLFMAsset *asset);

#ifdef __cplusplus
}
#endif

#endif
