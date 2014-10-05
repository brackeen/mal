
#include "glfm.h"
#include <stdlib.h>
#include "ok_wav.h"
#include "mal.h"

static const int MAX_SOURCES = 16;
typedef struct {
    mal_context *context;
    mal_buffer *buffer;
    mal_source *sources[MAX_SOURCES];
} mal_app;

static void play_sound(mal_app *app, mal_buffer *buffer) {
    // This is useful to test buffer freeing during playback
//    if (app->buffer != NULL) {
//        mal_buffer_free(app->buffer);
//        app->buffer = NULL;
//    }
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (app->sources[i] != NULL && mal_source_get_state(app->sources[i]) == MAL_SOURCE_STATE_STOPPED) {
            mal_source_set_buffer(app->sources[i], buffer);
            mal_source_set_gain(app->sources[i], 0.25f);
            mal_source_set_state(app->sources[i], MAL_SOURCE_STATE_PLAYING);
            printf("PLAY %i\n", i);
            break;
        }
    }
}

// TODO: Move this somewhere
static size_t read_func(void *user_data, uint8_t *buffer, const size_t count) {
    GLFMAsset *asset = user_data;
    return glfmAssetRead(asset, buffer, count);
}

static int seek_func(void *user_data, const int count) {
    GLFMAsset *asset = user_data;
    return glfmAssetSeek(asset, count, SEEK_CUR);
}


static void onFrame(GLFMDisplay *display, const double frameTime);
static void onSurfaceCreated(GLFMDisplay *display, const int width, const int height);
static GLboolean onTouch(GLFMDisplay *display, const int touch, const GLFMTouchPhase phase, const int x, const int y);

// Main entry point
void glfm_main(GLFMDisplay *display) {
    mal_app *app = calloc(1, sizeof(mal_app));

    glfmSetDisplayConfig(display,
                         GLFMColorFormatRGBA8888,
                         GLFMDepthFormatNone,
                         GLFMStencilFormatNone,
                         GLFMUserInterfaceChromeFullscreen);
    glfmSetUserData(display, app);
    glfmSetSurfaceCreatedFunc(display, onSurfaceCreated);
    glfmSetSurfaceResizedFunc(display, onSurfaceCreated);
    glfmSetMainLoopFunc(display, onFrame);
    glfmSetTouchFunc(display, onTouch);
    
    
    GLFMAsset *asset = glfmAssetOpen("sound.wav");
    ok_audio *audio = ok_wav_read_from_callbacks(asset, read_func, seek_func, true);
    glfmAssetClose(asset);

    if (audio->data == NULL) {
        printf("Error: %s\n", audio->error_message);
    }
    else {
        app->context = mal_context_create(44100);
        if (app->context == NULL) {
            printf("Couldn't create audio context\n");
        }
        mal_format format = {
            .sample_rate = audio->sample_rate,
            .num_channels = audio->num_channels,
            .bit_depth = audio->bit_depth
        };
        if (!mal_context_format_is_valid(app->context, format)) {
            printf("Audio format is invalid\n");
        }
        app->buffer = mal_buffer_create_no_copy(app->context, format, (uint32_t)audio->num_frames, audio->data, free);
        if (app->buffer == NULL) {
            printf("Couldn't create audio buffer\n");
        }
        audio->data = NULL; // Audio buffer is now managed by mal, don't free it
        ok_audio_free(audio);
        
        for (int i = 0; i < MAX_SOURCES; i++) {
            app->sources[i] = mal_source_create(app->context, format);
            //mal_source_set_gain(app->sources[i], 0.25f);
        }
        const mal_buffer *buffers[3] = { app->buffer, app->buffer, app->buffer };
        //bool success = mal_source_set_buffer(source, buffer);
        bool success = mal_source_set_buffer_sequence(app->sources[0], 3, buffers);
        if (!success) {
            printf("Couldn't attach buffer to audio source\n");
        }
        mal_source_set_gain(app->sources[0], 0.25f);
        success = mal_source_set_state(app->sources[0], MAL_SOURCE_STATE_PLAYING);
        if (!success) {
            printf("Couldn't play audio\n");
        }
    }
}

static GLboolean onTouch(GLFMDisplay *display, const int touch, const GLFMTouchPhase phase, const int x, const int y) {
    if (phase == GLFMTouchPhaseBegan) {
        mal_app *app = glfmGetUserData(display);
        play_sound(app, app->buffer);
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