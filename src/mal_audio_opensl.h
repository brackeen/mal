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

#ifndef MAL_AUDIO_OPENSL_H
#define MAL_AUDIO_OPENSL_H

#include <SLES/OpenSLES.h>
#include <stdbool.h>
#if defined(__ANDROID__)
#include <android/looper.h>
#include <android/native_activity.h>
#include <fcntl.h>
#include <unistd.h>
#define LOOPER_ID_USER_MESSAGE 0x1ffbdff1
// From http://mobilepearls.com/labs/native-android-api/ndk/docs/opensles/
// "The buffer queue interface is expected to have significant changes... We recommend that your
// application code use Android simple buffer queues instead, because we do not plan to change
// that API"
#include <SLES/OpenSLES_Android.h>
#undef SL_DATALOCATOR_BUFFERQUEUE
#define SL_DATALOCATOR_BUFFERQUEUE SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE
#define SLDataLocator_BufferQueue SLDataLocator_AndroidSimpleBufferQueue
#define SLBufferQueueItf SLAndroidSimpleBufferQueueItf
#endif /* __ANDROID__ */

struct _MalContext {
    SLObjectItf slObject;
    SLEngineItf slEngine;
    SLObjectItf slOutputMixObject;

#if defined(__ANDROID__)
    JavaVM *vm;
    jobject appContext;
    int32_t sdkVersion;
    ALooper *looper;
    int looperMessagePipe[2];
#endif
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    SLObjectItf slObject;
    SLPlayItf slPlay;
    SLVolumeItf slVolume;
    SLBufferQueueItf slBufferQueue;

    bool backgroundPaused;
};

#define MAL_USE_MUTEX
#include "mal_audio_abstract.h"
#include <math.h>

// MARK: Context

static bool _malContextInit(MalContext *context, void *androidActivity,
                            const char **errorMissingAudioSystem) {
    (void)errorMissingAudioSystem;

    // Create engine
    SLresult result = slCreateEngine(&context->data.slObject, 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Realize engine
    result = (*context->data.slObject)->Realize(context->data.slObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Get engine interface
    result = (*context->data.slObject)->GetInterface(context->data.slObject, SL_IID_ENGINE,
                                                     &context->data.slEngine);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Get output mix
    result = (*context->data.slEngine)->CreateOutputMix(context->data.slEngine,
                                                        &context->data.slOutputMixObject, 0,
                                                        NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Realize the output mix
    result = (*context->data.slOutputMixObject)->Realize(context->data.slOutputMixObject,
                                                         SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

#if defined(__ANDROID__)
    if (androidActivity) {
        ANativeActivity *activity = (ANativeActivity *)androidActivity;
        JNIEnv *jniEnv = _malGetJNIEnv(activity->vm);
        if (jniEnv) {
            _jniClearException(jniEnv);

            context->data.vm = activity->vm;
            context->data.sdkVersion = activity->sdkVersion;
            _jniCallMethodWithArgs(jniEnv, activity->clazz, "setVolumeControlStream",
                                   "(I)V", Void, 0x00000003); /* STREAM_MUSIC */
            jobject appContext = _jniCallMethod(jniEnv, activity->clazz,
                                                "getApplicationContext",
                                                "()Landroid/content/Context;", Object);
            if (appContext) {
                context->data.appContext = (*jniEnv)->NewGlobalRef(jniEnv, appContext);
                (*jniEnv)->DeleteLocalRef(jniEnv, appContext);
            }
        }
    }
#endif

    return true;
}

static void _malContextCloseLooper(MalContext *context) {
#if defined(__ANDROID__)
    if (context && context->data.looper) {
        ALooper_removeFd(context->data.looper, context->data.looperMessagePipe[0]);
        close(context->data.looperMessagePipe[0]);
        context->data.looper = NULL;
    }
#endif
}

static void _malContextDispose(MalContext *context) {
    if (context->data.slOutputMixObject) {
        (*context->data.slOutputMixObject)->Destroy(context->data.slOutputMixObject);
        context->data.slOutputMixObject = NULL;
    }
    if (context->data.slObject) {
        (*context->data.slObject)->Destroy(context->data.slObject);
        context->data.slObject = NULL;
        context->data.slEngine = NULL;
    }
    _malContextCloseLooper(context);

#if defined(__ANDROID__)
    if (context->data.vm && context->data.appContext) {
        JNIEnv *jniEnv = _malGetJNIEnv(context->data.vm);
        if (jniEnv) {
            (*jniEnv)->DeleteGlobalRef(jniEnv, context->data.appContext);
            context->data.appContext = NULL;
        }
    }
#endif
}

#if defined(__ANDROID__)
enum looperMessageType {
    ON_PLAYER_FINISHED_MAGIC = 0x1df11fb1
};

struct looperMessage {
    enum looperMessageType type;
    MalCallbackId onFinishedId;
};

static int _malLooperCallback(int fd, int events, void *user) {
    struct looperMessage msg;

    if ((events & ALOOPER_EVENT_INPUT) != 0) {
        while (read(fd, &msg, sizeof(msg)) == sizeof(msg)) {
            if (msg.type == ON_PLAYER_FINISHED_MAGIC) {
                _malHandleOnFinishedCallback(msg.onFinishedId);
            }
        }
    }

    if ((events & ALOOPER_EVENT_HANGUP) != 0) {
        // Not sure this is right
        _malContextCloseLooper((MalContext *)user);
    }

    return 1;
}

static void _malLooperPost(int pipe, struct looperMessage *msg) {
    if (write(pipe, msg, sizeof(*msg)) != sizeof(*msg)) {
        // The pipe is full. Shouldn't happen, ignore
    }
}

#endif

static bool _malContextSetActive(MalContext *context, bool active) {
    if (context->active != active) {
        context->active = active;

#if defined(__ANDROID__)
        if (active) {
            ALooper *looper = ALooper_forThread();
            if (context->data.looper != looper) {
                _malContextCloseLooper(context);

                if (looper) {
                    int result = pipe2(context->data.looperMessagePipe, O_NONBLOCK | O_CLOEXEC);
                    if (result == 0) {
                        ALooper_addFd(looper, context->data.looperMessagePipe[0],
                                      LOOPER_ID_USER_MESSAGE, ALOOPER_EVENT_INPUT,
                                      _malLooperCallback, context);
                        context->data.looper = looper;
                    }
                }
            }
        } else {
            _malContextCloseLooper(context);
        }
#endif

        // From http://mobilepearls.com/labs/native-android-api/ndk/docs/opensles/
        // "Be sure to destroy your audio players when your activity is
        // paused, as they are a global resource shared with other apps."
        //
        // Here, we'll pause playing sounds, and destroy unused players.
        ok_vec_foreach(&context->players, MalPlayer *player) {
            if (active) {
                if (!player->data.slObject) {
                    malPlayerSetFormat(player, player->format);
                    _malPlayerUpdateMute(player);
                    _malPlayerUpdateGain(player);
                } else if (player->data.backgroundPaused &&
                           malPlayerGetState(player) == MAL_PLAYER_STATE_PAUSED) {
                    malPlayerSetState(player, MAL_PLAYER_STATE_PLAYING);
                }
                player->data.backgroundPaused = false;
            } else {
                switch (malPlayerGetState(player)) {
                    case MAL_PLAYER_STATE_STOPPED:
                        MAL_LOCK(player);
                        _malPlayerDispose(player);
                        MAL_UNLOCK(player);
                        player->data.backgroundPaused = false;
                        break;
                    case MAL_PLAYER_STATE_PAUSED:
                        player->data.backgroundPaused = false;
                        break;
                    case MAL_PLAYER_STATE_PLAYING: {
                        bool success = malPlayerSetState(player, MAL_PLAYER_STATE_PAUSED);
                        player->data.backgroundPaused = success;
                        break;
                    }
                }
            }
        }
    }
    return true;
}

static void _malContextUpdateMute(MalContext *context) {
    ok_vec_apply(&context->players, _malPlayerUpdateMute);
}

static void _malContextUpdateGain(MalContext *context) {
    ok_vec_apply(&context->players, _malPlayerUpdateGain);
}

// MARK: Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           const malDeallocatorFunc dataDeallocator) {
    (void)context;
    (void)buffer->data.dummy;
    const size_t dataLength = ((buffer->format.bitDepth / 8) *
                                buffer->format.numChannels * buffer->numFrames);
    if (managedData) {
        buffer->managedData = managedData;
        buffer->managedDataDeallocator = dataDeallocator;
    } else {
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

// Buffer queue callback, which is called on a different thread.
//
// According to the Android team, "it is unspecified whether buffer queue callbacks are called upon
// transition to SL_PLAYSTATE_STOPPED or by BufferQueue::Clear."
//
// The Android team recommends "non-blocking synchronization", but the lock will be uncontended in
// most cases.
// Also, Chromium has locks in their OpenSLES-based audio implementation.
static void _malBufferQueueCallback(SLBufferQueueItf queue, void *voidPlayer) {
    MalPlayer *player = (MalPlayer *)voidPlayer;
    if (player && queue) {
        MAL_LOCK(player);
        if (player->looping && player->buffer &&
            player->buffer->managedData &&
            _malPlayerGetState(player) == MAL_PLAYER_STATE_PLAYING) {
            const MalBuffer *buffer = player->buffer;
            const uint32_t len = (uint32_t)(buffer->numFrames * (buffer->format.bitDepth / 8) *
                    buffer->format.numChannels);
            (*queue)->Enqueue(queue, buffer->managedData, len);
        } else if (player->data.slPlay) {
            (*player->data.slPlay)->SetPlayState(player->data.slPlay, SL_PLAYSTATE_STOPPED);
            if (player->onFinished && player->context && player->context->data.looper) {
                struct looperMessage msg = {
                    .type = ON_PLAYER_FINISHED_MAGIC,
                    .onFinishedId = atomic_load(&player->onFinishedId),
                };
                _malLooperPost(player->context->data.looperMessagePipe[1], &msg);
            }
        }
        MAL_UNLOCK(player);
    }
}

static void _malPlayerUpdateMute(MalPlayer *player) {
    if (player && player->context && player->data.slVolume) {
        bool mute = player->mute || player->context->mute;
        (*player->data.slVolume)->SetMute(player->data.slVolume,
                                          mute ? SL_BOOLEAN_TRUE : SL_BOOLEAN_FALSE);
    }
}

static void _malPlayerUpdateGain(MalPlayer *player) {
    if (player && player->context && player->data.slVolume) {
        float gain = player->context->gain * player->gain;
        SLmillibel millibelVolume = (SLmillibel)lroundf(2000 * log10f(gain));
        if (millibelVolume < SL_MILLIBEL_MIN) {
            millibelVolume = SL_MILLIBEL_MIN;
        } else if (millibelVolume > 0) {
            millibelVolume = 0;
        }
        (*player->data.slVolume)->SetVolumeLevel(player->data.slVolume, millibelVolume);
    }
}

static bool _malPlayerInit(MalPlayer *player) {
    (void)player;
    // Do nothing
    return true;
}

static void _malPlayerDispose(MalPlayer *player) {
    if (player->data.slObject) {
        (*player->data.slObject)->Destroy(player->data.slObject);
        player->data.slObject = NULL;
        player->data.slBufferQueue = NULL;
        player->data.slPlay = NULL;
        player->data.slVolume = NULL;
    }
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    (void)player;
    // Do nothing
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    if (!player->context) {
        return false;
    }

    if (player->data.slObject && malContextIsFormatEqual(player->context, player->format, format)) {
        return true;
    }

    _malPlayerDispose(player);

    const int n = 1;
    const bool systemIsLittleEndian = *(const char *)&n == 1;

    SLDataLocator_BufferQueue slBufferQueue = {
        .locatorType = SL_DATALOCATOR_BUFFERQUEUE,
        .numBuffers = 2
    };

    double sampleRate = (format.sampleRate <= MAL_DEFAULT_SAMPLE_RATE ?
                         malContextGetSampleRate(player->context) : format.sampleRate);

    SLDataFormat_PCM slFormat = {
        .formatType = SL_DATAFORMAT_PCM,
        .numChannels = format.numChannels,
        .samplesPerSec = (SLuint32)(sampleRate * 1000),
        .bitsPerSample = (format.bitDepth == 8 ? SL_PCMSAMPLEFORMAT_FIXED_8 :
                          SL_PCMSAMPLEFORMAT_FIXED_16),
        .containerSize = (format.bitDepth == 8 ? SL_PCMSAMPLEFORMAT_FIXED_8 :
                          SL_PCMSAMPLEFORMAT_FIXED_16),
        .channelMask = (format.numChannels == 2 ?
                        (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER),
        .endianness = (systemIsLittleEndian ? SL_BYTEORDER_LITTLEENDIAN : SL_BYTEORDER_BIGENDIAN)
    };

    SLDataSource slDataSource = {&slBufferQueue, &slFormat};
    SLDataLocator_OutputMix slOutputMix = {SL_DATALOCATOR_OUTPUTMIX,
        player->context->data.slOutputMixObject};
    SLDataSink slAudioSink = {&slOutputMix, NULL};

    // Create the player
    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    SLresult result =
    (*player->context->data.slEngine)->CreateAudioPlayer(player->context->data.slEngine,
                                                         &player->data.slObject, &slDataSource,
                                                         &slAudioSink, 2, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        player->data.slObject = NULL;
        return false;
    }

    // Realize the player
    result = (*player->data.slObject)->Realize(player->data.slObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        _malPlayerDispose(player);
        return false;
    }

    // Get the play interface
    result = (*player->data.slObject)->GetInterface(player->data.slObject, SL_IID_PLAY,
                                                    &player->data.slPlay);
    if (result != SL_RESULT_SUCCESS) {
        _malPlayerDispose(player);
        return false;
    }

    // Get buffer queue interface
    result = (*player->data.slObject)->GetInterface(player->data.slObject, SL_IID_BUFFERQUEUE,
                                                    &player->data.slBufferQueue);
    if (result != SL_RESULT_SUCCESS) {
        _malPlayerDispose(player);
        return false;
    }

    // Register buffer queue callback
    result = (*player->data.slBufferQueue)->RegisterCallback(player->data.slBufferQueue,
                                                             _malBufferQueueCallback, player);
    if (result != SL_RESULT_SUCCESS) {
        _malPlayerDispose(player);
        return false;
    }

    // Get the volume interface (optional)
    result = (*player->data.slObject)->GetInterface(player->data.slObject, SL_IID_VOLUME,
                                                    &player->data.slVolume);
    if (result != SL_RESULT_SUCCESS) {
        player->data.slVolume = NULL;
    }

    player->format = format;
    _malPlayerUpdateMute(player);
    _malPlayerUpdateGain(player);
    return true;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    (void)player;
    (void)buffer;
    // Do nothing
    return true;
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
    (void)player;
    (void)looping;
    // Do nothing
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    if (!player->data.slPlay) {
        return MAL_PLAYER_STATE_STOPPED;
    } else {
        SLuint32 state;
        SLresult result = (*player->data.slPlay)->GetPlayState(player->data.slPlay, &state);
        if (result != SL_RESULT_SUCCESS) {
            return MAL_PLAYER_STATE_STOPPED;
        } else if (state == SL_PLAYSTATE_PAUSED) {
            return MAL_PLAYER_STATE_PAUSED;
        } else if (state == SL_PLAYSTATE_PLAYING) {
            return MAL_PLAYER_STATE_PLAYING;
        } else {
            return MAL_PLAYER_STATE_STOPPED;
        }
    }
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state) {
    if (!player->data.slPlay) {
        return false;
    }

    SLuint32 slState;
    switch (state) {
        case MAL_PLAYER_STATE_STOPPED:
        default:
            slState = SL_PLAYSTATE_STOPPED;
            break;
        case MAL_PLAYER_STATE_PAUSED:
            slState = SL_PLAYSTATE_PAUSED;
            break;
        case MAL_PLAYER_STATE_PLAYING:
            slState = SL_PLAYSTATE_PLAYING;
            break;
    }

    // Queue if needed
    if (oldState != MAL_PLAYER_STATE_PAUSED && slState == SL_PLAYSTATE_PLAYING &&
        player->data.slBufferQueue) {
        const MalBuffer *buffer = player->buffer;
        if (buffer->managedData) {
            const uint32_t len = (uint32_t)(buffer->numFrames * (buffer->format.bitDepth / 8) *
                    buffer->format.numChannels);
            (*player->data.slBufferQueue)->Enqueue(player->data.slBufferQueue,
                                                     buffer->managedData, len);
        }
    }

    (*player->data.slPlay)->SetPlayState(player->data.slPlay, slState);

    // Clear buffer queue
    if (slState == SL_PLAYSTATE_STOPPED && player->data.slBufferQueue) {
        (*player->data.slBufferQueue)->Clear(player->data.slBufferQueue);
    }

    return true;
}

#endif
