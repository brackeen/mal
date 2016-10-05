/*
 mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2016 David Brackeen
 
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

#ifndef _MAL_AUDIO_COREAUDIO_H_
#define _MAL_AUDIO_COREAUDIO_H_

#include "mal.h"
#include <AudioToolbox/AudioToolbox.h>

struct _ramp {
    int value; // -1 for fade out, 1 for fade in, 0 for no fade
    uint32_t frames;
    uint32_t frames_position;
};

struct _mal_context {
    AUGraph graph;
    AudioUnit mixer_unit;
    AUNode mixer_node;

    bool first_time;

    uint32_t num_buses;
    bool can_ramp_input_gain;
    bool can_ramp_output_gain;
    struct _ramp ramp;
};

struct _mal_buffer {

};

struct _mal_player {
    uint32_t input_bus;

    uint32_t next_frame;
    mal_player_state state;

    struct _ramp ramp;
};

#define MAL_USE_MUTEX
#include "mal_audio_abstract.h"

// MARK: Context

static OSStatus render_notification(void *user_data, AudioUnitRenderActionFlags *flags,
                                    const AudioTimeStamp *timestamp, UInt32 bus,
                                    UInt32 in_frames, AudioBufferList *data);

static bool _mal_context_init(mal_context *context) {
    context->data.first_time = true;
    context->active = false;

    // Create audio graph
    OSStatus status = NewAUGraph(&context->data.graph);
    if (status != noErr) {
        MAL_LOG("Couldn't create audio graph (err %i)", (int)status);
        return false;
    }

    // Create output node
    AUNode output_node;
    AudioComponentDescription output_desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };
    status = AUGraphAddNode(context->data.graph, &output_desc, &output_node);
    if (status != noErr) {
        MAL_LOG("Couldn't create output node (err %i)", (int)status);
        return false;
    }

    // Create mixer node
    AudioComponentDescription mixer_desc = {
        .componentType = kAudioUnitType_Mixer,
        .componentSubType = kAudioUnitSubType_MultiChannelMixer,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };
    status = AUGraphAddNode(context->data.graph, &mixer_desc, &context->data.mixer_node);
    if (status != noErr) {
        MAL_LOG("Couldn't create mixer node (err %i)", (int)status);
        return false;
    }

    // Connect a node's output to a node's input
    status = AUGraphConnectNodeInput(context->data.graph, context->data.mixer_node, 0,
                                     output_node, 0);
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

    // Get mixer
    status = AUGraphNodeInfo(context->data.graph, context->data.mixer_node, NULL,
                             &context->data.mixer_unit);
    if (status != noErr) {
        MAL_LOG("Couldn't get mixer unit (err %i)", (int)status);
        return false;
    }

    // Set output sample rate
    if (context->sample_rate > 0) {
        status = AudioUnitSetProperty(context->data.mixer_unit,
                                      kAudioUnitProperty_SampleRate,
                                      kAudioUnitScope_Output,
                                      0,
                                      &context->sample_rate,
                                      sizeof(context->sample_rate));
        if (status != noErr) {
            MAL_LOG("Ignoring: Couldn't set output sample rate (err %i)", (int)status);
        }
    }

    // Check if input gain can ramp
    AudioUnitParameterInfo parameter_info;
    UInt32 parameter_info_size = sizeof(AudioUnitParameterInfo);
    status = AudioUnitGetProperty(context->data.mixer_unit, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Input, kMultiChannelMixerParam_Volume,
                                  &parameter_info, &parameter_info_size);
    if (status == noErr) {
        context->data.can_ramp_input_gain =
            (parameter_info.flags & kAudioUnitParameterFlag_CanRamp) != 0;
    } else {
        context->data.can_ramp_input_gain = false;
    }

    // Check if output gain can ramp
    status = AudioUnitGetProperty(context->data.mixer_unit, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Output, kMultiChannelMixerParam_Volume,
                                  &parameter_info, &parameter_info_size);
    if (status == noErr) {
        context->data.can_ramp_output_gain =
            (parameter_info.flags & kAudioUnitParameterFlag_CanRamp) != 0;
    } else {
        context->data.can_ramp_output_gain = false;
    }
    if (context->data.can_ramp_output_gain) {
        status = AudioUnitAddRenderNotify(context->data.mixer_unit, render_notification, context);
        if (status != noErr) {
            context->data.can_ramp_output_gain = false;
        }
    }

    // Get bus count
    UInt32 bus_size = sizeof(context->data.num_buses);
    status = AudioUnitGetProperty(context->data.mixer_unit,
                                  kAudioUnitProperty_ElementCount,
                                  kAudioUnitScope_Input,
                                  0,
                                  &context->data.num_buses,
                                  &bus_size);
    if (status != noErr) {
        MAL_LOG("Couldn't get mixer unit (err %i)", (int)status);
        return false;
    }

    // Set output volume
    _mal_context_set_gain(context, context->gain);

    // Init
    status = AUGraphInitialize(context->data.graph);
    if (status != noErr) {
        MAL_LOG("Couldn't init graph (err %i)", (int)status);
        return false;
    }

#ifdef MAL_DEBUG_LOG
    CAShow(context->data.graph);
#endif

    return true;
}

static void _mal_context_dispose(mal_context *context) {
    context->active = false;
    if (context->data.graph) {
        AUGraphStop(context->data.graph);
        AUGraphUninitialize(context->data.graph);
        DisposeAUGraph(context->data.graph);
        context->data.graph = NULL;
        context->data.mixer_unit = NULL;
    }
}

static void _mal_context_reset(mal_context *context) {
    bool active = context->active;
    for (int i = 0; i < context->players.length; i++) {
        mal_player *player = context->players.values[i];
        _mal_player_dispose(player);
    }
    MAL_LOCK(context);
    _mal_context_dispose(context);
    _mal_context_init(context);
    _mal_context_set_mute(context, context->mute);
    _mal_context_set_gain(context, context->gain);
    MAL_UNLOCK(context);
    mal_context_set_active(context, active);
    for (int i = 0; i < context->players.length; i++) {
        mal_player *player = context->players.values[i];
        bool success = _mal_player_init(player);
        if (!success) {
            MAL_LOG("Couldn't reset player %i", i);
        } else {
            _mal_player_set_mute(player, player->mute);
            _mal_player_set_gain(player, player->gain);
            _mal_player_set_format(player, player->format);
            if (player->data.state == MAL_PLAYER_STATE_PLAYING) {
                player->data.state = MAL_PLAYER_STATE_STOPPED;
                mal_player_set_state(player, MAL_PLAYER_STATE_PLAYING);
            }
        }
    }
}

static void _mal_context_set_active(mal_context *context, bool active) {
    if (context->active != active) {
        OSStatus status;
        context->active = active;
        if (active) {
            Boolean running = false;
            AUGraphIsRunning(context->data.graph, &running);
            if (running) {
                // Hasn't called AUGraphStop yet - do nothing
                context->data.ramp.value = 0;
                status = noErr;
            } else {
                if (!context->data.first_time && context->data.can_ramp_output_gain) {
                    // Fade in
                    context->data.ramp.value = 1;
                    context->data.ramp.frames = 4096;
                    context->data.ramp.frames_position = 0;
                }
                status = AUGraphStart(context->data.graph);
                context->data.first_time = false;
            }
        } else {
            if (context->data.can_ramp_output_gain) {
                // Fade out
                context->data.ramp.value = -1;
                context->data.ramp.frames = 4096;
                context->data.ramp.frames_position = 0;
                status = noErr;
            } else {
                status = AUGraphStop(context->data.graph);
            }
        }
        if (status != noErr) {
            MAL_LOG("Couldn't %s graph (err %i)", (active ? "start" : "stop"), (int)status);
        }
    }
}

static void _mal_context_set_mute(mal_context *context, bool mute) {
    _mal_context_set_gain(context, context->gain);
}

static void _mal_context_set_gain(mal_context *context, float gain) {
    float total_gain = context->mute ? 0.0f : gain;
    OSStatus status = AudioUnitSetParameter(context->data.mixer_unit,
                                            kMultiChannelMixerParam_Volume,
                                            kAudioUnitScope_Output,
                                            0,
                                            total_gain,
                                            0);

    if (status != noErr) {
        MAL_LOG("Couldn't set volume (err %i)", (int)status);
    }
}

static bool _mal_ramp(mal_context *context, AudioUnitScope scope, AudioUnitElement bus,
                      uint32_t in_frames, double gain, struct _ramp *ramp) {
    uint32_t t = ramp->frames;
    uint32_t p1 = ramp->frames_position;
    uint32_t p2 = p1 + in_frames;
    bool done = false;
    if (p2 >= t) {
        p2 = t;
        done = true;
    }
    ramp->frames_position = p2;
    if (ramp->value < 0) {
        p1 = t - p1;
        p2 = t - p2;
    }

    AudioUnitParameterEvent ramp_event;
    memset(&ramp_event, 0, sizeof(ramp_event));
    ramp_event.scope = scope;
    ramp_event.element = bus;
    ramp_event.parameter = kMultiChannelMixerParam_Volume;
    ramp_event.eventType = kParameterEvent_Ramped;
    ramp_event.eventValues.ramp.startValue = gain * p1 / t;
    ramp_event.eventValues.ramp.endValue = gain * p2 / t;
    ramp_event.eventValues.ramp.durationInFrames = in_frames;
    ramp_event.eventValues.ramp.startBufferOffset = 0;
    AudioUnitScheduleParameters(context->data.mixer_unit, &ramp_event, 1);

    if (done) {
        ramp->value = 0;
    }
    return done;
}

static OSStatus render_notification(void *user_data, AudioUnitRenderActionFlags *flags,
                                    const AudioTimeStamp *timestamp, UInt32 bus,
                                    UInt32 in_frames, AudioBufferList *data) {
    if (*flags & kAudioUnitRenderAction_PreRender) {
        mal_context *context = user_data;
        if (context->data.ramp.value != 0) {
            MAL_LOCK(context);
            // Double-checked locking
            if (context->data.ramp.value != 0) {
                bool done = _mal_ramp(context, kAudioUnitScope_Output, bus,
                                      in_frames, context->gain, &context->data.ramp);
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
};

// MARK: Buffer

static bool _mal_buffer_init(mal_context *context, mal_buffer *buffer,
                             const void *copied_data, void *managed_data,
                             const mal_deallocator data_deallocator) {
    if (managed_data) {
        buffer->managed_data = managed_data;
        buffer->managed_data_deallocator = data_deallocator;
    } else {
        const size_t data_length = ((buffer->format.bit_depth / 8) *
                                    buffer->format.num_channels * buffer->num_frames);
        void *new_buffer = malloc(data_length);
        if (!new_buffer) {
            return false;
        }
        memcpy(new_buffer, copied_data, data_length);
        buffer->managed_data = new_buffer;
        buffer->managed_data_deallocator = free;
    }
    return true;
}

static void _mal_buffer_dispose(mal_buffer *buffer) {
    // Do nothing
}

// MARK: Player

static OSStatus audio_render_callback(void *user_data, AudioUnitRenderActionFlags *flags,
                                      const AudioTimeStamp *timestamp, UInt32 bus,
                                      UInt32 in_frames, AudioBufferList *data) {
    mal_player *player = user_data;

    MAL_LOCK(player);
    if (player->buffer == NULL || player->buffer->managed_data == NULL ||
        player->data.state == MAL_PLAYER_STATE_STOPPED ||
        player->data.next_frame >= player->buffer->num_frames) {
        // Silence for end of playback, or because the player is paused.
        for (int i = 0; i < data->mNumberBuffers; i++) {
            memset(data->mBuffers[i].mData, 0, data->mBuffers[i].mDataByteSize);
        }

        if (player->data.state == MAL_PLAYER_STATE_PLAYING ||
            player->buffer == NULL || player->buffer->managed_data == NULL) {
            // Stop
            player->data.state = MAL_PLAYER_STATE_STOPPED;
            player->data.next_frame = 0;

            if (player->context && player->context->data.graph) {
                AUGraphDisconnectNodeInput(player->context->data.graph,
                                           player->context->data.mixer_node,
                                           player->data.input_bus);
            }
        }
    } else {
        const uint32_t num_frames = player->buffer->num_frames;
        const uint32_t frame_size = ((player->buffer->format.bit_depth / 8) *
                                     player->buffer->format.num_channels);
        for (int i = 0; i < data->mNumberBuffers; i++) {
            void *dst = data->mBuffers[i].mData;
            uint32_t dst_remaining = data->mBuffers[i].mDataByteSize;

            void *src = player->buffer->managed_data + player->data.next_frame * frame_size;
            while (dst_remaining > 0) {
                uint32_t player_frames = num_frames - player->data.next_frame;
                uint32_t max_frames = dst_remaining / frame_size;
                uint32_t copy_frames = player_frames < max_frames ? player_frames : max_frames;
                uint32_t copy_bytes = copy_frames * frame_size;

                if (copy_bytes == 0) {
                    break;
                }

                memcpy(dst, src, copy_bytes);
                player->data.next_frame += copy_frames;
                dst += copy_bytes;
                src += copy_bytes;
                dst_remaining -= copy_bytes;

                if (player->data.next_frame >= player->buffer->num_frames) {
                    if (player->looping) {
                        player->data.next_frame = 0;
                        src = player->buffer->managed_data;
                    } else {
                        break;
                    }
                }
            }

            if (dst_remaining > 0) {
                // Silence
                memset(dst, 0, dst_remaining);
            }
        }
        if (player->data.ramp.value != 0) {
            bool done = _mal_ramp(player->context, kAudioUnitScope_Input, player->data.input_bus,
                                  in_frames, player->gain, &player->data.ramp);
            if (done && player->data.state == MAL_PLAYER_STATE_PAUSED &&
                player->context && player->context->data.graph) {
                AUGraphDisconnectNodeInput(player->context->data.graph,
                                           player->context->data.mixer_node,
                                           player->data.input_bus);
                Boolean updated;
                AUGraphUpdate(player->context->data.graph, &updated);
            }
        }
    }
    MAL_UNLOCK(player);

    return noErr;
}

static bool _mal_player_init(mal_player *player) {
    player->data.input_bus = UINT32_MAX;

    mal_context *context = player->context;
    if (!context || context->data.num_buses == 0) {
        return false;
    }

    // Find a free bus
    int num_buses = context->data.num_buses;
    bool *taken_buses = calloc(num_buses, sizeof(bool));
    if (!taken_buses) {
        return false;
    }
    for (int i = 0; i < context->players.length; i++) {
        mal_player *curr_player = context->players.values[i];
        if (curr_player != player) {
            taken_buses[curr_player->data.input_bus] = true;
        }
    }
    for (int i = 0; i < num_buses; i++) {
        if (!taken_buses[i]) {
            player->data.input_bus = i;
            break;
        }
    }
    free(taken_buses);
    if (player->data.input_bus != UINT32_MAX) {
        return true;
    }

    // Try to increase the number of buses (by 8)
    UInt32 bus_size = sizeof(UInt32);
    UInt32 new_bus_count = num_buses + 8;
    OSStatus status = AudioUnitSetProperty(context->data.mixer_unit,
                                           kAudioUnitProperty_ElementCount,
                                           kAudioUnitScope_Input,
                                           0,
                                           &new_bus_count,
                                           bus_size);
    if (status == noErr) {
        context->data.num_buses = new_bus_count;
        player->data.input_bus = num_buses;
        return true;
    } else {
        return false;
    }
}

static void _mal_player_dispose(mal_player *player) {
    // Do nothing
}

static bool _mal_player_set_format(mal_player *player, mal_format format) {
    if (!player->context) {
        return false;
    }

    AudioStreamBasicDescription stream_desc;
    memset(&stream_desc, 0, sizeof(stream_desc));
    stream_desc.mFormatID = kAudioFormatLinearPCM;
    stream_desc.mFramesPerPacket = 1;
    stream_desc.mSampleRate = format.sample_rate;
    stream_desc.mBitsPerChannel = format.bit_depth;
    stream_desc.mChannelsPerFrame = format.num_channels;
    stream_desc.mBytesPerFrame = (format.bit_depth / 8) * format.num_channels;
    stream_desc.mBytesPerPacket = stream_desc.mBytesPerFrame;
    stream_desc.mFormatFlags = (kLinearPCMFormatFlagIsSignedInteger |
                                kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked);
    OSStatus status = AudioUnitSetProperty(player->context->data.mixer_unit,
                                           kAudioUnitProperty_StreamFormat,
                                           kAudioUnitScope_Input,
                                           player->data.input_bus,
                                           &stream_desc,
                                           sizeof(stream_desc));
    if (status != noErr) {
        MAL_LOG("Couldn't set stream format (err %i)", (int)status);
        return false;
    }

    return true;
}

static bool _mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    // Do nothing
    return true;
}

static void _mal_player_set_mute(mal_player *player, bool mute) {
    _mal_player_set_gain(player, player->gain);
}

static void _mal_player_set_gain(mal_player *player, float gain) {
    if (player && player->context && player->context->data.mixer_unit) {
        float total_gain = player->mute ? 0.0f : gain;
        OSStatus status = AudioUnitSetParameter(player->context->data.mixer_unit,
                                                kMultiChannelMixerParam_Volume,
                                                kAudioUnitScope_Input,
                                                player->data.input_bus,
                                                total_gain,
                                                0);
        if (status != noErr) {
            MAL_LOG("Couldn't set volume (err %i)", (int)status);
        }
    }
}

static void _mal_player_set_looping(mal_player *player, bool looping) {
    // Do nothing
}

static mal_player_state _mal_player_get_state(const mal_player *player) {
    return player->data.state;
}

static bool _mal_player_set_state(mal_player *player, mal_player_state state) {
    if (!player->context || !player->context->data.graph) {
        return false;
    }

    switch (state) {
        case MAL_PLAYER_STATE_STOPPED:
        default: {
            AUGraphDisconnectNodeInput(player->context->data.graph,
                                       player->context->data.mixer_node,
                                       player->data.input_bus);
            Boolean updated;
            AUGraphUpdate(player->context->data.graph, &updated);
            player->data.next_frame = 0;
            break;
        }
        case MAL_PLAYER_STATE_PAUSED:
            if (player->context->data.can_ramp_input_gain) {
                // Fade out
                player->data.ramp.value = -1;
                player->data.ramp.frames = player->buffer->format.sample_rate * 0.1;
                player->data.ramp.frames_position = 0;
            } else {
                AUGraphDisconnectNodeInput(player->context->data.graph,
                                           player->context->data.mixer_node,
                                           player->data.input_bus);
                Boolean updated;
                AUGraphUpdate(player->context->data.graph, &updated);
            }
            break;
        case MAL_PLAYER_STATE_PLAYING: {
            if (player->data.state == MAL_PLAYER_STATE_STOPPED) {
                AudioUnitReset(player->context->data.mixer_unit, kAudioUnitScope_Input, player->data.input_bus);
            }

            AURenderCallbackStruct render_callback;
            memset(&render_callback, 0, sizeof(render_callback));
            render_callback.inputProc = audio_render_callback;
            render_callback.inputProcRefCon = player;
            AUGraphSetNodeInputCallback(player->context->data.graph,
                                        player->context->data.mixer_node,
                                        player->data.input_bus,
                                        &render_callback);
            Boolean updated;
            AUGraphUpdate(player->context->data.graph, &updated);
            if (player->data.state == MAL_PLAYER_STATE_PAUSED &&
                player->context->data.can_ramp_input_gain) {
                // Fade in
                player->data.ramp.value = 1;
                player->data.ramp.frames = player->buffer->format.sample_rate * 0.05;
                player->data.ramp.frames_position = 0;
            }
            break;
        }
    }
    player->data.state = state;
    return true;
}

#endif
