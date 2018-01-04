/*
 Mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2018 David Brackeen

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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include <pulse/pulseaudio.h>
#pragma clang diagnostic pop

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
FUNC_DECLARE(pa_context_get_state);
FUNC_DECLARE(pa_context_get_server_info);
FUNC_DECLARE(pa_context_set_sink_input_mute);
FUNC_DECLARE(pa_context_set_sink_input_volume);
FUNC_DECLARE(pa_context_set_state_callback);
FUNC_DECLARE(pa_operation_get_state);
FUNC_DECLARE(pa_operation_unref);
FUNC_DECLARE(pa_stream_new);
FUNC_DECLARE(pa_stream_get_state);
FUNC_DECLARE(pa_stream_get_sample_spec);
FUNC_DECLARE(pa_stream_get_index);
FUNC_DECLARE(pa_stream_set_write_callback);
FUNC_DECLARE(pa_stream_set_state_callback);
FUNC_DECLARE(pa_stream_update_sample_rate);
FUNC_DECLARE(pa_stream_connect_playback);
FUNC_DECLARE(pa_stream_begin_write);
FUNC_DECLARE(pa_stream_write);
FUNC_DECLARE(pa_stream_cork);
FUNC_DECLARE(pa_stream_set_underflow_callback);
FUNC_DECLARE(pa_stream_disconnect);
FUNC_DECLARE(pa_stream_unref);
FUNC_DECLARE(pa_channel_map_init_auto);
FUNC_DECLARE(pa_sw_volume_from_linear);

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
#define pa_context_get_state FUNC_PREFIX(pa_context_get_state)
#define pa_context_get_server_info FUNC_PREFIX(pa_context_get_server_info)
#define pa_context_set_sink_input_mute FUNC_PREFIX(pa_context_set_sink_input_mute)
#define pa_context_set_sink_input_volume FUNC_PREFIX(pa_context_set_sink_input_volume)
#define pa_context_set_state_callback FUNC_PREFIX(pa_context_set_state_callback)
#define pa_operation_get_state FUNC_PREFIX(pa_operation_get_state)
#define pa_operation_unref FUNC_PREFIX(pa_operation_unref)
#define pa_stream_new FUNC_PREFIX(pa_stream_new)
#define pa_stream_get_state FUNC_PREFIX(pa_stream_get_state)
#define pa_stream_get_sample_spec FUNC_PREFIX(pa_stream_get_sample_spec)
#define pa_stream_get_index FUNC_PREFIX(pa_stream_get_index)
#define pa_stream_set_write_callback FUNC_PREFIX(pa_stream_set_write_callback)
#define pa_stream_set_state_callback FUNC_PREFIX(pa_stream_set_state_callback)
#define pa_stream_update_sample_rate FUNC_PREFIX(pa_stream_update_sample_rate)
#define pa_stream_connect_playback FUNC_PREFIX(pa_stream_connect_playback)
#define pa_stream_begin_write FUNC_PREFIX(pa_stream_begin_write)
#define pa_stream_write FUNC_PREFIX(pa_stream_write)
#define pa_stream_cork FUNC_PREFIX(pa_stream_cork)
#define pa_stream_set_underflow_callback FUNC_PREFIX(pa_stream_set_underflow_callback)
#define pa_stream_disconnect FUNC_PREFIX(pa_stream_disconnect)
#define pa_stream_unref FUNC_PREFIX(pa_stream_unref)
#define pa_channel_map_init_auto FUNC_PREFIX(pa_channel_map_init_auto)
#define pa_sw_volume_from_linear FUNC_PREFIX(pa_sw_volume_from_linear)

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
        return PA_ERR_ACCESS;
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
    FUNC_LOAD(handle, pa_context_get_state);
    FUNC_LOAD(handle, pa_context_get_server_info);
    FUNC_LOAD(handle, pa_context_set_sink_input_mute);
    FUNC_LOAD(handle, pa_context_set_sink_input_volume);
    FUNC_LOAD(handle, pa_context_set_state_callback);
    FUNC_LOAD(handle, pa_operation_get_state);
    FUNC_LOAD(handle, pa_operation_unref);
    FUNC_LOAD(handle, pa_stream_new);
    FUNC_LOAD(handle, pa_stream_get_state);
    FUNC_LOAD(handle, pa_stream_get_sample_spec);
    FUNC_LOAD(handle, pa_stream_get_index);
    FUNC_LOAD(handle, pa_stream_set_write_callback);
    FUNC_LOAD(handle, pa_stream_set_state_callback);
    FUNC_LOAD(handle, pa_stream_update_sample_rate);
    FUNC_LOAD(handle, pa_stream_connect_playback);
    FUNC_LOAD(handle, pa_stream_begin_write);
    FUNC_LOAD(handle, pa_stream_write);
    FUNC_LOAD(handle, pa_stream_cork);
    FUNC_LOAD(handle, pa_stream_set_underflow_callback);
    FUNC_LOAD(handle, pa_stream_disconnect);
    FUNC_LOAD(handle, pa_stream_unref);
    FUNC_LOAD(handle, pa_channel_map_init_auto);
    FUNC_LOAD(handle, pa_sw_volume_from_linear);

    libpulseHandle = handle;
    return PA_OK;

fail:
    return PA_ERR_NOTIMPLEMENTED;
}

#endif // MAL_PULSEAUDIO_STATIC

// MARK: Implementation

#include "ok_lib.h"
#include "mal_audio_abstract_types.h"
#include "mal.h"

typedef enum {
    MAL_STREAM_INACTIVE = 0,
    MAL_STREAM_STARTING,
    MAL_STREAM_PLAYING,
    MAL_STREAM_DRAINING,
} MalStreamState;

struct _MalContext {
    pa_threaded_mainloop *mainloop;
    pa_context *context;

    _Atomic(bool) hasPolledEvents;
    struct ok_queue_of(MalCallbackId) finishedCallbackIds;
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    pa_stream *stream;
    
    uint32_t nextFrame;
    MalPlayerState state;
    MalStreamState streamState;
};

#define MAL_USE_MUTEX
#include "mal_audio_abstract.h"

// MARK: Context

static void _malPulseAudioOperationWait(pa_threaded_mainloop *mainloop, pa_operation *operation) {
    while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(mainloop);
    }
    pa_operation_unref(operation);
}

static void _malPulseAudioContextStateCallback(pa_context *context, void *userData) {
    pa_threaded_mainloop *mainloop = userData;
    pa_threaded_mainloop_signal(mainloop, 0);
}

static void _malPulseAudioServerInfoCallback(pa_context *c, const pa_server_info *info,
                                             void *userData) {
    struct MalContext *context = userData;
    if (info) {
        context->actualSampleRate = info->sample_spec.rate;
    }
    pa_threaded_mainloop_signal(context->data.mainloop, 0);
}

static bool _malContextInit(MalContext *context, void *androidActivity,
                            const char **errorMissingAudioSystem) {
    (void)androidActivity;
    struct _MalContext *pa = &context->data;

    ok_queue_init(&pa->finishedCallbackIds);

#ifndef MAL_PULSEAUDIO_STATIC
    // Load libpulse library
    if (_malLoadLibpulse() != PA_OK) {
        if (errorMissingAudioSystem) {
            *errorMissingAudioSystem = "PulseAudio";
        }
        return false;
    }
#endif

    // Create mainloop
    pa->mainloop = pa_threaded_mainloop_new();
    if (!pa->mainloop) {
        goto fail;
    }

    // Create context
    pa->context = pa_context_new(pa_threaded_mainloop_get_api(pa->mainloop), NULL);
    if (!pa->context) {
        goto fail;
    }

    // Connect context and wait for PA_CONTEXT_READY state
    pa_context_state_t state = PA_CONTEXT_UNCONNECTED;
    pa_threaded_mainloop_lock(pa->mainloop);
    if (pa_threaded_mainloop_start(pa->mainloop) != PA_OK) {
        goto unlock_and_fail;
    }
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
    if (state != PA_CONTEXT_READY) {
        goto unlock_and_fail;
    }

    // Get sample rate
    pa_operation *operation;
    operation = pa_context_get_server_info(pa->context, _malPulseAudioServerInfoCallback, context);
    _malPulseAudioOperationWait(pa->mainloop, operation);

    // Success
    pa_threaded_mainloop_unlock(pa->mainloop);
    return true;

unlock_and_fail:
    pa_threaded_mainloop_unlock(pa->mainloop);
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
    ok_queue_deinit(&pa->finishedCallbackIds);
}

static bool _malContextSetActive(MalContext *context, bool active) {
    if (context->active != active) {
        // TODO: When inactive, pause running streams, release stopped streams.
        // NOTE: Playback streams are a limited system-wide resource (32 on PulseAudio 4.0 and
        // older, 256 on PulseAudio 5.0 and newer).
    }
    return true;
}

static void _malContextUpdateMute(MalContext *context) {
    ok_vec_apply(&context->players, _malPlayerUpdateMute);
}

static void _malContextUpdateGain(MalContext *context) {
    ok_vec_apply(&context->players, _malPlayerUpdateGain);
}

void malContextPollEvents(MalContext *context) {
    if (!context) {
        return;
    }
    atomic_store(&context->data.hasPolledEvents, true);

    MalCallbackId onFinishedId = 0;
    while (ok_queue_pop(&context->data.finishedCallbackIds, &onFinishedId)) {
        _malHandleOnFinishedCallback(onFinishedId);
    }
}

// MARK: Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           const malDeallocatorFunc dataDeallocator) {
    if (managedData) {
        buffer->managedData = managedData;
        buffer->managedDataDeallocator = dataDeallocator;
    } else {
        const size_t dataLength = ((buffer->format.bitDepth / 8) *
                                   buffer->format.numChannels * buffer->numFrames);
        void *newBuffer = malloc(dataLength);
        if (!newBuffer) {
            return false;
        }
        memcpy(newBuffer, copiedData, dataLength);
        buffer->managedData = newBuffer;
        buffer->managedDataDeallocator = free;
    }
    return true;
}

static void _malBufferDispose(MalBuffer *buffer) {
    (void)buffer;
    // Do nothing
}

// MARK: Player

static void _malStreamStateCallback(pa_stream *stream, void *userData) {
    pa_threaded_mainloop *mainloop = userData;
    pa_threaded_mainloop_signal(mainloop, 0);
}

static void _malStreamSuccessCallback(pa_stream *stream, int success, void *userData) {
    pa_threaded_mainloop *mainloop = userData;
    pa_threaded_mainloop_signal(mainloop, 0);
}

static void _malStreamUnderflowCallback(pa_stream *stream, void *userData) {
    MalPlayer *player = userData;
    MAL_LOCK(player);
    if (player->data.streamState == MAL_STREAM_DRAINING) {
        player->data.streamState = MAL_STREAM_INACTIVE;
        if (player->data.state == MAL_PLAYER_STATE_PLAYING) {
            MalCallbackId onFinishedId = atomic_load(&player->onFinishedId);
            if (onFinishedId) {
                MalContext *context = player->context;
                if (context && atomic_load(&context->data.hasPolledEvents)) {
                    ok_queue_push(&context->data.finishedCallbackIds, onFinishedId);
                }
            }
        }
        player->data.state = MAL_PLAYER_STATE_STOPPED;
        pa_operation_unref(pa_stream_cork(player->data.stream, 1, NULL, NULL));
    }
    MAL_UNLOCK(player);
}

static void _malStreamWriteCallback(pa_stream *stream, size_t length, void *userData) {
    MalPlayer *player = userData;
    MAL_LOCK(player);
    if (player->data.streamState == MAL_STREAM_DRAINING ||
        player->data.state == MAL_PLAYER_STATE_STOPPED ||
        player->buffer == NULL || player->buffer->managedData == NULL) {
        MAL_UNLOCK(player);
        return;
    }

    pa_seek_mode_t seekMode = ((player->data.streamState == MAL_STREAM_STARTING) ?
                               PA_SEEK_RELATIVE_ON_READ : PA_SEEK_RELATIVE);
    player->data.streamState = MAL_STREAM_PLAYING;
    void *buffer;
    if (pa_stream_begin_write(stream, &buffer, &length) != PA_OK) {
        MAL_UNLOCK(player);
        return;
    }
    const uint32_t numFrames = player->buffer->numFrames;
    const uint32_t frameSize = ((player->buffer->format.bitDepth / 8) *
                                player->buffer->format.numChannels);
    size_t bytesWritten = 0;
    uint8_t *dst = buffer;
    uint32_t dstRemaining = (uint32_t)length;
    uint8_t *src = player->buffer->managedData;
    src += player->data.nextFrame * frameSize;
    while (dstRemaining > 0) {
        uint32_t playerFrames = numFrames - player->data.nextFrame;
        uint32_t maxFrames = dstRemaining / frameSize;
        uint32_t copyFrames = playerFrames < maxFrames ? playerFrames : maxFrames;
        uint32_t copyBytes = copyFrames * frameSize;

        if (copyBytes == 0) {
            break;
        }

        memcpy(dst, src, copyBytes);
        player->data.nextFrame += copyFrames;
        dst += copyBytes;
        src += copyBytes;
        dstRemaining -= copyBytes;
        bytesWritten += copyBytes;

        if (player->data.nextFrame >= player->buffer->numFrames) {
            player->data.nextFrame = 0;
            if (player->looping) {
                src = player->buffer->managedData;
            } else {
                player->data.streamState = MAL_STREAM_DRAINING;
                break;
            }
        }
    }

    MAL_UNLOCK(player);

    pa_stream_write(stream, buffer, bytesWritten, NULL, 0, seekMode);
}

static bool _malPlayerInit(MalPlayer *player) {
    (void)player;
    return true;
}

static void _malPlayerDispose(MalPlayer *player) {
    if (!player->context) {
        return;
    }
    struct _MalContext *pa = &player->context->data;

    if (pa->mainloop && player->data.stream) {
        pa_threaded_mainloop_lock(pa->mainloop);
        pa_stream_set_write_callback(player->data.stream, NULL, NULL);
        pa_stream_set_underflow_callback(player->data.stream, NULL, NULL);
        pa_stream_disconnect(player->data.stream);
        pa_stream_unref(player->data.stream);
        pa_threaded_mainloop_unlock(pa->mainloop);

        player->data.stream = NULL;
    }
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    if (!player->context) {
        return false;
    }

    // If the same format, do nothing
    if (player->data.stream && malContextIsFormatEqual(player->context, player->format, format)) {
        return true;
    }

    const int n = 1;
    const bool isLittleEndian = *(const char *)&n == 1;
    struct _MalContext *pa = &player->context->data;
    double sampleRate = (format.sampleRate <= MAL_DEFAULT_SAMPLE_RATE ?
                         malContextGetSampleRate(player->context) : format.sampleRate);

    pa_threaded_mainloop_lock(pa->mainloop);

    // Change sample rate without creating a new stream
    if (player->data.stream && (player->format.bitDepth == format.bitDepth &&
                                player->format.numChannels == format.numChannels)) {
        pa_operation *operation = pa_stream_update_sample_rate(player->data.stream,
                                                               (uint32_t)sampleRate,
                                                               _malStreamSuccessCallback,
                                                               pa->mainloop);
        _malPulseAudioOperationWait(pa->mainloop, operation);

        const pa_sample_spec *sampleSpec = pa_stream_get_sample_spec(player->data.stream);
        if (fabs(sampleSpec->rate - sampleRate) <= 1) {
            // Success
            goto quit;
        }
    }

    if (player->data.stream) {
        _malPlayerDispose(player);
    }

    pa_sample_format_t sampleFormat;
    switch (player->format.bitDepth) {
        case 8:
            sampleFormat = PA_SAMPLE_U8;
            break;
        case 16: default:
            sampleFormat = isLittleEndian ? PA_SAMPLE_S16LE : PA_SAMPLE_S16BE;
            break;
        case 24:
            sampleFormat = isLittleEndian ? PA_SAMPLE_S24LE : PA_SAMPLE_S24BE;
            break;
        case 32:
            sampleFormat = isLittleEndian ? PA_SAMPLE_S32LE : PA_SAMPLE_S32BE;
            break;
    }

    pa_sample_spec sampleSpec;
    sampleSpec.format = sampleFormat;
    sampleSpec.rate = (uint32_t)format.sampleRate;
    sampleSpec.channels = format.numChannels;

    double targetBufferDuration = 0.5;
    pa_buffer_attr bufferAttributes;
    bufferAttributes.tlength = ((player->format.bitDepth / 8) *
                                player->format.numChannels *
                                (uint32_t)(targetBufferDuration * format.sampleRate));
    bufferAttributes.maxlength = (uint32_t)-1;
    bufferAttributes.minreq = (uint32_t)-1;
    bufferAttributes.prebuf = 0;
    bufferAttributes.fragsize = (uint32_t)-1;

    pa_channel_map channelMap;
    if (!pa_channel_map_init_auto(&channelMap, format.numChannels, PA_CHANNEL_MAP_WAVEEX)) {
        goto quit;
    }

    int flags = (PA_STREAM_START_CORKED |       // Start paused
                 PA_STREAM_ADJUST_LATENCY |     // Let server pick buffer metrics
                 PA_STREAM_INTERPOLATE_TIMING | // For pa_stream_get_time()
                 PA_STREAM_NOT_MONOTONIC |      // For pa_stream_get_time()
                 PA_STREAM_AUTO_TIMING_UPDATE | // For pa_stream_get_time()
                 PA_STREAM_VARIABLE_RATE);      // For pa_stream_update_sample_rate()

    pa_stream *stream = pa_stream_new(pa->context, "Playback Stream", &sampleSpec, &channelMap);
    if (!stream) {
        goto quit;
    }

    pa_stream_state_t state = PA_STREAM_UNCONNECTED;
    pa_stream_set_state_callback(stream, _malStreamStateCallback, pa->mainloop);
    if (pa_stream_connect_playback(stream, NULL, &bufferAttributes,
                                   (pa_stream_flags_t)flags, NULL, NULL) == PA_OK) {
        while (1) {
            state = pa_stream_get_state(stream);
            if (state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state)) {
                break;
            }
            pa_threaded_mainloop_wait(pa->mainloop);
        }
    }
    pa_stream_set_state_callback(stream, NULL, NULL);

    if (state != PA_STREAM_READY) {
        pa_stream_unref(stream);
        goto quit;
    }

    pa_stream_set_write_callback(stream, _malStreamWriteCallback, player);
    pa_stream_set_underflow_callback(stream, _malStreamUnderflowCallback, player);
    player->data.stream = stream;

quit:
    pa_threaded_mainloop_unlock(pa->mainloop);

    if (player->data.stream) {
        _malPlayerUpdateMute(player);
        _malPlayerUpdateGain(player);
        _malPlayerSetBuffer(player, player->buffer);
    }

    return (player->data.stream != NULL);
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    (void)player;
    (void)buffer;
    return true;
}

static void _malPlayerUpdateMute(MalPlayer *player) {
    if (player && player->context && player->data.stream) {
        struct _MalContext *pa = &player->context->data;
        bool mute = player->context->mute || player->mute;

        pa_threaded_mainloop_lock(pa->mainloop);
        uint32_t index = pa_stream_get_index(player->data.stream);
        pa_operation_unref(pa_context_set_sink_input_mute(pa->context, index, mute ? 1 : 0,
                                                          NULL, NULL));
        pa_threaded_mainloop_unlock(pa->mainloop);
    }
}

static void _malPlayerUpdateGain(MalPlayer *player) {
    if (player && player->context && player->data.stream) {
        struct _MalContext *pa = &player->context->data;
        float gain = player->context->gain * player->gain;

        pa_volume_t volume = pa_sw_volume_from_linear((double)gain);
        pa_cvolume cvolume;
        cvolume.channels = player->format.numChannels;
        for (int i = 0; i < cvolume.channels; i++) {
            cvolume.values[i] = volume;
        }

        pa_threaded_mainloop_lock(pa->mainloop);
        uint32_t index = pa_stream_get_index(player->data.stream);
        pa_operation_unref(pa_context_set_sink_input_volume(pa->context, index, &cvolume,
                                                            NULL, NULL));
        pa_threaded_mainloop_unlock(pa->mainloop);
    }
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
    (void)player;
    (void)looping;
    // Do nothing
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    (void)player;
    // Do nothing
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    return player->data.state;
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state) {
    if (!player->context || !player->data.stream) {
        return false;
    }

    struct _MalContext *pa = &player->context->data;

    pa_threaded_mainloop_lock(pa->mainloop);

    switch (state) {
        case MAL_PLAYER_STATE_STOPPED:
        default: {
            pa_operation_unref(pa_stream_cork(player->data.stream, 1, NULL, NULL));
            player->data.nextFrame = 0;
            player->data.streamState = MAL_STREAM_INACTIVE;
            break;
        }
        case MAL_PLAYER_STATE_PAUSED: {
            pa_operation_unref(pa_stream_cork(player->data.stream, 1, NULL, NULL));
            break;
        }
        case MAL_PLAYER_STATE_PLAYING: {
            player->data.streamState = ((oldState == MAL_PLAYER_STATE_PAUSED) ? MAL_STREAM_PLAYING :
                                        MAL_STREAM_STARTING);
            pa_operation_unref(pa_stream_cork(player->data.stream, 0, NULL, NULL));
            break;
        }
    }

    pa_threaded_mainloop_unlock(pa->mainloop);

    player->data.state = state;
    return true;
}

#endif
