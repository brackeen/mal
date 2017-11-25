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
static pthread_mutex_t globalMutex = PTHREAD_MUTEX_INITIALIZER;
#  define MAL_LOCK(player) pthread_mutex_lock(&player->mutex)
#  define MAL_UNLOCK(player) pthread_mutex_unlock(&player->mutex)
#  define MAL_LOCK_GLOBAL() pthread_mutex_lock(&globalMutex)
#  define MAL_UNLOCK_GLOBAL() pthread_mutex_unlock(&globalMutex)
#else
#  define MAL_LOCK(player) do { } while(0)
#  define MAL_UNLOCK(player) do { } while(0)
#  define MAL_LOCK_GLOBAL() do { } while(0)
#  define MAL_UNLOCK_GLOBAL() do { } while(0)
#endif

// Audio subsystems need to implement these structs and functions.
// All mal_*init() functions should return `true` on success, `false` otherwise.

struct _MalContext;
struct _MalBuffer;
struct _MalPlayer;

static bool _malContextInit(MalContext *context);
static void _malContextDidCreate(MalContext *context);
static void _malContextWillDispose(MalContext *context);
static void _malContextDispose(MalContext *context);
static void _malContextDidSetActive(MalContext *context, bool active);
static void _malContextSetActive(MalContext *context, bool active);
static void _malContextSetMute(MalContext *context, bool mute);
static void _malContextSetGain(MalContext *context, float gain);
static void _malContextCheckRoutes(MalContext *context);
/**
 Either `copiedData` or `managedData` will be non-null, but not both. If `copiedData` is set,
 the data must be copied (don't keep a reference to `copiedData`).
 If successful, the buffer's #managedData and #managedDataDeallocator fields must be set.
 */
static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           malDeallocatorFunc dataDeallocator);
static void _malBufferDispose(MalBuffer *buffer);

static bool _malPlayerInit(MalPlayer *player);
static void _malPlayerDispose(MalPlayer *player);
static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format);
static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer);
static void _malPlayerSetMute(MalPlayer *player, bool mute);
static void _malPlayerSetGain(MalPlayer *player, float gain);
static void _malPlayerSetLooping(MalPlayer *player, bool looping);
static void _malPlayerDidSetFinishedCallback(MalPlayer *player);
static MalPlayerState _malPlayerGetState(const MalPlayer *player);
static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state);

// MARK: Globals

typedef struct ok_vec_of(MalPlayer *) MalPlayerVec;
typedef struct ok_vec_of(MalBuffer *) MalBufferVec;
typedef struct ok_map_of(uint64_t, MalPlayer *) MalCallbackMap;

static MalCallbackMap *globalActiveCallbacks = NULL;
static uint64_t nextFinishedCallbackID = 1;

// MARK: Structs

struct MalContext {
    MalPlayerVec players;
    MalBufferVec buffers;
    bool routes[NUM_MAL_ROUTES];
    float gain;
    bool mute;
    bool active;
    double sampleRate;

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

    struct _MalBuffer data;
};

struct MalPlayer {
    MalContext *context;
    MalFormat format;
    const MalBuffer *buffer;
    float gain;
    bool mute;
    bool looping;

    malPlaybackFinishedFunc onFinished;
    void *onFinishedUserData;
    uint64_t onFinishedId;

#ifdef MAL_USE_MUTEX
    pthread_mutex_t mutex;
#endif

    struct _MalPlayer data;
};

// MARK: Context

MalContext *malContextCreate(double outputSampleRate) {
    MalContext *context = (MalContext *)calloc(1, sizeof(MalContext));
    if (context) {
#ifdef MAL_USE_MUTEX
        pthread_mutex_init(&context->mutex, NULL);
#endif
        context->mute = false;
        context->gain = 1.0f;
        context->sampleRate = outputSampleRate;
        ok_vec_init(&context->players);
        ok_vec_init(&context->buffers);
        bool success = _malContextInit(context);
        if (success) {
            _malContextDidCreate(context);
            malContextSetActive(context, true);
        } else {
            malContextFree(context);
            context = NULL;
        }
    }
    return context;
}

void malContextSetActive(MalContext *context, bool active) {
    if (context) {
        MAL_LOCK(context);
        _malContextSetActive(context, active);
        context->active = active;
        MAL_UNLOCK(context);
        _malContextDidSetActive(context, active);
        if (active) {
            _malContextCheckRoutes(context);
        }
    }
}

bool malContextGetMute(const MalContext *context) {
    return context ? context->mute : false;
}

void malContextSetMute(MalContext *context, bool mute) {
    if (context) {
        context->mute = mute;
        _malContextSetMute(context, mute);
    }
}

float malContextGetGain(const MalContext *context) {
    return context ? context->gain : 1.0f;
}

void malContextSetGain(MalContext *context, float gain) {
    if (context) {
        context->gain = gain;
        _malContextSetGain(context, gain);
    }
}

bool malContextIsFormatValid(const MalContext *context, MalFormat format) {
    (void)context;
    // TODO: Move to subsystem
    return ((format.bitDepth == 8 || format.bitDepth == 16) &&
            (format.numChannels == 1 || format.numChannels == 2) &&
            format.sampleRate > 0);
}

bool malContextIsRouteEnabled(const MalContext *context, MalRoute route) {
    if (context && route < NUM_MAL_ROUTES) {
        return context->routes[route];
    } else {
        return false;
    }
}

void malContextFree(MalContext *context) {
    if (context) {
        // Delete players
        ok_vec_foreach(&context->players, MalPlayer *player) {
            malPlayerSetBuffer(player, NULL);
            MAL_LOCK(player);
            _malPlayerDispose(player);
            player->context = NULL;
            MAL_UNLOCK(player);
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
}

bool malFormatsEqual(MalFormat format1, MalFormat format2) {
    static double sampleRateEpsilon = 0.0001;
    return (format1.bitDepth == format2.bitDepth &&
            format1.numChannels == format2.numChannels &&
            fabs(format1.sampleRate - format2.sampleRate) <= sampleRateEpsilon);
}

// MARK: Buffer

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
        ok_vec_push(&context->buffers, buffer);
        buffer->context = context;
        buffer->format = format;
        buffer->numFrames = numFrames;

        bool success = _malBufferInit(context, buffer, copiedData, managedData,
                                        dataDeallocator);
        if (!success) {
            malBufferFree(buffer);
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
        static const MalFormat null_format = {0, 0, 0};
        return null_format;
    }
}

uint32_t malBufferGetNumFrames(const MalBuffer *buffer) {
    return buffer ? buffer->numFrames : 0;
}

void *malBufferGetData(const MalBuffer *buffer) {
    return buffer ? buffer->managedData : NULL;
}

void malBufferFree(MalBuffer *buffer) {
    if (buffer) {
        if (buffer->context) {
            // First, stop all players that are using this buffer.
            ok_vec_foreach(&buffer->context->players, MalPlayer *player) {
                if (player->buffer == buffer) {
                    malPlayerSetBuffer(player, NULL);
                }
            }
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
}

// MARK: Player

MalPlayer *malPlayerCreate(MalContext *context, MalFormat format) {
    // Check params
    if (!context || !malContextIsFormatValid(context, format)) {
        return NULL;
    }
    MalPlayer *player = (MalPlayer *)calloc(1, sizeof(MalPlayer));
    if (player) {
#ifdef MAL_USE_MUTEX
        pthread_mutex_init(&player->mutex, NULL);
#endif
        ok_vec_push(&context->players, player);
        player->context = context;
        player->format = format;
        player->gain = 1.0f;

        bool success = _malPlayerInit(player);
        if (success) {
            success = _malPlayerSetFormat(player, format);
        }
        if (!success) {
            malPlayerFree(player);
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

bool malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    if (player && malContextIsFormatValid(player->context, format)) {
        malPlayerSetState(player, MAL_PLAYER_STATE_STOPPED);
        MAL_LOCK(player);
        bool success = _malPlayerSetFormat(player, format);
        if (success) {
            player->format = format;
        }
        MAL_UNLOCK(player);
        return success;
    }
    return false;
}

bool malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    if (!player) {
        return false;
    } else {
        malPlayerSetState(player, MAL_PLAYER_STATE_STOPPED);
        MAL_LOCK(player);
        bool success = _malPlayerSetBuffer(player, buffer);
        if (success) {
            player->buffer = buffer;
        } else {
            player->buffer = NULL;
        }
        MAL_UNLOCK(player);
        return success;
    }
}

const MalBuffer *malPlayerGetBuffer(const MalPlayer *player) {
    return player ? player->buffer : NULL;
}

void malPlayerSetFinishedFunc(MalPlayer *player, malPlaybackFinishedFunc onFinished,
                              void *userData) {
    if (player) {
        MAL_LOCK_GLOBAL();
        if (!globalActiveCallbacks) {
            globalActiveCallbacks = (MalCallbackMap *)malloc(sizeof(MalCallbackMap));
            ok_map_init_custom(globalActiveCallbacks, ok_uint64_hash, ok_64bit_equals);
        }
        uint64_t oldOnFinishedId;
        uint64_t newOnFinishedId;
        if (onFinished != NULL) {
            newOnFinishedId = nextFinishedCallbackID;
            nextFinishedCallbackID++;
        } else {
            newOnFinishedId = 0;
        }
        MAL_LOCK(player);
        oldOnFinishedId = player->onFinishedId;
        player->onFinishedId = newOnFinishedId;
        player->onFinished = onFinished;
        player->onFinishedUserData = userData;
        MAL_UNLOCK(player);

        if (oldOnFinishedId) {
            ok_map_remove(globalActiveCallbacks, oldOnFinishedId);
        }
        if (newOnFinishedId) {
            ok_map_put(globalActiveCallbacks, newOnFinishedId, player);
        }

        MAL_UNLOCK_GLOBAL();
        _malPlayerDidSetFinishedCallback(player);
    }
}

malPlaybackFinishedFunc malPlayerGetFinishedFunc(MalPlayer *player) {
    return player ? player->onFinished : NULL;
}

static void _malHandleOnFinishedCallback(uint64_t onFinishedId) {
    // Find the player
    MalPlayer *player = NULL;
    MAL_LOCK_GLOBAL();
    if (globalActiveCallbacks) {
        player = ok_map_get(globalActiveCallbacks, onFinishedId);
    }
    MAL_UNLOCK_GLOBAL();
    // Send callback
    if (player && player->onFinished) {
        player->onFinished(player->onFinishedUserData, player);
    }
}

bool malPlayerGetMute(const MalPlayer *player) {
    return player && player->mute;
}

void malPlayerSetMute(MalPlayer *player, bool mute) {
    if (player) {
        MAL_LOCK(player);
        player->mute = mute;
        _malPlayerSetMute(player, mute);
        MAL_UNLOCK(player);
    }
}

float malPlayerGetGain(const MalPlayer *player) {
    return player ? player->gain : 1.0f;
}

void malPlayerSetGain(MalPlayer *player, float gain) {
    if (player) {
        MAL_LOCK(player);
        player->gain = gain;
        _malPlayerSetGain(player, gain);
        MAL_UNLOCK(player);
    }
}

bool malPlayerIsLooping(const MalPlayer *player) {
    return player ? player->looping : false;
}

void malPlayerSetLooping(MalPlayer *player, bool looping) {
    if (player) {
        MAL_LOCK(player);
        player->looping = looping;
        _malPlayerSetLooping(player, looping);
        MAL_UNLOCK(player);
    }
}

bool malPlayerSetState(MalPlayer *player, MalPlayerState state) {
    if (!player || !player->buffer) {
        return false;
    } else {
        MAL_LOCK(player);
        MalPlayerState oldState = _malPlayerGetState(player);
        bool success = true;
        if (state != oldState) {
            success = _malPlayerSetState(player, oldState, state);
        }
        MAL_UNLOCK(player);
        return success;
    }
}

MalPlayerState malPlayerGetState(MalPlayer *player) {
    if (!player || !player->buffer) {
        return MAL_PLAYER_STATE_STOPPED;
    } else {
        MAL_LOCK(player);
        MalPlayerState state = _malPlayerGetState(player);
        MAL_UNLOCK(player);
        return state;
    }
}

void malPlayerFree(MalPlayer *player) {
    if (player) {
        malPlayerSetBuffer(player, NULL);
        malPlayerSetFinishedFunc(player, NULL, NULL);
        MAL_LOCK(player);
        _malPlayerDispose(player);
        if (player->context) {
            ok_vec_remove(&player->context->players, player);
            player->context = NULL;
        }
        MAL_UNLOCK(player);
#ifdef MAL_USE_MUTEX
        pthread_mutex_destroy(&player->mutex);
#endif
        free(player);
    }
}

#endif
