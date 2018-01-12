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

#ifndef MAL_AUDIO_COREAUDIO_H
#define MAL_AUDIO_COREAUDIO_H

#include "mal.h"
#include <AudioToolbox/AudioToolbox.h>

#ifdef NDEBUG
#  define MAL_LOG(...) do { } while(0)
#else
#  define MAL_LOG(...) do { printf("Mal: " __VA_ARGS__); printf("\n"); } while(0)
#endif

struct _MalRamp {
    _Atomic(int) value; // -1 for fade out, 1 for fade in, 0 for no fade
    uint32_t frames;
    uint32_t framesPosition;
};

struct _MalContext {
    AUGraph graph;
    AudioUnit mixerUnit;
    AUNode mixerNode;

    bool firstTime;

    uint32_t numBuses;
    bool canRampInputGain;
    bool canRampOutputGain;
    struct _MalRamp ramp;
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    AudioUnit converterUnit;
    AUNode converterNode;
    uint32_t mixerBus;

    uint32_t nextFrame;
    MalPlayerState state;

    struct _MalRamp ramp;
};

#define MAL_USE_MUTEX
#include "mal_audio_abstract.h"

static void _malContextSetSampleRate(MalContext *context);
static void _malPlayerDidDispose(MalPlayer *player);

// MARK: Context

static OSStatus renderNotification(void *userData, AudioUnitRenderActionFlags *flags,
                                   const AudioTimeStamp *timestamp, UInt32 bus,
                                   UInt32 inFrames, AudioBufferList *data);

static bool _malContextInit(MalContext *context, void *androidActivity,
                            const char **errorMissingAudioSystem) {
    (void)androidActivity;
    (void)errorMissingAudioSystem;

    context->data.firstTime = true;
    context->active = false;

    // Call first because actual value may be different from requested, and the mixer rate
    // needs to be set to that value.
    _malContextSetSampleRate(context);

    // Create audio graph
    OSStatus status = NewAUGraph(&context->data.graph);
    if (status != noErr) {
        MAL_LOG("Couldn't create audio graph (err %i)", (int)status);
        return false;
    }

    // Create output node
    AUNode outputNode;
#if TARGET_OS_OSX
    UInt32 outputType = kAudioUnitSubType_DefaultOutput;
#elif TARGET_OS_IPHONE
    UInt32 outputType = kAudioUnitSubType_RemoteIO;
#endif

    AudioComponentDescription outputDesc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = outputType,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };
    status = AUGraphAddNode(context->data.graph, &outputDesc, &outputNode);
    if (status != noErr) {
        MAL_LOG("Couldn't create output node (err %i)", (int)status);
        return false;
    }

    // Create mixer node
    AudioComponentDescription mixerDesc = {
        .componentType = kAudioUnitType_Mixer,
        .componentSubType = kAudioUnitSubType_MultiChannelMixer,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };
    status = AUGraphAddNode(context->data.graph, &mixerDesc, &context->data.mixerNode);
    if (status != noErr) {
        MAL_LOG("Couldn't create mixer node (err %i)", (int)status);
        return false;
    }

    // Connect a node's output to a node's input
    status = AUGraphConnectNodeInput(context->data.graph, context->data.mixerNode, 0,
                                     outputNode, 0);
    if (status != noErr) {
        MAL_LOG("Couldn't connect nodes (err %i)", (int)status);
        return false;
    }

    // Open graph
    status = AUGraphOpen(context->data.graph);
    if (status != noErr) {
        MAL_LOG("Couldn't open graph (err %i)", (int)status);
        return false;
    }

    // Get mixer unit
    status = AUGraphNodeInfo(context->data.graph, context->data.mixerNode, NULL,
                             &context->data.mixerUnit);
    if (status != noErr) {
        MAL_LOG("Couldn't get mixer unit (err %i)", (int)status);
        return false;
    }

    // Set mixer output sample rate
    // NOTE: This can be set to any value. Best to set it to same as the device output rate.
    Float64 sampleRate = (Float64)context->actualSampleRate;
    UInt32 sampleRateSize = (UInt32)sizeof(sampleRate);
    if (sampleRate > MAL_DEFAULT_SAMPLE_RATE) {
        status = AudioUnitSetProperty(context->data.mixerUnit,
                                      kAudioUnitProperty_SampleRate,
                                      kAudioUnitScope_Output,
                                      0,
                                      &sampleRate,
                                      sampleRateSize);
        if (status != noErr) {
            MAL_LOG("Ignoring: Couldn't set output sample rate (err %i)", (int)status);
        }
    }

    // Check if input gain can ramp
    AudioUnitParameterInfo parameterInfo;
    UInt32 parameterInfoSize = sizeof(AudioUnitParameterInfo);
    status = AudioUnitGetProperty(context->data.mixerUnit, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Input, kMultiChannelMixerParam_Volume,
                                  &parameterInfo, &parameterInfoSize);
    if (status == noErr) {
        context->data.canRampInputGain =
            (parameterInfo.flags & kAudioUnitParameterFlag_CanRamp) != 0;
    } else {
        context->data.canRampInputGain = false;
    }

    // Check if output gain can ramp
    status = AudioUnitGetProperty(context->data.mixerUnit, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Output, kMultiChannelMixerParam_Volume,
                                  &parameterInfo, &parameterInfoSize);
    if (status == noErr) {
        context->data.canRampOutputGain =
            (parameterInfo.flags & kAudioUnitParameterFlag_CanRamp) != 0;
    } else {
        context->data.canRampOutputGain = false;
    }
    if (context->data.canRampOutputGain) {
        status = AudioUnitAddRenderNotify(context->data.mixerUnit, renderNotification, context);
        if (status != noErr) {
            context->data.canRampOutputGain = false;
        }
    }

    // Get bus count
    UInt32 busSize = sizeof(context->data.numBuses);
    status = AudioUnitGetProperty(context->data.mixerUnit,
                                  kAudioUnitProperty_ElementCount,
                                  kAudioUnitScope_Input,
                                  0,
                                  &context->data.numBuses,
                                  &busSize);
    if (status != noErr) {
        MAL_LOG("Couldn't get mixer unit (err %i)", (int)status);
        return false;
    }

    // Set output volume
    _malContextUpdateGain(context);

    // Init
    status = AUGraphInitialize(context->data.graph);
    if (status != noErr) {
        MAL_LOG("Couldn't init graph (err %i)", (int)status);
        return false;
    }

    return true;
}

static void _malContextDidDispose(MalContext *context) {
    context->active = false;
    context->data.graph = NULL;
    context->data.mixerUnit = NULL;
    context->data.mixerNode = 0;
}

static void _malContextDispose(MalContext *context) {
    if (context->data.graph) {
        AUGraphStop(context->data.graph);
        AUGraphUninitialize(context->data.graph);
        DisposeAUGraph(context->data.graph);
    }
    _malContextDidDispose(context);
}

static void _malContextReset(MalContext *context) {
    bool active = context->active;
    ok_vec_foreach(&context->players, MalPlayer *player) {
        _malPlayerDispose(player);
    }
    MAL_LOCK(context);
    _malContextDispose(context);
    _malContextInit(context, NULL, NULL);
    _malContextUpdateGain(context);
    MAL_UNLOCK(context);
    malContextSetActive(context, active);
    ok_vec_foreach(&context->players, MalPlayer *player) {
        bool success = _malPlayerInit(player, player->format);
        if (!success) {
            MAL_LOG("Couldn't reset player");
        } else {
            if (player->data.state == MAL_PLAYER_STATE_PLAYING) {
                player->data.state = MAL_PLAYER_STATE_STOPPED;
                malPlayerSetState(player, MAL_PLAYER_STATE_PLAYING);
            }
        }
    }
}

static bool _malContextSetActive(MalContext *context, bool active) {
    if (context->active == active) {
        return true;
    }
    OSStatus status;
    if (active) {
        Boolean running = false;
        AUGraphIsRunning(context->data.graph, &running);
        if (running) {
            // Hasn't called AUGraphStop yet - do nothing
            atomic_store(&context->data.ramp.value, 0);
            status = noErr;
        } else {
            if (!context->data.firstTime && context->data.canRampOutputGain) {
                // Fade in
                atomic_store(&context->data.ramp.value, 1);
                context->data.ramp.frames = 4096;
                context->data.ramp.framesPosition = 0;
            }
            status = AUGraphStart(context->data.graph);
            context->data.firstTime = false;
        }
    } else {
        if (context->data.canRampOutputGain) {
            // Fade out
            atomic_store(&context->data.ramp.value, -1);
            context->data.ramp.frames = 4096;
            context->data.ramp.framesPosition = 0;
            status = noErr;
        } else {
            status = AUGraphStop(context->data.graph);
        }
    }
    if (status != noErr) {
        MAL_LOG("Couldn't %s graph (err %i)", (active ? "start" : "stop"), (int)status);
    }
    return status == noErr;
}

static void _malContextUpdateMute(MalContext *context) {
    _malContextUpdateGain(context);
}

static void _malContextUpdateGain(MalContext *context) {
    float totalGain = context->mute ? 0.0f : context->gain;
    OSStatus status = AudioUnitSetParameter(context->data.mixerUnit,
                                            kMultiChannelMixerParam_Volume,
                                            kAudioUnitScope_Output,
                                            0,
                                            totalGain,
                                            0);

    if (status != noErr) {
        MAL_LOG("Couldn't set volume (err %i)", (int)status);
    }
}

static bool _malRamp(MalContext *context, AudioUnitScope scope, AudioUnitElement bus,
                     uint32_t inFrames, float gain, struct _MalRamp *ramp) {
    uint32_t t = ramp->frames;
    uint32_t p1 = ramp->framesPosition;
    uint32_t p2 = p1 + inFrames;
    bool done = false;
    if (p2 >= t) {
        p2 = t;
        done = true;
    }
    ramp->framesPosition = p2;
    if (ramp->value < 0) {
        p1 = t - p1;
        p2 = t - p2;
    }

    AudioUnitParameterEvent rampEvent;
    memset(&rampEvent, 0, sizeof(rampEvent));
    rampEvent.scope = scope;
    rampEvent.element = bus;
    rampEvent.parameter = kMultiChannelMixerParam_Volume;
    rampEvent.eventType = kParameterEvent_Ramped;
    rampEvent.eventValues.ramp.startValue = gain * p1 / t;
    rampEvent.eventValues.ramp.endValue = gain * p2 / t;
    rampEvent.eventValues.ramp.durationInFrames = inFrames;
    rampEvent.eventValues.ramp.startBufferOffset = 0;
    AudioUnitScheduleParameters(context->data.mixerUnit, &rampEvent, 1);

    if (done) {
        ramp->value = 0;
    }
    return done;
}

static OSStatus renderNotification(void *userData, AudioUnitRenderActionFlags *flags,
                                    const AudioTimeStamp *timestamp, UInt32 bus,
                                    UInt32 inFrames, AudioBufferList *data) {
    if (*flags & kAudioUnitRenderAction_PreRender) {
        MalContext *context = userData;
        if (atomic_load(&context->data.ramp.value) != 0) {
            MAL_LOCK(context);
            // Double-checked locking
            if (atomic_load(&context->data.ramp.value) != 0) {
                bool done = _malRamp(context, kAudioUnitScope_Output, bus,
                                      inFrames, context->gain, &context->data.ramp);
                if (done && context->active == false && context->data.graph) {
                    Boolean running = false;
                    AUGraphIsRunning(context->data.graph, &running);
                    if (running) {
                        AUGraphStop(context->data.graph);
                    }
                }
            }
            MAL_UNLOCK(context);
        }
    }
    return noErr;
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
    // Do nothing
}

// MARK: Player

static void _malHandleOnFinished(void *userData) {
    uintptr_t userDataInt = (uintptr_t)userData;
    MalCallbackId onFinishedId = (MalCallbackId)userDataInt;
    _malHandleOnFinishedCallback(onFinishedId);
}

static OSStatus _malPlayerRenderCallback(void *userData, AudioUnitRenderActionFlags *flags,
                                         const AudioTimeStamp *timestamp, UInt32 bus,
                                         UInt32 inFrames, AudioBufferList *data) {
    MalPlayer *player = userData;

    MAL_LOCK(player);
    MalPlayerState state = player->data.state;
    if (player->buffer == NULL || player->buffer->managedData == NULL ||
        state == MAL_PLAYER_STATE_STOPPED ||
        player->data.nextFrame >= player->buffer->numFrames) {
        // Silence for end of playback, or because the player is paused.
        for (uint32_t i = 0; i < data->mNumberBuffers; i++) {
            memset(data->mBuffers[i].mData, 0, data->mBuffers[i].mDataByteSize);
        }

        if (state == MAL_PLAYER_STATE_PLAYING ||
            player->buffer == NULL || player->buffer->managedData == NULL) {
            // Stop
            player->data.state = MAL_PLAYER_STATE_STOPPED;
            player->data.nextFrame = 0;

            if (player->context && player->context->data.graph) {
                AUGraphDisconnectNodeInput(player->context->data.graph,
                                           player->data.converterNode,
                                           0);
                Boolean updated;
                AUGraphUpdate(player->context->data.graph, &updated);
            }

            if (state == MAL_PLAYER_STATE_PLAYING) {
                MalCallbackId onFinishedId = atomic_load(&player->onFinishedId);
                if (onFinishedId) {
                    uintptr_t userData = onFinishedId;
                    dispatch_async_f(dispatch_get_main_queue(), (void *)userData,
                                     &_malHandleOnFinished);
                }
            }
        }
    } else {
        const uint32_t numFrames = player->buffer->numFrames;
        const uint32_t frameSize = ((player->buffer->format.bitDepth / 8) *
                                    player->buffer->format.numChannels);
        for (uint32_t i = 0; i < data->mNumberBuffers; i++) {
            uint8_t *dst = data->mBuffers[i].mData;
            uint32_t dstRemaining = data->mBuffers[i].mDataByteSize;

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

                if (player->data.nextFrame >= player->buffer->numFrames) {
                    if (atomic_load(&player->looping)) {
                        player->data.nextFrame = 0;
                        src = player->buffer->managedData;
                    } else {
                        break;
                    }
                }
            }

            if (dstRemaining > 0) {
                // Silence
                memset(dst, 0, dstRemaining);
            }
        }
        if (player->data.ramp.value != 0) {
            bool done = _malRamp(player->context, kAudioUnitScope_Input,
                                  player->data.mixerBus, inFrames, player->gain,
                                  &player->data.ramp);
            if (done && player->data.state == MAL_PLAYER_STATE_PAUSED &&
                player->context && player->context->data.graph) {
                AUGraphDisconnectNodeInput(player->context->data.graph,
                                           player->data.converterNode,
                                           0);
                Boolean updated;
                AUGraphUpdate(player->context->data.graph, &updated);
            }
        }
    }
    MAL_UNLOCK(player);

    return noErr;
}

static bool _malPlayerInitBus(MalPlayer *player) {
    player->data.mixerBus = UINT32_MAX;

    MalContext *context = player->context;
    if (!context || context->data.numBuses == 0) {
        return false;
    }

    // Find a free bus
    int numBuses = context->data.numBuses;
    bool *takenBuses = calloc(numBuses, sizeof(bool));
    if (!takenBuses) {
        return false;
    }
    ok_vec_foreach(&context->players, MalPlayer *currPlayer) {
        if (currPlayer != player) {
            takenBuses[currPlayer->data.mixerBus] = true;
        }
    }
    for (int i = 0; i < numBuses; i++) {
        if (!takenBuses[i]) {
            player->data.mixerBus = i;
            break;
        }
    }
    free(takenBuses);
    if (player->data.mixerBus != UINT32_MAX) {
        return true;
    }

    // Try to increase the number of buses (by 8)
    UInt32 busSize = sizeof(UInt32);
    UInt32 newBusCount = numBuses + 8;
    OSStatus status = AudioUnitSetProperty(context->data.mixerUnit,
                                           kAudioUnitProperty_ElementCount,
                                           kAudioUnitScope_Input,
                                           0,
                                           &newBusCount,
                                           busSize);
    if (status == noErr) {
        context->data.numBuses = newBusCount;
        player->data.mixerBus = numBuses;
        return true;
    } else {
        return false;
    }
}

static bool _malPlayerInit(MalPlayer *player, MalFormat format) {
    if (!_malPlayerInitBus(player)) {
        return false;
    }

    // Create converter node
    AudioComponentDescription converterDesc;
    converterDesc.componentType = kAudioUnitType_FormatConverter;
    converterDesc.componentSubType = kAudioUnitSubType_AUConverter;
    converterDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
    OSStatus status = AUGraphAddNode(player->context->data.graph,
                                     &converterDesc,
                                     &player->data.converterNode);
    if (status != noErr) {
        MAL_LOG("Couldn't add converter node(err %i)", (int)status);
        return false;
    }

    // Get converter audio unit
    status = AUGraphNodeInfo(player->context->data.graph, player->data.converterNode, NULL,
                             &player->data.converterUnit);
    if (status != noErr) {
        MAL_LOG("Couldn't get converter unit (err %i)", (int)status);
        return false;
    }

    // Connect converter to mixer
    status = AUGraphConnectNodeInput(player->context->data.graph,
                                     player->data.converterNode,
                                     0,
                                     player->context->data.mixerNode,
                                     player->data.mixerBus);
    if (status != noErr) {
        MAL_LOG("Couldn't connect converter to mixer (err %i)", (int)status);
        return false;
    }

    // Set stream format
    double sampleRate = (format.sampleRate <= MAL_DEFAULT_SAMPLE_RATE ?
                         malContextGetSampleRate(player->context) : format.sampleRate);

    AudioStreamBasicDescription streamDesc;
    memset(&streamDesc, 0, sizeof(streamDesc));
    streamDesc.mFormatID = kAudioFormatLinearPCM;
    streamDesc.mFramesPerPacket = 1;
    streamDesc.mSampleRate = sampleRate;
    streamDesc.mBitsPerChannel = format.bitDepth;
    streamDesc.mChannelsPerFrame = format.numChannels;
    streamDesc.mBytesPerFrame = (format.bitDepth / 8) * format.numChannels;
    streamDesc.mBytesPerPacket = streamDesc.mBytesPerFrame;
    streamDesc.mFormatFlags = (kLinearPCMFormatFlagIsSignedInteger |
                               kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked);
    status = AudioUnitSetProperty(player->data.converterUnit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &streamDesc,
                                  sizeof(streamDesc));
    if (status != noErr) {
        MAL_LOG("Couldn't set stream format (err %i)", (int)status);
        return false;
    }

    _malPlayerUpdateGain(player);
    return true;
}

static void _malPlayerDidDispose(MalPlayer *player) {
    player->data.converterUnit = NULL;
    player->data.converterNode = 0;
}

static void _malPlayerDispose(MalPlayer *player) {
    if (player->context && player->data.converterNode) {
        AUGraphRemoveNode(player->context->data.graph, player->data.converterNode);
    }
    _malPlayerDidDispose(player);
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    // Do nothing
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    if (player->context && player->data.converterNode) {
        UInt32 outNumInteractions = 0;
        if (AUGraphCountNodeInteractions(player->context->data.graph, player->data.converterNode,
                                         &outNumInteractions) == noErr) {
            // If there are two connections, that means the node 1) has a callback and 2) it is in
            // the graph. Since _malPlayerSetBuffer() can only be called if stopped, that means the
            // graph is not updated. So, force a sync.
            // Without this, this warning message occationally appears in stress_test.c:
            // "[augraph] 2928: DoMakeDisconnection failed with error -10860" 
            if (outNumInteractions == 2) {
                AUGraphUpdate(player->context->data.graph, NULL);
            }
        }
    }
    return true;
}

static void _malPlayerUpdateMute(MalPlayer *player) {
    _malPlayerUpdateGain(player);
}

static void _malPlayerUpdateGain(MalPlayer *player) {
    if (player && player->context && player->context->data.mixerUnit) {
        float totalGain = player->mute ? 0.0f : player->gain;
        OSStatus status = AudioUnitSetParameter(player->context->data.mixerUnit,
                                                kMultiChannelMixerParam_Volume,
                                                kAudioUnitScope_Input,
                                                player->data.mixerBus,
                                                totalGain,
                                                0);
        if (status != noErr) {
            MAL_LOG("Couldn't set volume (err %i)", (int)status);
        }
    }
}

static bool _malPlayerSetLooping(MalPlayer *player, bool looping) {
    (void)player;
    (void)looping;
    // Do nothing
    return true;
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    return player->data.state;
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state) {
    if (!player->context || !player->context->data.graph) {
        return false;
    }

    switch (state) {
        case MAL_PLAYER_STATE_STOPPED:
        default: {
            AUGraphDisconnectNodeInput(player->context->data.graph,
                                       player->data.converterNode,
                                       0);
            Boolean updated;
            AUGraphUpdate(player->context->data.graph, &updated);
            player->data.nextFrame = 0;
            break;
        }
        case MAL_PLAYER_STATE_PAUSED:
            if (player->context->data.canRampInputGain) {
                // Fade out
                double sampleRate = (player->buffer->format.sampleRate <= MAL_DEFAULT_SAMPLE_RATE ?
                                     malContextGetSampleRate(player->context) :
                                     player->buffer->format.sampleRate);
                player->data.ramp.value = -1;
                player->data.ramp.frames = sampleRate * 0.1;
                player->data.ramp.framesPosition = 0;
            } else {
                AUGraphDisconnectNodeInput(player->context->data.graph,
                                           player->data.converterNode,
                                           0);
                Boolean updated;
                AUGraphUpdate(player->context->data.graph, &updated);
            }
            break;
        case MAL_PLAYER_STATE_PLAYING: {
            if (oldState == MAL_PLAYER_STATE_STOPPED) {
                AudioUnitReset(player->context->data.mixerUnit, kAudioUnitScope_Input,
                               player->data.mixerBus);
            }

            AURenderCallbackStruct renderCallback;
            memset(&renderCallback, 0, sizeof(renderCallback));
            renderCallback.inputProc = _malPlayerRenderCallback;
            renderCallback.inputProcRefCon = player;
            AUGraphSetNodeInputCallback(player->context->data.graph,
                                        player->data.converterNode,
                                        0,
                                        &renderCallback);
            Boolean updated;
            AUGraphUpdate(player->context->data.graph, &updated);
            if (oldState == MAL_PLAYER_STATE_PAUSED &&
                player->context->data.canRampInputGain) {
                // Fade in
                double sampleRate = (player->buffer->format.sampleRate <= MAL_DEFAULT_SAMPLE_RATE ?
                                     malContextGetSampleRate(player->context) :
                                     player->buffer->format.sampleRate);
                player->data.ramp.value = 1;
                player->data.ramp.frames = sampleRate * 0.05;
                player->data.ramp.framesPosition = 0;
            }
            break;
        }
    }
    player->data.state = state;
    return true;
}

#endif
