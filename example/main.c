
#include "glfm.h"
#include <stdlib.h>
#include "ok_wav.h"
#include "mal.h"

#define kMaxPlayers 16

typedef struct {
    mal_context *context;
    mal_buffer *buffer;
    mal_player *players[kMaxPlayers];
} mal_app;

static void play_sound(mal_app *app, mal_buffer *buffer) {
    // This is useful to test buffer freeing during playback
//    if (app->buffer != NULL) {
//        mal_buffer_free(app->buffer);
//        app->buffer = NULL;
//    }
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i] != NULL && mal_player_get_state(app->players[i]) == MAL_PLAYER_STATE_STOPPED) {
            mal_player_set_buffer(app->players[i], buffer);
            mal_player_set_gain(app->players[i], 0.25f);
            mal_player_set_state(app->players[i], MAL_PLAYER_STATE_PLAYING);
            glfmLog(GLFMLogLevelInfo, "PLAY %i", i);
            break;
        }
    }
}

static void mal_init(mal_app *app, ok_audio *audio) {
    app->context = mal_context_create(44100);
    if (app->context == NULL) {
        glfmLog(GLFMLogLevelError, "Couldn't create audio context");
    }
    mal_format format = {
        .sample_rate = audio->sample_rate,
        .num_channels = audio->num_channels,
        .bit_depth = audio->bit_depth
    };
    if (!mal_context_format_is_valid(app->context, format)) {
        glfmLog(GLFMLogLevelError, "Audio format is invalid");
    }
    app->buffer = mal_buffer_create_no_copy(app->context, format, (uint32_t)audio->num_frames, audio->data, free);
    if (app->buffer == NULL) {
        glfmLog(GLFMLogLevelError, "Couldn't create audio buffer");
    }
    audio->data = NULL; // Audio buffer is now managed by mal, don't free it
    ok_audio_free(audio);
    
    for (int i = 0; i < kMaxPlayers; i++) {
        app->players[i] = mal_player_create(app->context, format);
    }
    const mal_buffer *buffers[3] = { app->buffer, app->buffer, app->buffer };
    bool success = mal_player_set_buffer_sequence(app->players[0], 3, buffers);
    if (!success) {
        glfmLog(GLFMLogLevelError, "Couldn't attach buffer to audio player");
    }
    mal_player_set_gain(app->players[0], 0.25f);
    success = mal_player_set_state(app->players[0], MAL_PLAYER_STATE_PLAYING);
    if (!success) {
        glfmLog(GLFMLogLevelError, "Couldn't play audio");
    }
}

// Be a good app citizen - set mal to inactive when pausing.
static void on_app_pause(GLFMDisplay *display) {
    mal_app *app = glfmGetUserData(display);
    mal_context_set_active(app->context, false);
}

static void on_app_resume(GLFMDisplay *display) {
    mal_app *app = glfmGetUserData(display);
    mal_context_set_active(app->context, true);
}

// GLFM functions

static size_t read_func(void *user_data, uint8_t *buffer, const size_t count) {
    GLFMAsset *asset = user_data;
    return glfmAssetRead(asset, buffer, count);
}

static int seek_func(void *user_data, const int count) {
    GLFMAsset *asset = user_data;
    return glfmAssetSeek(asset, count, SEEK_CUR);
}

static GLboolean on_touch(GLFMDisplay *display, const int touch, const GLFMTouchPhase phase, const int x, const int y) {
    if (phase == GLFMTouchPhaseBegan) {
        mal_app *app = glfmGetUserData(display);
        play_sound(app, app->buffer);
    }
    return GL_TRUE;
}

static void on_surface_created(GLFMDisplay *display, const int width, const int height) {
    glViewport(0, 0, width, height);
}

static void on_frame(GLFMDisplay *display, const double frameTime) {
    glClearColor(0.6f, 0.0f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void glfm_main(GLFMDisplay *display) {
    mal_app *app = calloc(1, sizeof(mal_app));

    glfmSetDisplayConfig(display,
                         GLFMColorFormatRGBA8888,
                         GLFMDepthFormatNone,
                         GLFMStencilFormatNone,
                         GLFMUserInterfaceChromeNavigation);
    glfmSetUserData(display, app);
    glfmSetSurfaceCreatedFunc(display, on_surface_created);
    glfmSetSurfaceResizedFunc(display, on_surface_created);
    glfmSetMainLoopFunc(display, on_frame);
    glfmSetTouchFunc(display, on_touch);
    glfmSetAppPausingFunc(display, on_app_pause);
    glfmSetAppResumingFunc(display, on_app_resume);
    
    GLFMAsset *asset = glfmAssetOpen("sound.wav");
    ok_audio *audio = ok_wav_read_from_callbacks(asset, read_func, seek_func, true);
    glfmAssetClose(asset);
    
    if (audio->data == NULL) {
        glfmLog(GLFMLogLevelError, "Error: %s", audio->error_message);
    }
    else {
        mal_init(app, audio);
    }
}
