#if defined(MAL_EXAMPLE_WITH_GLFM)
#include "glfm.h"
#elif defined(MAL_EXAMPLE_WITH_GLFW)
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#else
#error Mal example has no window layer
#endif

#include "mal.h"
#include "ok_wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FILE_COMPAT_ANDROID_ACTIVITY glfmAndroidGetActivity()
#include "file_compat.h"

#define kMaxPlayers 16
#define kTestAudioPause 0

typedef struct {
    MalContext *context;
    MalBuffer *buffer[2];
    MalPlayer *players[kMaxPlayers];
} ExampleApp;

static void onFinished(MalPlayer *player, void *userData) {
    ExampleApp *app = userData;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (player == app->players[i]) {
            printf("FINISHED player=%i\n", i);
        }
    }
}

static void playSound(ExampleApp *app, MalBuffer *buffer, float gain) {
#if kTestAudioPause
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i]) {
            MalPlayerState state = malPlayerGetState(app->players[i]);
            if (state == MAL_PLAYER_STATE_PLAYING) {
                malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PAUSED);
            } else if (state == MAL_PLAYER_STATE_PAUSED) {
                malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
            }
        }
    }
#else
    // Stop any looping players
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i] && malPlayerGetState(app->players[i]) == MAL_PLAYER_STATE_PLAYING &&
            malPlayerIsLooping(app->players[i])) {
            malPlayerSetLooping(app->players[i], false);
        }
    }
    // Play new sound
    MalFormat format = malBufferGetFormat(buffer);
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i] && malPlayerGetState(app->players[i]) == MAL_PLAYER_STATE_STOPPED &&
            malContextIsFormatEqual(app->context, malPlayerGetFormat(app->players[i]), format)) {
            
            malPlayerSetBuffer(app->players[i], buffer);
            malPlayerSetGain(app->players[i], gain);
            malPlayerSetFinishedFunc(app->players[i], onFinished, app);
            malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
            printf("PLAY player=%i gain=%.2f\n", i, gain);
            break;
        }
    }
#endif
}

static bool malExampleInit(ExampleApp *app) {
    void *androidActivity = NULL;
#if defined(MAL_EXAMPLE_WITH_GLFM) && defined(__ANDROID__)
    androidActivity = glfmAndroidGetActivity();
#endif
    app->context = malContextCreateWithOptions(MAL_DEFAULT_SAMPLE_RATE, androidActivity, NULL);
    if (!app->context) {
        printf("Error: Couldn't create audio context\n");
        return false;
    }

    char *sound_files[2] = { "sound-22k-mono.wav", "sound-44k-stereo.wav" };
    for (int i = 0; i < 2; i++) {
        char sound_path[PATH_MAX];
        fc_resdir(sound_path, PATH_MAX);
        strncat(sound_path, sound_files[i], sizeof(sound_path) - strlen(sound_path) - 1);

        FILE *file = fopen(sound_path, "rb");
        ok_wav *wav = ok_wav_read(file, true);
        fclose(file);

        if (!wav->data) {
            printf("Error: %s\n", wav->error_message);
            ok_wav_free(wav);
            return false;
        }

        MalFormat format = {
            .sampleRate = wav->sample_rate,
            .numChannels = wav->num_channels,
            .bitDepth = wav->bit_depth
        };
        if (!malContextIsFormatValid(app->context, format)) {
            printf("Error: Audio format is invalid\n");
            ok_wav_free(wav);
            return false;
        }
        app->buffer[i] = malBufferCreateNoCopy(app->context, format, (uint32_t)wav->num_frames,
                                               wav->data, free);

        wav->data = NULL; // Audio buffer is now managed by mal, don't free it
        ok_wav_free(wav);
        if (!app->buffer[i]) {
            printf("Error: Couldn't create audio buffer\n");
            return false;
        }
    }

    for (int i = 0; i < kMaxPlayers; i++) {
        // Half have the format of buffer 0, the other half have the format of buffer 1
        app->players[i] = malPlayerCreate(app->context, malBufferGetFormat(app->buffer[i & 1]));
    }
    bool success = malPlayerSetBuffer(app->players[0], app->buffer[0]);
    if (!success) {
        printf("Error: Couldn't attach buffer to audio player\n");
        return false;
    }
    malPlayerSetGain(app->players[0], 0.25f);
    malPlayerSetLooping(app->players[0], true);
    success = malPlayerSetState(app->players[0], MAL_PLAYER_STATE_PLAYING);
    if (!success) {
        printf("Error: Couldn't play audio\n");
        return false;
    }
    return true;
}

static void malExampleFree(ExampleApp *app) {
    malBufferRelease(app->buffer[0]);
    malBufferRelease(app->buffer[1]);
    for (int i = 0; i < kMaxPlayers; i++) {
        malPlayerRelease(app->players[i]);
    }
    malContextRelease(app->context);
    free(app);
}

#if defined(MAL_EXAMPLE_WITH_GLFM)

// MARK: GLFM functions

static void onFocusChange(GLFMDisplay *display, bool focused) {
    ExampleApp *app = glfmGetUserData(display);
    malContextSetActive(app->context, focused);
}

static bool onTouch(GLFMDisplay *display, int touch, GLFMTouchPhase phase, double x, double y) {
    if (phase == GLFMTouchPhaseBegan) {
        int width, height;
        glfmGetDisplaySize(display, &width, &height);
        ExampleApp *app = glfmGetUserData(display);
        int index = x < width / 2 ? 0 : 1;
        playSound(app, app->buffer[index], 0.05f + 0.60f * (height - (float)y) / height);
    }
    return true;
}

static void onSurfaceCreated(GLFMDisplay *display, int width, int height) {
    glViewport(0, 0, width, height);
}

static void onFrame(GLFMDisplay *display, double frameTime) {
    ExampleApp *app = glfmGetUserData(display);
    malContextPollEvents(app->context);

    glClearColor(0.6f, 0.0f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void glfmMain(GLFMDisplay *display) {
    ExampleApp *app = calloc(1, sizeof(ExampleApp));

    bool success = malExampleInit(app);
    if (!success) {
        return;
    }

    glfmSetDisplayConfig(display,
                         GLFMRenderingAPIOpenGLES2,
                         GLFMColorFormatRGBA8888,
                         GLFMDepthFormatNone,
                         GLFMStencilFormatNone,
                         GLFMMultisampleNone);
    glfmSetUserData(display, app);
    glfmSetSurfaceCreatedFunc(display, onSurfaceCreated);
    glfmSetSurfaceResizedFunc(display, onSurfaceCreated);
    glfmSetMainLoopFunc(display, onFrame);
    glfmSetTouchFunc(display, onTouch);
    glfmSetAppFocusFunc(display, onFocusChange);
}

#elif defined(MAL_EXAMPLE_WITH_GLFW)

static void onError(int error, const char *description) {
    printf("Error: %s\n", description);
}

static void onFocusChange(GLFWwindow *window, int focused) {
    ExampleApp *app = glfwGetWindowUserPointer(window);
    malContextSetActive(app->context, focused == GLFW_TRUE);
}

static void onMouseClick(GLFWwindow *window, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        int viewWidth;
        int viewHeight;
        glfwGetWindowSize(window, &viewWidth, &viewHeight);
        double x, y;
        glfwGetCursorPos(window, &x, &y);

        ExampleApp *app = glfwGetWindowUserPointer(window);
        int index = x < viewWidth / 2 ? 0 : 1;
        playSound(app, app->buffer[index], 0.05f + 0.60f * (viewHeight - (float)y) / viewHeight);
    }
}

int main(void) {
    GLFWwindow *window;
    glfwSetErrorCallback(onError);
    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window = glfwCreateWindow(640, 480, "Mal Example", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    ExampleApp *app = calloc(1, sizeof(ExampleApp));
    malExampleInit(app);
    glfwSetWindowUserPointer(window, app);
    glfwSetWindowFocusCallback(window, onFocusChange);
    glfwSetMouseButtonCallback(window, onMouseClick);

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.6f, 0.0f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);

        if (glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_FALSE) {
            glfwWaitEvents();
        } else {
            glfwPollEvents();
        }
        malContextPollEvents(app->context);
    }
    glfwDestroyWindow(window);
    glfwTerminate();

    malExampleFree(app);
    exit(EXIT_SUCCESS);
}

#endif
