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

#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <pulse/pulseaudio.h>

#ifdef NDEBUG
#  define MAL_LOG(...) do { } while(0)
#else
#  define MAL_LOG(...) do { printf("Mal: " __VA_ARGS__); printf("\n"); } while(0)
#endif

// MARK: Dynamic library loading

static void *libpulseHandle = NULL;

static typeof(pa_threaded_mainloop_new) *mal_pa_threaded_mainloop_new;
#define pa_threaded_mainloop_new mal_pa_threaded_mainloop_new
static typeof(pa_threaded_mainloop_free) *mal_pa_threaded_mainloop_free;
#define pa_threaded_mainloop_free mal_pa_threaded_mainloop_free

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

#define loadSymOrFail(handle, name) do { \
    mal_##name = _malLoadSym(handle, #name); \
    if (!mal_##name) { \
        goto fail; \
    } \
} while(0)

static bool _malLoadLibpulse() {
    if (libpulseHandle) {
        return true;
    }
    dlerror();
    void *handle = dlopen("libpulse.so.0", RTLD_NOW);
    if (dlerror() || !handle) {
        return false;
    }

    bool success = false;

    loadSymOrFail(handle, pa_threaded_mainloop_new);
    loadSymOrFail(handle, pa_threaded_mainloop_free);

    success = true;
    libpulseHandle = handle;

fail:
    return success;
}

// MARK: Implementation

struct _MalContext {
    pa_threaded_mainloop *mainloop;
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    int dummy;
};

#include "mal_audio_abstract.h"

// MARK: Context

static bool _malContextInit(MalContext *context) {
    if (!_malLoadLibpulse()) {
        return false;
    }
    context->data.mainloop = pa_threaded_mainloop_new();
    if (!context->data.mainloop) {
        return false;
    }
    return true;
}

static void _malContextDispose(MalContext *context) {
    if (context->data.mainloop) {
        pa_threaded_mainloop_free(context->data.mainloop);
        context->data.mainloop = NULL;
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
