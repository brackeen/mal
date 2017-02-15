/*
 mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2017 David Brackeen
 
 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef _MAL_AUDIO_OPENSL_H_
#define _MAL_AUDIO_OPENSL_H_

#include <SLES/OpenSLES.h>
#include <stdbool.h>
#ifdef ANDROID
#include <android/looper.h>
#define _GNU_SOURCE
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
#endif

struct _MalContext {
    SLObjectItf slObject;
    SLEngineItf slEngine;
    SLObjectItf slOutputMixObject;
#ifdef ANDROID
    ALooper *looper;
    int looperMessagePipe[2];
#endif
};

struct _MalBuffer {

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

static void _malPlayerUpdateGain(MalPlayer *player);

// MARK: Context

static bool _malContextInit(MalContext *context) {
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

    // NOTE: SLAudioIODeviceCapabilitiesItf isn't supported, so there's no way to get routing
    // information.
    // Also, GetDestinationOutputDeviceIDs only returns SL_DEFAULTDEVICEID_AUDIOOUTPUT.
    //
    // Potentially, if we have access to the ANativeActivity, we could get an instance of
    // AudioManager and call the Java functions:
    // if (isBluetoothA2dpOn() or isBluetoothScoOn()) then wireless
    // else if (isSpeakerphoneOn()) then speaker
    // else headset
    //
    return true;
}

static void _malContextCloseLooper(MalContext *context) {
#ifdef ANDROID
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
}

#ifdef ANDROID
enum looperMessageType {
    ON_PLAYER_FINISHED_MAGIC = 0xdff11ffb
};

struct looperMessage {
    enum looperMessageType type;
    uint64_t onFinishedId;
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

static int _malLooperPost(int pipe, struct looperMessage *msg) {
    if (write(pipe, msg, sizeof(*msg)) != sizeof(*msg)) {
        // The pipe is full. Shouldn't happen, ignore
    }
}

#endif

static void _malContextSetActive(MalContext *context, bool active) {
    if (context->active != active) {
        context->active = active;

#ifdef ANDROID
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
                    _malPlayerSetGain(player, player->gain);
                    _malPlayerSetMute(player, player->mute);
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
}

static void _malContextSetMute(MalContext *context, bool mute) {
    ok_vec_apply(&context->players, _malPlayerUpdateGain);
}

static void _malContextSetGain(MalContext *context, float gain) {
    ok_vec_apply(&context->players, _malPlayerUpdateGain);
}

// MARK: Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           const malDeallocatorFunc dataDeallocator) {
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
            const size_t len = (buffer->numFrames * (buffer->format.bitDepth / 8) *
                                buffer->format.numChannels);
            (*queue)->Enqueue(queue, buffer->managedData, len);
        } else if (player->data.slPlay) {
            (*player->data.slPlay)->SetPlayState(player->data.slPlay, SL_PLAYSTATE_STOPPED);
            if (player->onFinished && player->context && player->context->data.looper) {
                struct looperMessage msg = {
                    .type = ON_PLAYER_FINISHED_MAGIC,
                    .onFinishedId = player->onFinishedId,
                };
                _malLooperPost(player->context->data.looperMessagePipe[1], &msg);
            }
        }
        MAL_UNLOCK(player);
    }
}

static void _malPlayerUpdateGain(MalPlayer *player) {
    if (player && player->context && player->data.slVolume) {
        float gain = 0;
        if (!player->context->mute && !player->mute) {
            gain = player->context->gain * player->gain;
        }
        if (gain <= 0) {
            (*player->data.slVolume)->SetMute(player->data.slVolume, SL_BOOLEAN_TRUE);
        } else {
            SLmillibel millibelVolume = (SLmillibel)roundf(2000 * log10f(gain));
            if (millibelVolume < SL_MILLIBEL_MIN) {
                millibelVolume = SL_MILLIBEL_MIN;
            } else if (millibelVolume > 0) {
                millibelVolume = 0;
            }
            (*player->data.slVolume)->SetVolumeLevel(player->data.slVolume, millibelVolume);
            (*player->data.slVolume)->SetMute(player->data.slVolume, SL_BOOLEAN_FALSE);
        }
    }
}

static bool _malPlayerInit(MalPlayer *player) {
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
    // Do nothing
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    _malPlayerDispose(player);
    if (!player->context) {
        return false;
    }

    const int n = 1;
    const bool systemIsLittleEndian = *(char *)&n == 1;

    SLDataLocator_BufferQueue slBufferQueue = {
        .locatorType = SL_DATALOCATOR_BUFFERQUEUE,
        .numBuffers = 2
    };

    SLDataFormat_PCM slFormat = {
        .formatType = SL_DATAFORMAT_PCM,
        .numChannels = format.numChannels,
        .samplesPerSec = (SLuint32)(format.sampleRate * 1000),
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
    _malPlayerUpdateGain(player);
    return true;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    // Do nothing
    return true;
}

static void _malPlayerSetMute(MalPlayer *player, bool mute) {
    _malPlayerUpdateGain(player);
}

static void _malPlayerSetGain(MalPlayer *player, float gain) {
    _malPlayerUpdateGain(player);
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
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

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState,
                               MalPlayerState state) {
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
            const size_t len = (buffer->numFrames * (buffer->format.bitDepth / 8) *
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
