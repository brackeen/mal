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

#ifndef MAL_AUDIO_ABSTRACT_H
#define MAL_AUDIO_ABSTRACT_H

#include "mal.h"
#include "ok_lib.h"
#include <math.h>

// If MAL_USE_MUTEX is defined, modifications to MalPlayer objects are locked.
// Define MAL_USE_MUTEX if a player's buffer data is read on a different thread than the main
// thread.
#ifdef MAL_USE_MUTEX
#  include <pthread.h>
#  define MAL_LOCK(player) pthread_mutex_lock(&player->mutex)
#  define MAL_UNLOCK(player) pthread_mutex_unlock(&player->mutex)
#else
#  define MAL_LOCK(player) do { } while(0)
#  define MAL_UNLOCK(player) do { } while(0)
#endif

// MARK: Atomics

#if defined(MAL_NO_STDATOMIC)
#  if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#    define _Atomic(T) T
#  endif
#  define atomic_load(object) *(object)
#  define atomic_store(object, value) *(object) = value
#  define OK_ATOMIC_INC(value) (++(*(value)))
#  define OK_ATOMIC_DEC(value) (--(*(value)))
#elif defined(OK_LIB_USE_STDATOMIC)
#  define OK_ATOMIC_INC(value) (atomic_fetch_add_explicit((value), 1, memory_order_relaxed) + 1)
#  define OK_ATOMIC_DEC(value) (atomic_fetch_sub_explicit((value), 1, memory_order_relaxed) - 1)
#elif defined(_MSC_VER)
#  if UINTPTR_MAX == UINT64_MAX
#    define OK_ATOMIC_INC(value) InterlockedIncrement64(value)
#    define OK_ATOMIC_DEC(value) InterlockedDecrement64(value)
#  else
#    define OK_ATOMIC_INC(value) InterlockedIncrement(value)
#    define OK_ATOMIC_DEC(value) InterlockedDecrement(value)
#  endif
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
#  define OK_ATOMIC_INC(value) (__atomic_fetch_add((value), 1, __ATOMIC_RELAXED) + 1)
#  define OK_ATOMIC_DEC(value) (__atomic_fetch_sub((value), 1, __ATOMIC_RELAXED) - 1)
#else
#  error stdatomic.h required
#endif

// Audio subsystems need to implement these structs and functions.
// All functions that return a `bool` should return `true` on success, `false` otherwise.

struct _MalContext;
struct _MalBuffer;
struct _MalPlayer;

static bool _malContextInit(MalContext *context, void *androidActivity,
                            const char **errorMissingAudioSystem);
static void _malContextDidCreate(MalContext *context);
static void _malContextWillDispose(MalContext *context);
static void _malContextDispose(MalContext *context);
static void _malContextDidSetActive(MalContext *context, bool active);
static bool _malContextSetActive(MalContext *context, bool active);
static void _malContextUpdateMute(MalContext *context);
static void _malContextUpdateGain(MalContext *context);
/**
 Either `copiedData` or `managedData` will be non-null, but not both. If `copiedData` is set,
 the data must be copied (don't keep a reference to `copiedData`).
 If successful, the buffer's #managedData and #managedDataDeallocator fields must be set.
 */
static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           malDeallocatorFunc dataDeallocator);
static void _malBufferDispose(MalBuffer *buffer);

static bool _malPlayerInit(MalPlayer *player, MalFormat format);
static void _malPlayerDispose(MalPlayer *player);
static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer);
static void _malPlayerUpdateMute(MalPlayer *player);
static void _malPlayerUpdateGain(MalPlayer *player);
static bool _malPlayerSetLooping(MalPlayer *player, bool looping);
static bool _malPlayerSetState(MalPlayer *player, MalPlayerState state);

// MARK: Globals

typedef struct ok_vec_of(MalPlayer *) MalPlayerVec;
typedef struct ok_vec_of(MalBuffer *) MalBufferVec;

// MARK: Structs

typedef enum {
    MAL_STREAM_STOPPED = 0,
    MAL_STREAM_STARTING,
    MAL_STREAM_PLAYING,
    MAL_STREAM_PAUSING,
    MAL_STREAM_PAUSED,
    MAL_STREAM_RESUMING,
    MAL_STREAM_STOPPING,
    MAL_STREAM_DRAINING,
} MalStreamState;

struct MalContext {
    MalPlayerVec players;
    MalBufferVec buffers;
    float gain;
    bool mute;
    bool active;
    double requestedSampleRate;
    double actualSampleRate;

    _Atomic(size_t) refCount;

#ifdef MAL_USE_MUTEX
    pthread_mutex_t mutex;
#endif

    struct _MalContext data;
};

struct MalBuffer {
    MalContext *context;
    MalFormat format;
    uint32_t numFrames;
    void *managedData;
    malDeallocatorFunc managedDataDeallocator;

    _Atomic(size_t) refCount;

    struct _MalBuffer data;
};

struct MalPlayer {
    MalContext *context;
    MalFormat format;
    MalBuffer *buffer;
    _Atomic(MalStreamState) streamState;
    float gain;
    bool mute;
    _Atomic(bool) looping;

    _Atomic(size_t) refCount;

    malPlaybackFinishedFunc onFinished;
    void *onFinishedUserData;
    _Atomic(bool) hasOnFinishedCallback;

#ifdef MAL_USE_MUTEX
    pthread_mutex_t mutex;
#endif

    struct _MalPlayer data;
};

// MARK: Sample rate helper functions

#ifdef MAL_INCLUDE_SAMPLE_RATE_FUNCTIONS

static double _malGetClosestSampleRate(double sampleRate) {
    const double typicalSampleRates[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
        48000, 88200, 96000, 176400, 192000 };
    const size_t typicalSampleRatesSize = sizeof(typicalSampleRates) / sizeof(*typicalSampleRates);
    double closestSampleRate = typicalSampleRates[0];
    for (size_t i = 1; i < typicalSampleRatesSize; i++) {
        double diff = fabs(typicalSampleRates[i] - sampleRate);
        if (diff <= fabs(closestSampleRate - sampleRate)) {
            closestSampleRate = typicalSampleRates[i];
        }
    }
    return closestSampleRate;
}

#endif

// MARK: Context

MalContext *malContextCreate() {
    return malContextCreateWithOptions(MAL_DEFAULT_SAMPLE_RATE, NULL, NULL);
}

MalContext *malContextCreateWithOptions(double requestedSampleRate, void *androidActivity,
                                        const char **errorMissingAudioSystem) {

    MalContext *context = (MalContext *)calloc(1, sizeof(MalContext));
    if (context) {
#ifdef MAL_USE_MUTEX
        pthread_mutex_init(&context->mutex, NULL);
#endif
        atomic_store(&context->refCount, 1);
        context->mute = false;
        context->gain = 1.0f;
        context->requestedSampleRate = requestedSampleRate;
        ok_vec_init(&context->players);
        ok_vec_init(&context->buffers);
        bool success = _malContextInit(context, androidActivity, errorMissingAudioSystem);
        if (success) {
            _malContextDidCreate(context);
            success = malContextSetActive(context, true);
            if (context->actualSampleRate <= MAL_DEFAULT_SAMPLE_RATE) {
                context->actualSampleRate = 44100;
            }
            if (!success) {
                malContextRelease(context);
                context = NULL;
            }
        } else {
            malContextRelease(context);
            context = NULL;
        }
    }
    return context;
}

double malContextGetSampleRate(const MalContext *context) {
    return context ? context->actualSampleRate : 44100;
}

bool malContextSetActive(MalContext *context, bool active) {
    if (!context) {
        return false;
    }
    MAL_LOCK(context);
    bool success = _malContextSetActive(context, active);
    if (success) {
        context->active = active;
    }
    MAL_UNLOCK(context);
    if (success) {
        _malContextDidSetActive(context, active);
    }
    return success;
}

bool malContextGetMute(const MalContext *context) {
    return context ? context->mute : false;
}

void malContextSetMute(MalContext *context, bool mute) {
    if (context) {
        context->mute = mute;
        _malContextUpdateMute(context);
    }
}

float malContextGetGain(const MalContext *context) {
    return context ? context->gain : 1.0f;
}

void malContextSetGain(MalContext *context, float gain) {
    if (context) {
        context->gain = gain;
        _malContextUpdateGain(context);
    }
}

bool malContextIsFormatValid(const MalContext *context, MalFormat format) {
    (void)context;
    // TODO: Move to subsystem
    return ((format.bitDepth == 8 || format.bitDepth == 16) &&
            (format.numChannels == 1 || format.numChannels == 2));
}

static void _malContextFree(MalContext *context) {
    // Delete players
    ok_vec_foreach(&context->players, MalPlayer *player) {
        malPlayerSetBuffer(player, NULL);
        malPlayerSetFinishedFunc(player, NULL, NULL);
        _malPlayerDispose(player);
        player->context = NULL;
    }
    ok_vec_deinit(&context->players);
    
    // Delete buffers
    ok_vec_foreach(&context->buffers, MalBuffer *buffer) {
        _malBufferDispose(buffer);
        buffer->context = NULL;
    }
    ok_vec_deinit(&context->buffers);
    
    // Dispose and free
    _malContextWillDispose(context);
    malContextSetActive(context, false);
    MAL_LOCK(context);
    _malContextDispose(context);
    MAL_UNLOCK(context);
    
#ifdef MAL_USE_MUTEX
    pthread_mutex_destroy(&context->mutex);
#endif
    free(context);
}

void malContextRetain(MalContext *context) {
    if (context) {
        (void)OK_ATOMIC_INC(&context->refCount);
    }
}

void malContextRelease(MalContext *context) {
    if (context && OK_ATOMIC_DEC(&context->refCount) == 0) {
        _malContextFree(context);
    }
}

static bool _malSampleRatesEqual(double sampleRate1, double sampleRate2) {
    const double sampleRateEpsilon = 0.01;
    return fabs(sampleRate1 - sampleRate2) <= sampleRateEpsilon;
}

bool malContextIsFormatEqual(const MalContext *context, MalFormat format1, MalFormat format2) {
    if (format1.sampleRate <= MAL_DEFAULT_SAMPLE_RATE) {
        format1.sampleRate = malContextGetSampleRate(context);
    }
    if (format2.sampleRate <= MAL_DEFAULT_SAMPLE_RATE) {
        format2.sampleRate = malContextGetSampleRate(context);
    }
    return (format1.bitDepth == format2.bitDepth &&
            format1.numChannels == format2.numChannels &&
            _malSampleRatesEqual(format1.sampleRate, format2.sampleRate));
}

// MARK: Buffer

#ifdef MAL_USE_DEFAULT_BUFFER_IMPL

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           const malDeallocatorFunc dataDeallocator) {
    (void)context;
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

#endif

static MalBuffer *_malBufferCreateInternal(MalContext *context, const MalFormat format,
                                           const uint32_t numFrames, const void *copiedData,
                                           void *managedData,
                                           const malDeallocatorFunc dataDeallocator) {
    // Check params
    const bool oneNonNullData = (copiedData == NULL) != (managedData == NULL);
    if (!context || !malContextIsFormatValid(context, format) || numFrames == 0 ||
        !oneNonNullData) {
        return NULL;
    }
    MalBuffer *buffer = (MalBuffer *)calloc(1, sizeof(MalBuffer));
    if (buffer) {
        atomic_store(&buffer->refCount, 1);
        ok_vec_push(&context->buffers, buffer);
        buffer->context = context;
        buffer->format = format;
        buffer->numFrames = numFrames;

        bool success = _malBufferInit(context, buffer, copiedData, managedData,
                                        dataDeallocator);
        if (!success) {
            malBufferRelease(buffer);
            buffer = NULL;
        }
    }
    return buffer;
}

MalBuffer *malBufferCreate(MalContext *context, MalFormat format, uint32_t numFrames,
                           const void *data) {
    return _malBufferCreateInternal(context, format, numFrames, data, NULL, NULL);
}

MalBuffer *malBufferCreateNoCopy(MalContext *context, MalFormat format, uint32_t numFrames,
                                 void *data, malDeallocatorFunc dataDeallocator) {
    return _malBufferCreateInternal(context, format, numFrames, NULL, data, dataDeallocator);
}

MalFormat malBufferGetFormat(const MalBuffer *buffer) {
    if (buffer) {
        return buffer->format;
    } else {
        static const MalFormat nullFormat = {0, 0, 0};
        return nullFormat;
    }
}

uint32_t malBufferGetNumFrames(const MalBuffer *buffer) {
    return buffer ? buffer->numFrames : 0;
}

void *malBufferGetData(const MalBuffer *buffer) {
    return buffer ? buffer->managedData : NULL;
}

static void _malBufferFree(MalBuffer *buffer) {
    if (buffer->context) {
        ok_vec_remove(&buffer->context->buffers, buffer);
    }
    _malBufferDispose(buffer);
    if (buffer->managedData) {
        if (buffer->managedDataDeallocator) {
            buffer->managedDataDeallocator(buffer->managedData);
        }
        buffer->managedData = NULL;
    }
    free(buffer);
}

void malBufferRetain(MalBuffer *buffer) {
    if (buffer) {
        (void)OK_ATOMIC_INC(&buffer->refCount);
    }
}

void malBufferRelease(MalBuffer *buffer) {
    if (buffer && OK_ATOMIC_DEC(&buffer->refCount) == 0) {
        _malBufferFree(buffer);
    }
}

// MARK: Player

MalPlayer *malPlayerCreate(MalContext *context, MalFormat format) {
    // Check params
    if (!context || !malContextIsFormatValid(context, format)) {
        return NULL;
    }
    MalPlayer *player = (MalPlayer *)calloc(1, sizeof(MalPlayer));
    if (player) {
        atomic_store(&player->refCount, 1);
#ifdef MAL_USE_MUTEX
        pthread_mutex_init(&player->mutex, NULL);
#endif
        ok_vec_push(&context->players, player);
        player->context = context;
        player->format = format;
        player->gain = 1.0f;

        bool success = _malPlayerInit(player, format);
        if (!success) {
            malPlayerRelease(player);
            player = NULL;
        }
    }
    return player;
}

MalFormat malPlayerGetFormat(const MalPlayer *player) {
    if (player) {
        return player->format;
    } else {
        static const MalFormat null_format = {0, 0, 0};
        return null_format;
    }
}

bool malPlayerSetBuffer(MalPlayer *player, MalBuffer *buffer) {
    if (!player) {
        return false;
    } else {
        malPlayerSetState(player, MAL_PLAYER_STATE_STOPPED);
        bool success = _malPlayerSetBuffer(player, buffer);
        MAL_LOCK(player);
        malBufferRelease(player->buffer);
        if (success) {
            malBufferRetain(buffer);
            player->buffer = buffer;
        } else {
            player->buffer = NULL;
        }
        MAL_UNLOCK(player);
        return success;
    }
}

MalBuffer *malPlayerGetBuffer(const MalPlayer *player) {
    return player ? player->buffer : NULL;
}

void malPlayerSetFinishedFunc(MalPlayer *player, malPlaybackFinishedFunc onFinished,
                              void *userData) {
    if (player) {
        player->onFinished = onFinished;
        player->onFinishedUserData = userData;
        atomic_store(&player->hasOnFinishedCallback, onFinished != NULL);
    }
}

malPlaybackFinishedFunc malPlayerGetFinishedFunc(MalPlayer *player) {
    return player ? player->onFinished : NULL;
}

static void _malHandleOnFinishedCallback(MalPlayer *player) {
    if (player && player->onFinished) {
        player->onFinished(player, player->onFinishedUserData);
    }
}

bool malPlayerGetMute(const MalPlayer *player) {
    return player && player->mute;
}

void malPlayerSetMute(MalPlayer *player, bool mute) {
    if (player) {
        player->mute = mute;
        _malPlayerUpdateMute(player);
    }
}

float malPlayerGetGain(const MalPlayer *player) {
    return player ? player->gain : 1.0f;
}

void malPlayerSetGain(MalPlayer *player, float gain) {
    if (player) {
        player->gain = gain;
        _malPlayerUpdateGain(player);
    }
}

bool malPlayerIsLooping(const MalPlayer *player) {
    return player ? player->looping : false;
}

bool malPlayerSetLooping(MalPlayer *player, bool looping) {
    if (!player) {
        return false;
    } else {
        bool success = _malPlayerSetLooping(player, looping);
        if (success) {
            atomic_store(&player->looping, looping);
        }
        return success;
    }
}

bool malPlayerSetState(MalPlayer *player, MalPlayerState state) {
    if (!player || !player->buffer) {
        return false;
    } else {
        return _malPlayerSetState(player, state);
    }
}

MalPlayerState malPlayerGetState(MalPlayer *player) {
    if (!player) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    MalStreamState streamState = atomic_load(&player->streamState);
    switch (streamState) {
        case MAL_STREAM_STOPPED: case MAL_STREAM_STOPPING: default:
            return MAL_PLAYER_STATE_STOPPED;
        case MAL_STREAM_STARTING: case MAL_STREAM_PLAYING:
        case MAL_STREAM_RESUMING: case MAL_STREAM_DRAINING:
            return MAL_PLAYER_STATE_PLAYING;
        case MAL_STREAM_PAUSING: case MAL_STREAM_PAUSED:
            return MAL_PLAYER_STATE_PAUSED;
    }
}

static void _malPlayerFree(MalPlayer *player) {
    malPlayerSetBuffer(player, NULL);
    malPlayerSetFinishedFunc(player, NULL, NULL);
    _malPlayerDispose(player);
    if (player->context) {
        ok_vec_remove(&player->context->players, player);
        player->context = NULL;
    }
#ifdef MAL_USE_MUTEX
    pthread_mutex_destroy(&player->mutex);
#endif
    free(player);
}

void malPlayerRetain(MalPlayer *player) {
    if (player) {
        (void)OK_ATOMIC_INC(&player->refCount);
    }
}

void malPlayerRelease(MalPlayer *player) {
    if (player && OK_ATOMIC_DEC(&player->refCount) == 0) {
        _malPlayerFree(player);
    }
}

#endif
