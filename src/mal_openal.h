
#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif
#include <stdlib.h>
#include <memory.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// MARK: Vector

typedef struct {
    void **values;
    unsigned int length;
    unsigned int capacity;
} mal_vector;

static bool mal_vector_ensure_capacity(mal_vector *list, const unsigned int additional_values) {
    if (list != NULL) {
        if (list->values == NULL || list->length + additional_values > list->capacity) {
            const unsigned int new_capacity = MAX(list->length + additional_values, list->capacity << 1);
            void **new_data = realloc(list->values, sizeof(void*) * new_capacity);
            if (new_data == NULL) {
                return false;
            }
            list->values = new_data;
            list->capacity = new_capacity;
        }
        return true;
    }
    return false;
}

static bool mal_vector_add(mal_vector *list, void *value) {
    if (mal_vector_ensure_capacity(list, 1)) {
        list->values[list->length++] = value;
        return true;
    }
    else {
        return false;
    }
}

static bool mal_vector_add_all(mal_vector *list, unsigned int num_values, void **values) {
    if (mal_vector_ensure_capacity(list, num_values)) {
        memcpy(list->values + list->length, values, sizeof(void *) * num_values);
        list->length += num_values;
        return true;
    }
    else {
        return false;
    }
}

static bool mal_vector_contains(const mal_vector *list, void *value) {
    if (list != NULL) {
        for (unsigned int i = 0; i < list->length; i++) {
            if (list->values[i] == value) {
                return true;
            }
        }
    }
    return false;
}

static bool mal_vector_remove(mal_vector *list, void *value) {
    if (list != NULL) {
        for (unsigned int i = 0; i < list->length; i++) {
            if (list->values[i] == value) {
                for (unsigned int j = i; j < list->length - 1; j++) {
                    list->values[j] = list->values[j + 1];
                }
                list->length--;
                return true;
            }
        }
    }
    return false;
}

static void mal_vector_free(mal_vector *list) {
    if (list != NULL && list->values != NULL) {
        free(list->values);
        list->values = NULL;
        list->length = 0;
        list->capacity = 0;
    }
}

// MARK: Types

/**
 Engines using this class need to implement mal_did_create_context, mal_will_destory_context, mal_will_set_active,
 and mal_context_is_audio_route_enabled()
 */

static void mal_did_create_context(mal_context *context);
static void mal_will_destory_context(mal_context *context);
static void mal_did_set_active(mal_context *context, const bool active);

typedef ALvoid AL_APIENTRY (*alcMacOSXMixerOutputRateProcPtr) (const ALdouble value);
typedef ALvoid AL_APIENTRY (*alBufferDataStaticProcPtr) (ALint bid, ALenum format, const ALvoid *data,
                                                         ALsizei size, ALsizei freq);

struct mal_context {
    ALCcontext *al_context;
    alBufferDataStaticProcPtr alBufferDataStaticProc;
    alcMacOSXMixerOutputRateProcPtr alcMacOSXMixerOutputRateProc;
    bool mute;
    float gain;
    void *internal_data;
    mal_vector players;
    mal_vector buffers;
};

struct mal_buffer {
    mal_context *context;
    ALuint al_buffer;
    mal_format format;
    uint32_t num_frames;
    void *managed_data;
    mal_deallocator managed_data_deallocator;
};

struct mal_player {
    mal_context *context;
    ALuint al_source;
    mal_format format;
    mal_vector buffers;
    bool looping;
    bool mute;
    float gain;
};

// Deletes OpenAL resources without deleting other info
static void mal_player_cleanup(mal_player *player);
static void mal_buffer_cleanup(mal_buffer *buffer);

// MARK: Context

mal_context *mal_context_create(const double output_sample_rate) {
    mal_context *context = calloc(1, sizeof(mal_context));
    if (context != NULL) {
        context->mute = false;
        context->gain = 1.0f;
        ALCdevice *device = alcOpenDevice(NULL);
        if (device == NULL) {
            mal_context_free(context);
            return NULL;
        }
        context->alBufferDataStaticProc = ((alBufferDataStaticProcPtr)
                                           alcGetProcAddress(NULL, "alBufferDataStatic"));
        context->alcMacOSXMixerOutputRateProc = ((alcMacOSXMixerOutputRateProcPtr)
                                                 alcGetProcAddress(NULL, "alcMacOSXMixerOutputRate"));
        
        if (context->alcMacOSXMixerOutputRateProc != NULL) {
            context->alcMacOSXMixerOutputRateProc(output_sample_rate);
        }
        
        context->al_context = alcCreateContext(device, 0);
        if (context->al_context == NULL) {
            mal_context_free(context);
            return NULL;
        }
        mal_did_create_context(context);
        mal_context_set_active(context, true);
    }
    return context;
}

void mal_context_set_active(mal_context *context, const bool active) {
    if (context != NULL) {
        if (active) {
            alcMakeContextCurrent(context->al_context);
        }
        else {
            alcMakeContextCurrent(NULL);
        }
        mal_did_set_active(context, active);
    }
}

bool mal_context_get_mute(const mal_context *context) {
    return (context == NULL) ? false : context->mute;
}

void mal_context_set_mute(mal_context *context, const bool mute) {
    if (context != NULL) {
        context->mute = mute;
        alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
        alGetError();
    }
}

float mal_context_get_gain(const mal_context *context) {
    return (context == NULL) ? 1.0f : context->gain;
}

void mal_context_set_gain(mal_context *context, const float gain) {
    if (context != NULL) {
        context->gain = gain;
        alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
        alGetError();
    }
}

bool mal_context_format_is_valid(const mal_context *context, const mal_format format) {
    return ((format.bit_depth == 8 || format.bit_depth == 16) &&
            (format.num_channels == 1 || format.num_channels == 2) &&
            format.sample_rate > 0);
}

void mal_context_free(mal_context *context) {
    if (context != NULL) {
        // Delete AL sources
        for (unsigned int i = 0; i < context->players.length; i++) {
            mal_player *player = context->players.values[i];
            mal_player_cleanup(player);
            player->context = NULL;
        }
        mal_vector_free(&context->players);
        
        // Delete AL buffers
        for (unsigned int i = 0; i < context->buffers.length; i++) {
            mal_buffer *buffer = context->buffers.values[i];
            mal_buffer_cleanup(buffer);
            buffer->context = NULL;
        }
        mal_vector_free(&context->buffers);
        
        mal_will_destory_context(context);
        mal_context_set_active(context, false);
        if (context->al_context != NULL) {
            ALCdevice *device = alcGetContextsDevice(context->al_context);
            alcDestroyContext(context->al_context);
            if (device != NULL) {
                alcCloseDevice(device);
            }
            alGetError();
            context->al_context = NULL;
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

static mal_buffer *mal_buffer_create_internal(mal_context *context, const mal_format format,
                                              const uint32_t num_frames, const void *copied_data,
                                              void *managed_data, const mal_deallocator data_deallocator) {
    // Check params
    const bool oneNonNullData = (copied_data == NULL) != (managed_data == NULL);
    if (context == NULL || !mal_context_format_is_valid(context, format) || num_frames == 0 || !oneNonNullData) {
        return NULL;
    }
    mal_buffer *buffer = calloc(1, sizeof(mal_buffer));
    if (buffer != NULL) {
        mal_vector_add(&context->buffers, buffer);
        buffer->context = context;
        buffer->format = format;
        buffer->num_frames = num_frames;
        alGenBuffers(1, &buffer->al_buffer);
        ALenum error;
        if ((error = alGetError()) != AL_NO_ERROR) {
            mal_buffer_free(buffer);
            return NULL;
        }
        else {
            const ALsizei data_length = (format.bit_depth/8) * format.num_channels * num_frames;
            const ALenum al_format = (format.bit_depth == 8 ?
                                      (format.num_channels == 2 ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8) :
                                      (format.num_channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16));
            const ALsizei freq = (ALsizei)format.sample_rate;
            if (copied_data != NULL) {
                alBufferData(buffer->al_buffer, al_format, copied_data, data_length, freq);
                if ((error = alGetError()) != AL_NO_ERROR) {
                    mal_buffer_free(buffer);
                    return NULL;
                }
            }
            else {
                if (context->alBufferDataStaticProc) {
                    context->alBufferDataStaticProc(buffer->al_buffer, al_format, managed_data, data_length, freq);
                    if ((error = alGetError()) != AL_NO_ERROR) {
                        mal_buffer_free(buffer);
                        return NULL;
                    }
                    else {
                        buffer->managed_data = managed_data;
                        buffer->managed_data_deallocator = data_deallocator;
                    }
                }
                else {
                    alBufferData(buffer->al_buffer, al_format, managed_data, data_length, freq);
                    if ((error = alGetError()) != AL_NO_ERROR) {
                        mal_buffer_free(buffer);
                        return NULL;
                    }
                    else {
                        // Managed data was copied because there is no alBufferDataStaticProc, so dealloc immediately.
                        if (data_deallocator != NULL) {
                            data_deallocator(managed_data);
                        }
                    }
                }
            }
        }
    }
    return buffer;
}

mal_buffer *mal_buffer_create(mal_context *context, const mal_format format,
                              const uint32_t num_frames, const void *data) {
    return mal_buffer_create_internal(context, format, num_frames, data, NULL, NULL);
}

mal_buffer *mal_buffer_create_no_copy(mal_context *context, const mal_format format,
                                      const uint32_t num_frames, void *data,
                                      const mal_deallocator data_deallocator) {
    return mal_buffer_create_internal(context, format, num_frames, NULL, data, data_deallocator);
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
    if (buffer != NULL && buffer->al_buffer != 0) {
        // First, stop all players that are using this buffer.
        if (buffer->context != NULL) {
            for (unsigned int i = 0; i < buffer->context->players.length; i++) {
                mal_player *player = buffer->context->players.values[i];
                if (mal_vector_contains(&player->buffers, buffer)) {
                    mal_player_set_buffer(player, NULL);
                }
            }
        }
        
        // Delete buffer
        alDeleteBuffers(1, &buffer->al_buffer);
        alGetError();
        buffer->al_buffer = 0;
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

mal_player *mal_player_create(mal_context *context, const mal_format format) {
    // Check params
    if (context == NULL || !mal_context_format_is_valid(context, format)) {
        return NULL;
    }
    mal_player *player = calloc(1, sizeof(mal_player));
    if (player != NULL) {
        mal_vector_add(&context->players, player);
        player->context = context;
        player->format = format;
        
        alGenSources(1, &player->al_source);
        ALenum error;
        if ((error = alGetError()) != AL_NO_ERROR) {
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
        player->format = format;
        return true;
    }
    else {
        return false;
    }
}

bool mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    return buffer == NULL ? mal_player_set_buffer_sequence(player, 0, NULL) :
    mal_player_set_buffer_sequence(player, 1, &buffer);
}

bool mal_player_set_buffer_sequence(mal_player *player, const unsigned int num_buffers,
                                    const mal_buffer **buffers) {
    if (player == NULL || player->al_source == 0) {
        return false;
    }
    else if (num_buffers == 0) {
        if (player->buffers.length > 0) {
            mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
            player->buffers.length = 0;
        }
        alSourcei(player->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        return true;
    }
    else if (buffers == NULL || buffers[0] == NULL) {
        return false;
    }
    else {
        // Check if format valid
        const mal_format format = buffers[0]->format;
        if (!mal_context_format_is_valid(player->context, format) || buffers[0]->al_buffer == 0) {
            return false;
        }
        
        // Check if all buffers are non-NULL and have the same format
        for (int i = 1; i < num_buffers; i++) {
            if (buffers[i] == NULL || !mal_formats_equal(format, buffers[i]->format) || buffers[i]->al_buffer == 0) {
                return false;
            }
        }
        
        // Stop and clear
        if (player->buffers.length > 0) {
            mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
            player->buffers.length = 0;
        }
        alSourcei(player->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        
        // Add buffers to list
        const bool success = mal_vector_add_all(&player->buffers, num_buffers, (void **)buffers);
        if (!success) {
            return false;
        }
        for (int i = 0; i < num_buffers; i++) {
            mal_buffer *buffer = player->buffers.values[i];
            alSourceQueueBuffers(player->al_source, 1, &buffer->al_buffer);
        }
        alGetError();
        return true;
    }
}

bool mal_player_has_buffer(const mal_player *player) {
    return player != NULL && player->buffers.length > 0;
}

bool mal_player_get_mute(const mal_player *player) {
    return player != NULL && player->mute;
}

void mal_player_set_mute(mal_player *player, const bool mute) {
    if (player != NULL) {
        player->mute = mute;
        alSourcef(player->al_source, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

float mal_player_get_gain(const mal_player *player) {
    return (player == NULL) ? 1.0f : player->gain;
}

void mal_player_set_gain(mal_player *player, const float gain) {
    if (player != NULL) {
        player->gain = gain;
        alSourcef(player->al_source, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

bool mal_player_is_looping(const mal_player *player) {
    return (player == NULL) ? false : player->looping;
}

void mal_player_set_looping(mal_player *player, const bool looping) {
    if (player != NULL) {
        player->looping = looping;
        alSourcei(player->al_source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        alGetError();
    }
}

bool mal_player_set_state(mal_player *player, const mal_player_state state) {
    if (player != NULL && player->buffers.length > 0) {
        if (state == MAL_PLAYER_STATE_PLAYING) {
            alSourcePlay(player->al_source);
        }
        else if (state == MAL_PLAYER_STATE_PAUSED) {
            alSourcePause(player->al_source);
        }
        else {
            alSourceStop(player->al_source);
        }
        alGetError();
        return true;
    }
    else {
        return false;
    }
}

mal_player_state mal_player_get_state(const mal_player *player) {
    if (player == NULL) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(player->al_source, AL_SOURCE_STATE, &state);
    alGetError();
    if (state == AL_PAUSED) {
        return MAL_PLAYER_STATE_PAUSED;
    }
    else if (state == AL_PLAYING) {
        return MAL_PLAYER_STATE_PLAYING;
    }
    else {
        return MAL_PLAYER_STATE_STOPPED;
    }
}

static void mal_player_cleanup(mal_player *player) {
    if (player != NULL && player->al_source != 0) {
        mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
        player->buffers.length = 0;
        alSourcei(player->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        alDeleteSources(1, &player->al_source);
        alGetError();
        player->al_source = 0;
    }
}

void mal_player_free(mal_player *player) {
    if (player != NULL) {
        if (player->context != NULL) {
            mal_vector_remove(&player->context->players, player);
            player->context = NULL;
        }
        mal_player_cleanup(player);
        mal_vector_free(&player->buffers);
        free(player);
    }
}
