
#include "glfm.h"
#include "mal.h"
#include "ok_wav.h"
#include <stdio.h> // For SEEK_CUR
#include <stdlib.h>

#define kMaxPlayers 16
#define kTestFreeBufferDuringPlayback 0
#define kTestAudioPause 0

typedef struct {
    MalContext *context;
    MalBuffer *buffer;
    MalPlayer *players[kMaxPlayers];
} MalApp;

static void onFinished(void *userData, MalPlayer *player) {
    MalApp *app = userData;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (player == app->players[i]) {
            glfmLog("FINISHED %i", i);
        }
    }
}

static void playSound(MalApp *app, MalBuffer *buffer, float gain) {
#if kTestFreeBufferDuringPlayback
    // This is useful to test buffer freeing during playback
    if (app->buffer) {
        malBufferFree(app->buffer);
        app->buffer = NULL;
    }
#elif kTestAudioPause
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
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i] && malPlayerGetState(app->players[i]) == MAL_PLAYER_STATE_STOPPED) {
            malPlayerSetBuffer(app->players[i], buffer);
            malPlayerSetGain(app->players[i], gain);
            malPlayerSetFinishedFunc(app->players[i], onFinished, app);
            malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
            glfmLog("PLAY %i gain=%.2f", i, gain);
            break;
        }
    }
#endif
}

static void malInit(MalApp *app, ok_wav *wav) {
    app->context = malContextCreate(44100);
    if (!app->context) {
        glfmLog("Error: Couldn't create audio context");
    }
    MalFormat format = {
        .sampleRate = wav->sample_rate,
        .numChannels = wav->num_channels,
        .bitDepth = wav->bit_depth};
    if (!malContextIsFormatValid(app->context, format)) {
        glfmLog("Error: Audio format is invalid");
    }
    app->buffer = malBufferCreateNoCopy(app->context, format, (uint32_t)wav->num_frames,
                                        wav->data, free);
    if (!app->buffer) {
        glfmLog("Error: Couldn't create audio buffer");
    }
    wav->data = NULL; // Audio buffer is now managed by mal, don't free it
    ok_wav_free(wav);

    for (int i = 0; i < kMaxPlayers; i++) {
        app->players[i] = malPlayerCreate(app->context, format);
    }
    bool success = malPlayerSetBuffer(app->players[0], app->buffer);
    if (!success) {
        glfmLog("Error: Couldn't attach buffer to audio player");
    }
    malPlayerSetGain(app->players[0], 0.25f);
    malPlayerSetLooping(app->players[0], true);
    success = malPlayerSetState(app->players[0], MAL_PLAYER_STATE_PLAYING);
    if (!success) {
        glfmLog("Error: Couldn't play audio");
    }
}

// Be a good app citizen - set mal to inactive when pausing.
static void onAppPause(GLFMDisplay *display) {
    MalApp *app = glfmGetUserData(display);
    malContextSetActive(app->context, false);
}

static void onAppResume(GLFMDisplay *display) {
    MalApp *app = glfmGetUserData(display);
    malContextSetActive(app->context, true);
}

// GLFM functions

static size_t glfmAssetRead2(void *asset, uint8_t *buffer, size_t count) {
    return glfmAssetRead((GLFMAsset *)asset, buffer, count);
}

static bool glfmAssetSeek2(void *asset, long offset) {
    return glfmAssetSeek((GLFMAsset *)asset, offset, SEEK_CUR) == 0;
}

static GLboolean onTouch(GLFMDisplay *display, const int touch, const GLFMTouchPhase phase,
                         const int x, const int y) {
    if (phase == GLFMTouchPhaseBegan) {
        int height = glfmGetDisplayHeight(display);
        MalApp *app = glfmGetUserData(display);
        playSound(app, app->buffer, 0.05f + 0.60f * (height - y) / height);
    }
    return GL_TRUE;
}

static void onSurfaceCreated(GLFMDisplay *display, const int width, const int height) {
    glViewport(0, 0, width, height);
}

static void onFrame(GLFMDisplay *display, const double frameTime) {
    glClearColor(0.6f, 0.0f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void glfmMain(GLFMDisplay *display) {
    MalApp *app = calloc(1, sizeof(MalApp));

    glfmSetDisplayConfig(display,
                         GLFMColorFormatRGBA8888,
                         GLFMDepthFormatNone,
                         GLFMStencilFormatNone,
                         GLFMMultisampleNone,
                         GLFMUserInterfaceChromeNavigation);
    glfmSetUserData(display, app);
    glfmSetSurfaceCreatedFunc(display, onSurfaceCreated);
    glfmSetSurfaceResizedFunc(display, onSurfaceCreated);
    glfmSetMainLoopFunc(display, onFrame);
    glfmSetTouchFunc(display, onTouch);
    glfmSetAppPausingFunc(display, onAppPause);
    glfmSetAppResumingFunc(display, onAppResume);

    GLFMAsset *asset = glfmAssetOpen("sound.wav");
    ok_wav *wav = ok_wav_read_from_callbacks(asset, glfmAssetRead2, glfmAssetSeek2, true);
    glfmAssetClose(asset);

    if (!wav->data) {
        glfmLog("Error: %s", wav->error_message);
    } else {
        malInit(app, wav);
    }
}
