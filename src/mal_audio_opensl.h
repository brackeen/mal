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
#if defined(__ANDROID__)
#include <android/native_activity.h>
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

#include "ok_lib.h"
#include "mal.h"

struct _MalContext {
    SLObjectItf slObject;
    SLEngineItf slEngine;
    SLObjectItf slOutputMixObject;

#if defined(__ANDROID__)
    JavaVM *vm;
    jobject appContext;
    int32_t sdkVersion;
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

    OK_LOCK_TYPE lock;
    bool backgroundPaused;
};

#define MAL_USE_DEFAULT_BUFFER_IMPL
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

static bool _malContextSetActive(MalContext *context, bool active) {
    if (context->active != active) {
        // From http://mobilepearls.com/labs/native-android-api/ndk/docs/opensles/
        // "Be sure to destroy your audio players when your activity is
        // paused, as they are a global resource shared with other apps."
        //
        // Here, we'll pause playing sounds, and destroy unused players.
        ok_vec_foreach(&context->players, MalPlayer *player) {
            if (active) {
                if (!player->data.slObject) {
                    _malPlayerInit(player, player->format);
                } else if (player->data.backgroundPaused &&
                           malPlayerGetState(player) == MAL_PLAYER_STATE_PAUSED) {
                    malPlayerSetState(player, MAL_PLAYER_STATE_PLAYING);
                }
                player->data.backgroundPaused = false;
            } else {
                switch (malPlayerGetState(player)) {
                    case MAL_PLAYER_STATE_STOPPED:
                        _malPlayerDispose(player);
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

// MARK: Player

// Buffer queue callback, which is called on a different thread.
//
// According to the Android team, "it is unspecified whether buffer queue callbacks are called upon
// transition to SL_PLAYSTATE_STOPPED or by BufferQueue::Clear."
static void _malPlayerRenderCallback(SLBufferQueueItf queue, void *voidPlayer) {
    MalPlayer *player = (MalPlayer *)voidPlayer;
    if (player && queue) {
        if (!OK_TRYLOCK(&player->data.lock)) {
            // Edge case: buffer is being set
            return;
        }
        if (atomic_load(&player->looping) && player->buffer &&
            player->buffer->managedData &&
            malPlayerGetState(player) == MAL_PLAYER_STATE_PLAYING) {
            const MalBuffer *buffer = player->buffer;
            const uint32_t len = (uint32_t)(buffer->numFrames * (buffer->format.bitDepth / 8) *
                    buffer->format.numChannels);
            (*queue)->Enqueue(queue, buffer->managedData, len);
        } else {
            MalStreamState expectedState = MAL_STREAM_PLAYING;
            if (atomic_compare_exchange_strong(&player->streamState, &expectedState,
                                               MAL_STREAM_STOPPED) &&
                atomic_load(&player->hasOnFinishedCallback) && player->context) {
                malPlayerRetain(player);
                ok_queue_push(&player->context->finishedPlayersWithCallbacks, player);
            }
        }
        OK_UNLOCK(&player->data.lock);
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

static bool _malPlayerInit(MalPlayer *player, MalFormat format) {
    if (!player->context) {
        return false;
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
                                                             _malPlayerRenderCallback, player);
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

static void _malPlayerDispose(MalPlayer *player) {
    if (player->data.slObject) {
        (*player->data.slObject)->Destroy(player->data.slObject);
        player->data.slObject = NULL;
        player->data.slBufferQueue = NULL;
        player->data.slPlay = NULL;
        player->data.slVolume = NULL;
    }
}

static bool _malPlayerSetBuffer(MalPlayer *player, MalBuffer *buffer) {
    OK_LOCK(&player->data.lock);
    player->buffer = buffer;
    OK_UNLOCK(&player->data.lock);
    return true;
}

static bool _malPlayerSetLooping(MalPlayer *player, bool looping) {
    (void)player;
    (void)looping;
    // Do nothing
    return true;
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState state) {
    if (!player->data.slPlay) {
        return false;
    }

    while (1) {
        MalStreamState streamState = atomic_load(&player->streamState);
        MalPlayerState oldState = _malStreamStateToPlayerState(streamState);
        if (oldState == state) {
            return true;
        } else if (state == MAL_PLAYER_STATE_PAUSED) {
            // Pause isn't possible if stopped (or stopping)
            if (streamState == MAL_STREAM_STOPPING || streamState == MAL_STREAM_STOPPED ||
                streamState == MAL_STREAM_DRAINING) {
                return false;
            }
        }

        MalStreamState newStreamState;
        SLuint32 slState;
        switch (state) {
            case MAL_PLAYER_STATE_STOPPED:
            default:
                slState = SL_PLAYSTATE_STOPPED;
                newStreamState = MAL_STREAM_STOPPED;
                break;
            case MAL_PLAYER_STATE_PAUSED:
                slState = SL_PLAYSTATE_PAUSED;
                newStreamState = MAL_STREAM_PAUSED;
                break;
            case MAL_PLAYER_STATE_PLAYING:
                slState = SL_PLAYSTATE_PLAYING;
                newStreamState = MAL_STREAM_PLAYING;
                break;
        }

        if (atomic_compare_exchange_strong(&player->streamState, &streamState, newStreamState)) {
            // Queue if needed
            if (oldState != MAL_PLAYER_STATE_PAUSED && slState == SL_PLAYSTATE_PLAYING &&
                player->data.slBufferQueue) {
                const MalBuffer *buffer = player->buffer;
                if (buffer->managedData) {
                    uint32_t len = (uint32_t)(buffer->numFrames * (buffer->format.bitDepth / 8) *
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
    }
}

#endif
