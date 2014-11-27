/*
 mal
 Copyright (c) 2014 David Brackeen
 
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

#if defined(ANDROID)

#include <android/log.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <math.h>
#include "mal.h"
#include "mal_vector.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "MAL", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "MAL", __VA_ARGS__))

#define kNumQueuedBuffers 2

struct mal_context {
    mal_vector players;
    mal_vector buffers;
    bool routes[NUM_MAL_ROUTES];
    float gain;
    bool mute;
    bool active;
    
    SLObjectItf sl_object;
    SLEngineItf sl_engine;
    SLObjectItf sl_output_mix_object;
};

struct mal_buffer {
    mal_context *context;
    mal_format format;
    uint32_t num_frames;
    void *managed_data;
    mal_deallocator managed_data_deallocator;
};

struct mal_player {
    mal_context *context;
    mal_format format;
    const mal_buffer *buffer;
    float gain;
    bool mute;
    bool looping;
    
    SLObjectItf sl_object;
    SLPlayItf sl_play;
    SLVolumeItf sl_volume;
    SLAndroidSimpleBufferQueueItf sl_buffer_queue;
};

// Deletes OpenSL resources without deleting other info
static void mal_player_cleanup(mal_player *player);
static void mal_buffer_cleanup(mal_buffer *buffer);

static void mal_player_update_gain(mal_player *player);
static bool mal_player_reset(mal_player *player, const mal_format format);

// MARK: Context

mal_context *mal_context_create(const double output_sample_rate) {
    mal_context *context = calloc(1, sizeof(mal_context));
    if (context != NULL) {
        context->mute = false;
        context->gain = 1.0f;
        context->active = true;
        
        // Create engine
        SLresult result = slCreateEngine(&context->sl_object, 0, NULL, 0, NULL, NULL);
        if (result != SL_RESULT_SUCCESS) {
            mal_context_free(context);
            return NULL;
        }
        
        // Realize engine
        result = (*context->sl_object)->Realize(context->sl_object, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            mal_context_free(context);
            return NULL;
        }
        
        // Get engine interface
        result = (*context->sl_object)->GetInterface(context->sl_object, SL_IID_ENGINE, &context->sl_engine);
        if (result != SL_RESULT_SUCCESS) {
            mal_context_free(context);
            return NULL;
        }
        
        // Get output mix
        result = (*context->sl_engine)->CreateOutputMix(context->sl_engine,
                                                        &context->sl_output_mix_object, 0, NULL, NULL);
        if (result != SL_RESULT_SUCCESS) {
            mal_context_free(context);
            return NULL;
        }
        
        // Realize the output mix
        result = (*context->sl_output_mix_object)->Realize(context->sl_output_mix_object, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            mal_context_free(context);
            return NULL;
        }
        
        // NOTE: SLAudioIODeviceCapabilitiesItf isn't supported, so there's no way to get routing information.
        // Also, GetDestinationOutputDeviceIDs only returns SL_DEFAULTDEVICEID_AUDIOOUTPUT.
        //
        // Potentially, if we have access to the ANativeActivity, we could get an instance of
        // AudioManager and call the Java functions:
        // if (isBluetoothA2dpOn() or isBluetoothScoOn()) then wireless
        // else if (isSpeakerphoneOn()) then speaker
        // else headset
        //
    }
    return context;
}

void mal_context_set_active(mal_context *context, const bool active) {
    if (context != NULL && context->active != active) {
        context->active = active;
        
        // From http://mobilepearls.com/labs/native-android-api/ndk/docs/opensles/
        // "Be sure to destroy your audio players when your activity is
        // paused, as they are a global resource shared with other apps."
        for (unsigned int i = 0; i < context->players.length; i++) {
            mal_player *player = context->players.values[i];
            
            if (active) {
                if (player->sl_object == NULL) {
                    mal_player_reset(player, player->format);
                }
            }
            else {
                mal_player_state state = mal_player_get_state(player);
                if (state == MAL_PLAYER_STATE_PLAYING) {
                    mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
                }
                else if (state == MAL_PLAYER_STATE_STOPPED) {
                    mal_player_cleanup(player);
                }
            }
        }
    }
}

static void mal_context_update_gain(mal_context *context) {
    if (context != NULL) {
        for (unsigned int i = 0; i < context->players.length; i++) {
            mal_player_update_gain(context->players.values[i]);
        }
    }
}

bool mal_context_get_mute(const mal_context *context) {
    return (context == NULL) ? false : context->mute;
}

void mal_context_set_mute(mal_context *context, const bool mute) {
    if (context != NULL) {
        if (context->mute != mute) {
            context->mute = mute;
            mal_context_update_gain(context);
        }
    }
}

float mal_context_get_gain(const mal_context *context) {
    return (context == NULL) ? 1.0f : context->gain;
}

void mal_context_set_gain(mal_context *context, const float gain) {
    if (context != NULL) {
        if (context->gain != gain) {
            context->gain = gain;
            mal_context_update_gain(context);
        }
    }
}

bool mal_context_format_is_valid(const mal_context *context, const mal_format format) {
    return ((format.bit_depth == 8 || format.bit_depth == 16) &&
            (format.num_channels == 1 || format.num_channels == 2) &&
            format.sample_rate > 0);
}

bool mal_context_is_route_enabled(const mal_context *context, const mal_route route) {
    if (context != NULL && route < NUM_MAL_ROUTES) {
        return context->routes[route];
    }
    else {
        return false;
    }
}

void mal_context_free(mal_context *context) {
    if (context != NULL) {
        // Delete players
        for (unsigned int i = 0; i < context->players.length; i++) {
            mal_player *player = context->players.values[i];
            mal_player_cleanup(player);
            player->context = NULL;
        }
        mal_vector_free(&context->players);
        
        // Delete buffers
        for (unsigned int i = 0; i < context->buffers.length; i++) {
            mal_buffer *buffer = context->buffers.values[i];
            mal_buffer_cleanup(buffer);
            buffer->context = NULL;
        }
        mal_vector_free(&context->buffers);
        
        // Delete OpenSL objects
        if (context->sl_output_mix_object != NULL) {
            (*context->sl_output_mix_object)->Destroy(context->sl_output_mix_object);
            context->sl_output_mix_object = NULL;
        }
        if (context->sl_object != NULL) {
            (*context->sl_object)->Destroy(context->sl_object);
            context->sl_object = NULL;
            context->sl_engine = NULL;
        }
        free(context);
    }
}

bool mal_formats_equal(const mal_format format1, const mal_format format2) {
    return (format1.bit_depth == format2.bit_depth &&
            format1.num_channels == format2.num_channels &&
            format1.sample_rate == format2.sample_rate);
}

// MARK: Buffer

mal_buffer *mal_buffer_create(mal_context *context, const mal_format format,
                              const uint32_t num_frames, const void *data) {
    // Check params
    if (context == NULL || !mal_context_format_is_valid(context, format) || num_frames == 0 || data == NULL) {
        return NULL;
    }
    // Copy data
    size_t len = num_frames * (format.bit_depth/8) * format.num_channels;
    void *managed_data = malloc(len);
    if (managed_data == NULL) {
        return NULL;
    }
    memcpy(managed_data, data, len);
    mal_buffer *buffer = mal_buffer_create_no_copy(context, format, num_frames, managed_data, free);
    if (buffer == NULL) {
        free(managed_data);
    }
    return buffer;
}

mal_buffer *mal_buffer_create_no_copy(mal_context *context, const mal_format format,
                                      const uint32_t num_frames, void *managed_data,
                                      const mal_deallocator data_deallocator) {
    // Check params
    if (context == NULL || !mal_context_format_is_valid(context, format) || num_frames == 0 || managed_data == NULL) {
        return NULL;
    }
    mal_buffer *buffer = calloc(1, sizeof(mal_buffer));
    if (buffer != NULL) {
        mal_vector_add(&context->buffers, buffer);
        buffer->context = context;
        buffer->format = format;
        buffer->num_frames = num_frames;
        buffer->managed_data = managed_data;
        buffer->managed_data_deallocator = data_deallocator;
    }
    return buffer;
}

mal_format mal_buffer_get_format(const mal_buffer *buffer) {
    if (buffer == NULL) {
        mal_format null_format = { 0, 0, 0 };
        return null_format;
    }
    else {
        return buffer->format;
    }
}

uint32_t mal_buffer_get_num_frames(const mal_buffer *buffer) {
    return (buffer == NULL) ? 0 : buffer->num_frames;
}

void *mal_buffer_get_data(const mal_buffer *buffer) {
    return (buffer == NULL) ? NULL : buffer->managed_data;
}

static void mal_buffer_cleanup(mal_buffer *buffer) {
    // Stop all players that are using this buffer.
    if (buffer->context != NULL) {
        for (unsigned int i = 0; i < buffer->context->players.length; i++) {
            mal_player *player = buffer->context->players.values[i];
            if (player->buffer == buffer) {
                mal_player_set_buffer(player, NULL);
            }
        }
    }
}

void mal_buffer_free(mal_buffer *buffer) {
    if (buffer != NULL) {
        mal_buffer_cleanup(buffer);
        if (buffer->context != NULL) {
            mal_vector_remove(&buffer->context->buffers, buffer);
            buffer->context = NULL;
        }
        if (buffer->managed_data != NULL) {
            if (buffer->managed_data_deallocator != NULL) {
                buffer->managed_data_deallocator(buffer->managed_data);
            }
            buffer->managed_data = NULL;
        }
        free(buffer);
    }
}


// MARK: Player

static void mal_player_cleanup(mal_player *player) {
    if (player != NULL) {
        if (player->buffer != NULL) {
            mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
            player->buffer = NULL;
            if (player->sl_buffer_queue != NULL) {
                (*player->sl_buffer_queue)->Clear(player->sl_buffer_queue);
            }
        }
        if (player->sl_object != NULL) {
            (*player->sl_object)->Destroy(player->sl_object);
            player->sl_object = NULL;
            player->sl_buffer_queue = NULL;
            player->sl_play = NULL;
            player->sl_volume = NULL;
        }
    }
}

// TODO: This callback happens on a different thread, and needs to be thread-safe.
static void mal_buffer_queue_callback(SLAndroidSimpleBufferQueueItf queue, void *void_player) {
    const mal_player *player = void_player;
    if (player->looping && queue != NULL && player->buffer != NULL &&
        player->buffer->managed_data != NULL && mal_player_get_state(player) == MAL_PLAYER_STATE_PLAYING) {
        const mal_buffer *buffer = player->buffer;
        const size_t len = buffer->num_frames * (buffer->format.bit_depth/8) * buffer->format.num_channels;
        (*queue)->Enqueue(queue, buffer->managed_data, len);
    }
    else if (player->sl_play != NULL) {
        (*player->sl_play)->SetPlayState(player->sl_play, SL_PLAYSTATE_STOPPED);
    }
}

static void mal_player_update_gain(mal_player *player) {
    if (player != NULL && player->context != NULL && player->sl_volume != NULL) {
        float gain = 0;
        if (!player->context->mute && !player->mute) {
            gain = player->context->gain * player->gain;
        }
        if (gain <= 0) {
            (*player->sl_volume)->SetMute(player->sl_volume, SL_BOOLEAN_TRUE);
        }
        else {
            SLmillibel millibelVolume = roundf(2000 * log10f(gain));
            if (millibelVolume < SL_MILLIBEL_MIN) {
                millibelVolume = SL_MILLIBEL_MIN;
            }
            else if (millibelVolume > 0) {
                millibelVolume = 0;
            }
            (*player->sl_volume)->SetVolumeLevel(player->sl_volume, millibelVolume);
            (*player->sl_volume)->SetMute(player->sl_volume, SL_BOOLEAN_FALSE);
        }
    }
}

static bool mal_player_reset(mal_player *player, const mal_format format) {
    if (player != NULL) {
        if (player->sl_object != NULL) {
            (*player->sl_object)->Destroy(player->sl_object);
            player->sl_object = NULL;
            player->sl_buffer_queue = NULL;
            player->sl_play = NULL;
            player->sl_volume = NULL;
        }
        
        const int n = 1;
        const bool system_is_little_endian = *(char *)&n == 1;
        
        SLDataLocator_AndroidSimpleBufferQueue sl_buffer_queue;
        sl_buffer_queue.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
        sl_buffer_queue.numBuffers = kNumQueuedBuffers;
        
        SLDataFormat_PCM sl_format;
        sl_format.formatType = SL_DATAFORMAT_PCM;
        sl_format.numChannels = format.num_channels;
        sl_format.samplesPerSec = (format.sample_rate * 1000);
        sl_format.bitsPerSample = format.bit_depth == 8 ?  SL_PCMSAMPLEFORMAT_FIXED_8 : SL_PCMSAMPLEFORMAT_FIXED_16;
        sl_format.containerSize = format.bit_depth == 8 ?  SL_PCMSAMPLEFORMAT_FIXED_8 : SL_PCMSAMPLEFORMAT_FIXED_16;
        sl_format.channelMask = (format.num_channels == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) :
                                 SL_SPEAKER_FRONT_CENTER);
        sl_format.endianness = system_is_little_endian ? SL_BYTEORDER_LITTLEENDIAN : SL_BYTEORDER_BIGENDIAN;
        
        SLDataSource sl_data_source = { &sl_buffer_queue, &sl_format };
        SLDataLocator_OutputMix sl_output_mix = { SL_DATALOCATOR_OUTPUTMIX, player->context->sl_output_mix_object };
        SLDataSink sl_audio_sink = { &sl_output_mix, NULL };
        
        // Create the player
        const SLInterfaceID ids[2] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME };
        const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
        SLresult result = (*player->context->sl_engine)->CreateAudioPlayer(player->context->sl_engine,
                                                                           &player->sl_object,
                                                                           &sl_data_source, &sl_audio_sink,
                                                                           2, ids, req);
        if (result != SL_RESULT_SUCCESS) {
            player->sl_object = NULL;
            return false;
        }
        
        // Realize the player
        result = (*player->sl_object)->Realize(player->sl_object, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            mal_player_cleanup(player);
            return false;
        }
        
        // Get the play interface
        result = (*player->sl_object)->GetInterface(player->sl_object, SL_IID_PLAY, &player->sl_play);
        if (result != SL_RESULT_SUCCESS) {
            mal_player_cleanup(player);
            return false;

        }
        
        // Get buffer queue interface
        result = (*player->sl_object)->GetInterface(player->sl_object, SL_IID_BUFFERQUEUE, &player->sl_buffer_queue);
        if (result != SL_RESULT_SUCCESS) {
            mal_player_cleanup(player);
            return false;
        }
        
        // Register buffer queue callback
        result = (*player->sl_buffer_queue)->RegisterCallback(player->sl_buffer_queue,
                                                              mal_buffer_queue_callback, player);
        if (result != SL_RESULT_SUCCESS) {
            mal_player_cleanup(player);
            return false;
        }
        
        // Get the volume interface (optional)
        result = (*player->sl_object)->GetInterface(player->sl_object, SL_IID_VOLUME, &player->sl_volume);
        if (result != SL_RESULT_SUCCESS) {
            player->sl_volume = NULL;
        }
        
        player->format = format;
        mal_player_update_gain(player);
        return true;
    }
    return false;
}

mal_player *mal_player_create(mal_context *context, const mal_format format) {
    // Check params
    if (context == NULL || !mal_context_format_is_valid(context, format)) {
        return NULL;
    }
    mal_player *player = calloc(1, sizeof(mal_player));
    if (player != NULL) {
        mal_vector_add(&context->players, player);
        player->context = context;
        
        bool success = mal_player_reset(player, format);
        if (!success) {
            mal_player_free(player);
            return NULL;
        }
    }
    return player;
}

mal_format mal_player_get_format(const mal_player *player) {
    if (player == NULL) {
        mal_format null_format = { 0, 0, 0 };
        return null_format;
    }
    else {
        return player->format;
    }
}

bool mal_player_set_format(mal_player *player, const mal_format format) {
    if (player != NULL && mal_context_format_is_valid(player->context, format)) {
        mal_player_cleanup(player);
        return mal_player_reset(player, format);
    }
    else {
        return false;
    }
}

bool mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    if (player == NULL) {
        return false;
    }
    else {
        // Stop and clear
        if (player->buffer != NULL) {
            mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
            player->buffer = NULL;
            if (player->sl_buffer_queue != NULL) {
                (*player->sl_buffer_queue)->Clear(player->sl_buffer_queue);
            }
        }
        if (player->sl_object == NULL) {
            bool success = mal_player_reset(player, player->format);
            if (!success) {
                return false;
            }
        }
        if (buffer == NULL) {
            return true;
        }
        else {
            // Check if format valid
            if (!mal_context_format_is_valid(player->context, buffer->format)) {
                return false;
            }

            player->buffer = buffer;
            return true;
        }
    }
}

const mal_buffer *mal_player_get_buffer(const mal_player *player) {
    return player == NULL ? NULL : player->buffer;
}

bool mal_player_get_mute(const mal_player *player) {
    return player != NULL && player->mute;
}

void mal_player_set_mute(mal_player *player, const bool mute) {
    if (player != NULL) {
        if (player->mute != mute) {
            player->mute = mute;
            mal_player_update_gain(player);
        }
    }
}

float mal_player_get_gain(const mal_player *player) {
    return (player == NULL) ? 1.0f : player->gain;
}

void mal_player_set_gain(mal_player *player, const float gain) {
    if (player != NULL) {
        if (player->gain != gain) {
            player->gain = gain;
            mal_player_update_gain(player);
        }
    }
}

bool mal_player_is_looping(const mal_player *player) {
    return (player == NULL) ? false : player->looping;
}

void mal_player_set_looping(mal_player *player, const bool looping) {
    if (player != NULL) {
        player->looping = looping;
    }
}

bool mal_player_set_state(mal_player *player, const mal_player_state state) {
    if (player != NULL && player->sl_play != NULL && player->buffer != NULL) {
        SLuint32 sl_state;
        switch (state) {
            case MAL_PLAYER_STATE_STOPPED: default:
                sl_state = SL_PLAYSTATE_STOPPED;
                break;
            case MAL_PLAYER_STATE_PAUSED:
                sl_state = SL_PLAYSTATE_PAUSED;
                break;
            case MAL_PLAYER_STATE_PLAYING:
                sl_state = SL_PLAYSTATE_PLAYING;
                break;
        }

        // Queue if needed
        if (sl_state == SL_PLAYSTATE_PLAYING && player->sl_buffer_queue != NULL) {
            const mal_buffer *buffer = player->buffer;
            if (buffer->managed_data != NULL) {
                const size_t len = buffer->num_frames * (buffer->format.bit_depth/8) * buffer->format.num_channels;
                (*player->sl_buffer_queue)->Enqueue(player->sl_buffer_queue, buffer->managed_data, len);
            }
        }
        
        (*player->sl_play)->SetPlayState(player->sl_play, sl_state);
        
        // Clear buffer queue
        if (sl_state == SL_PLAYSTATE_STOPPED && player->sl_buffer_queue != NULL) {
            (*player->sl_buffer_queue)->Clear(player->sl_buffer_queue);
        }
        return true;
    }
    else {
        return false;
    }
}

mal_player_state mal_player_get_state(const mal_player *player) {
    if (player == NULL || player->sl_play == NULL) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    else {
        SLuint32 state;
        SLresult result = (*player->sl_play)->GetPlayState(player->sl_play, &state);
        if (result != SL_RESULT_SUCCESS) {
            return MAL_PLAYER_STATE_STOPPED;
        }
        else if (state == SL_PLAYSTATE_PAUSED) {
            return MAL_PLAYER_STATE_PAUSED;
        }
        else if (state == SL_PLAYSTATE_PLAYING) {
            return MAL_PLAYER_STATE_PLAYING;
        }
        else {
            return MAL_PLAYER_STATE_STOPPED;
        }
    }
}

void mal_player_free(mal_player *player) {
    if (player != NULL) {
        if (player->context != NULL) {
            mal_vector_remove(&player->context->players, player);
            player->context = NULL;
        }
        mal_player_cleanup(player);
        free(player);
    }
}

#endif
