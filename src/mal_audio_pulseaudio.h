/*
 Mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2017 David Brackeen

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MAL_AUDIO_PULSEAUDIO_H
#define MAL_AUDIO_PULSEAUDIO_H

#include <pulse/pulseaudio.h>

#ifdef NDEBUG
#  define MAL_LOG(...) do { } while(0)
#else
#  include <stdio.h>
#  define MAL_LOG(...) do { printf("Mal: " __VA_ARGS__); printf("\n"); } while(0)
#endif

// MARK: Dynamic library loading

#ifndef MAL_PULSEAUDIO_STATIC

#include <dlfcn.h>

static void *libpulseHandle = NULL;

#define FUNC_PREFIX(name) _mal_##name
#define FUNC_DECLARE(name) static __typeof(name) *FUNC_PREFIX(name)
#define FUNC_LOAD(handle, name) do { \
    _mal_##name = (__typeof(name))_malLoadSym(handle, #name); \
    if (!_mal_##name) { \
        goto fail; \
    } \
} while(0)

FUNC_DECLARE(pa_threaded_mainloop_new);
FUNC_DECLARE(pa_threaded_mainloop_free);
FUNC_DECLARE(pa_threaded_mainloop_get_api);
FUNC_DECLARE(pa_threaded_mainloop_start);
FUNC_DECLARE(pa_threaded_mainloop_stop);
FUNC_DECLARE(pa_threaded_mainloop_wait);
FUNC_DECLARE(pa_threaded_mainloop_lock);
FUNC_DECLARE(pa_threaded_mainloop_unlock);
FUNC_DECLARE(pa_threaded_mainloop_signal);
FUNC_DECLARE(pa_context_new);
FUNC_DECLARE(pa_context_unref);
FUNC_DECLARE(pa_context_connect);
FUNC_DECLARE(pa_context_disconnect);
FUNC_DECLARE(pa_context_set_state_callback);
FUNC_DECLARE(pa_context_get_state);

#define pa_threaded_mainloop_new FUNC_PREFIX(pa_threaded_mainloop_new)
#define pa_threaded_mainloop_free FUNC_PREFIX(pa_threaded_mainloop_free)
#define pa_threaded_mainloop_get_api FUNC_PREFIX(pa_threaded_mainloop_get_api)
#define pa_threaded_mainloop_start FUNC_PREFIX(pa_threaded_mainloop_start)
#define pa_threaded_mainloop_stop FUNC_PREFIX(pa_threaded_mainloop_stop)
#define pa_threaded_mainloop_wait FUNC_PREFIX(pa_threaded_mainloop_wait)
#define pa_threaded_mainloop_lock FUNC_PREFIX(pa_threaded_mainloop_lock)
#define pa_threaded_mainloop_unlock FUNC_PREFIX(pa_threaded_mainloop_unlock)
#define pa_threaded_mainloop_signal FUNC_PREFIX(pa_threaded_mainloop_signal)
#define pa_context_new FUNC_PREFIX(pa_context_new)
#define pa_context_unref FUNC_PREFIX(pa_context_unref)
#define pa_context_connect FUNC_PREFIX(pa_context_connect)
#define pa_context_disconnect FUNC_PREFIX(pa_context_disconnect)
#define pa_context_set_state_callback FUNC_PREFIX(pa_context_set_state_callback)
#define pa_context_get_state FUNC_PREFIX(pa_context_get_state)

static void *_malLoadSym(void *handle, const char *name) {
    dlerror();
    void *sym = dlsym(handle, name);
    if (dlerror() || !sym) {
        MAL_LOG("Couldn't load symbol: %s", name);
        return NULL;
    } else {
        return sym;
    }
}

static int _malLoadLibpulse() {
    if (libpulseHandle) {
        return PA_OK;
    }
    dlerror();
#if defined(__APPLE__)
    void *handle = dlopen("libpulse.0.dylib", RTLD_NOW);
#else
    void *handle = dlopen("libpulse.so.0", RTLD_NOW);
#endif
    if (dlerror() || !handle) {
        return PA_ERR_MODINITFAILED;
    }

    FUNC_LOAD(handle, pa_threaded_mainloop_new);
    FUNC_LOAD(handle, pa_threaded_mainloop_free);
    FUNC_LOAD(handle, pa_threaded_mainloop_get_api);
    FUNC_LOAD(handle, pa_threaded_mainloop_start);
    FUNC_LOAD(handle, pa_threaded_mainloop_stop);
    FUNC_LOAD(handle, pa_threaded_mainloop_wait);
    FUNC_LOAD(handle, pa_threaded_mainloop_lock);
    FUNC_LOAD(handle, pa_threaded_mainloop_unlock);
    FUNC_LOAD(handle, pa_threaded_mainloop_signal);
    FUNC_LOAD(handle, pa_context_new);
    FUNC_LOAD(handle, pa_context_unref);
    FUNC_LOAD(handle, pa_context_connect);
    FUNC_LOAD(handle, pa_context_disconnect);
    FUNC_LOAD(handle, pa_context_set_state_callback);
    FUNC_LOAD(handle, pa_context_get_state);

    libpulseHandle = handle;
    return PA_OK;

fail:
    return PA_ERR_MODINITFAILED;
}

#endif // MAL_PULSEAUDIO_STATIC

// MARK: Implementation

struct _MalContext {
    pa_threaded_mainloop *mainloop;
    pa_context *context;
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    int dummy;
};

#include "mal_audio_abstract.h"

// MARK: Context

static void _malPulseAudioContextStateCallback(pa_context *context, void *userdata) {
    pa_threaded_mainloop *mainloop = userdata;
    pa_threaded_mainloop_signal(mainloop, 0);
}

static bool _malContextInit(MalContext *context, void *androidActivity,
                            const char **errorMissingAudioSystem) {
    (void)androidActivity;
    struct _MalContext *pa = &context->data;

#ifndef MAL_PULSEAUDIO_STATIC
    // Load libpulse library
    if (_malLoadLibpulse() != PA_OK) {
        if (errorMissingAudioSystem) {
            *errorMissingAudioSystem = "PulseAudio";
        }
        return false;
    }
#endif

    // Create and start mainloop
    pa->mainloop = pa_threaded_mainloop_new();
    if (!pa->mainloop || pa_threaded_mainloop_start(pa->mainloop) != PA_OK) {
        goto fail;
    }

    // Create context
    pa_mainloop_api *mainloop_api = pa_threaded_mainloop_get_api(pa->mainloop);
    pa->context = pa_context_new(mainloop_api, NULL);
    if (!pa->context) {
        goto fail;
    }

    // Connect context and wait for PA_CONTEXT_READY state
    pa_context_state_t state = PA_CONTEXT_UNCONNECTED;
    pa_threaded_mainloop_lock(pa->mainloop);
    pa_context_set_state_callback(pa->context, _malPulseAudioContextStateCallback, pa->mainloop);
    if (pa_context_connect(pa->context, NULL, PA_CONTEXT_NOFLAGS, NULL) == PA_OK) {
        while (1) {
            state = pa_context_get_state(pa->context);
            if (state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state)) {
                break;
            }
            pa_threaded_mainloop_wait(pa->mainloop);
        }
    }
    pa_context_set_state_callback(pa->context, NULL, NULL);
    pa_threaded_mainloop_unlock(pa->mainloop);
    if (state != PA_CONTEXT_READY) {
        goto fail;
    }

    return true;

fail:
    _malContextDispose(context);
    return false;
}

static void _malContextDispose(MalContext *context) {
    struct _MalContext *pa = &context->data;

    if (pa->mainloop) {
        pa_threaded_mainloop_stop(pa->mainloop);
    }
    if (pa->context) {
        pa_context_disconnect(pa->context);
        pa_context_unref(pa->context);
        pa->context = NULL;
    }
    if (pa->mainloop) {
        pa_threaded_mainloop_free(pa->mainloop);
        pa->mainloop = NULL;
    }
}

static bool _malContextSetActive(MalContext *context, bool active) {
    (void)context;
    (void)active;
    return true;
}

static void _malContextSetMute(MalContext *context, bool mute) {
    (void)context;
    (void)mute;
}

static void _malContextSetGain(MalContext *context, float gain) {
    (void)context;
    (void)gain;
}

// MARK: Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           malDeallocatorFunc dataDeallocator) {
    (void)context;
    (void)buffer;
    (void)copiedData;
    (void)managedData;
    (void)dataDeallocator;
    return false;
}

static void _malBufferDispose(MalBuffer *buffer) {
    (void)buffer;
}

// MARK: Player

static bool _malPlayerInit(MalPlayer *player) {
    (void)player;
    return false;
}

static void _malPlayerDispose(MalPlayer *player) {
    (void)player;
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    (void)player;
    (void)format;
    return false;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    (void)player;
    (void)buffer;
    return false;
}

static void _malPlayerSetMute(MalPlayer *player, bool mute) {
    (void)player;
    (void)mute;
}

static void _malPlayerSetGain(MalPlayer *player, float gain) {
    (void)player;
    (void)gain;
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
    (void)player;
    (void)looping;
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    (void)player;
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    (void)player;
    return MAL_PLAYER_STATE_STOPPED;
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state) {
    (void)player;
    (void)oldState;
    (void)state;
    return false;
}

#endif
