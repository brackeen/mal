
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
    mal_vector sources;
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

struct mal_source {
    mal_context *context;
    ALuint al_source;
    mal_format format;
    mal_vector buffers;
    bool looping;
    bool mute;
    float gain;
};

// Deletes OpenAL resources without deleting other info
static void mal_source_cleanup(mal_source *source);
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
        for (unsigned int i = 0; i < context->sources.length; i++) {
            mal_source *source = context->sources.values[i];
            mal_source_cleanup(source);
            source->context = NULL;
        }
        mal_vector_free(&context->sources);
        
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
        // First, stop all sources that are using this buffer.
        if (buffer->context != NULL) {
            for (unsigned int i = 0; i < buffer->context->sources.length; i++) {
                mal_source *source = buffer->context->sources.values[i];
                if (mal_vector_contains(&source->buffers, buffer)) {
                    mal_source_set_buffer(source, NULL);
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

// MARK: Source

mal_source *mal_source_create(mal_context *context, const mal_format format) {
    // Check params
    if (context == NULL || !mal_context_format_is_valid(context, format)) {
        return NULL;
    }
    mal_source *source = calloc(1, sizeof(mal_source));
    if (source != NULL) {
        mal_vector_add(&context->sources, source);
        source->context = context;
        source->format = format;
        
        alGenSources(1, &source->al_source);
        ALenum error;
        if ((error = alGetError()) != AL_NO_ERROR) {
            mal_source_free(source);
            return NULL;
        }
    }
    return source;
}

mal_format mal_source_get_format(const mal_source *source) {
    if (source == NULL) {
        mal_format null_format = { 0, 0, 0 };
        return null_format;
    }
    else {
        return source->format;
    }
}

bool mal_source_set_format(mal_source *source, const mal_format format) {
    if (source != NULL && mal_context_format_is_valid(source->context, format)) {
        source->format = format;
        return true;
    }
    else {
        return false;
    }
}

bool mal_source_set_buffer(mal_source *source, const mal_buffer *buffer) {
    return buffer == NULL ? mal_source_set_buffer_sequence(source, 0, NULL) :
    mal_source_set_buffer_sequence(source, 1, &buffer);
}

bool mal_source_set_buffer_sequence(mal_source *source, const unsigned int num_buffers,
                                    const mal_buffer **buffers) {
    if (source == NULL || source->al_source == 0) {
        return false;
    }
    else if (num_buffers == 0) {
        if (source->buffers.length > 0) {
            mal_source_set_state(source, MAL_SOURCE_STATE_STOPPED);
            source->buffers.length = 0;
        }
        alSourcei(source->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        return true;
    }
    else if (buffers == NULL || buffers[0] == NULL) {
        return false;
    }
    else {
        // Check if format valid
        const mal_format format = buffers[0]->format;
        if (!mal_context_format_is_valid(source->context, format) || buffers[0]->al_buffer == 0) {
            return false;
        }
        
        // Check if all buffers are non-NULL and have the same format
        for (int i = 1; i < num_buffers; i++) {
            if (buffers[i] == NULL || !mal_formats_equal(format, buffers[i]->format) || buffers[i]->al_buffer == 0) {
                return false;
            }
        }
        
        // Stop and clear
        if (source->buffers.length > 0) {
            mal_source_set_state(source, MAL_SOURCE_STATE_STOPPED);
            source->buffers.length = 0;
        }
        alSourcei(source->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        
        // Add buffers to list
        const bool success = mal_vector_add_all(&source->buffers, num_buffers, (void **)buffers);
        if (!success) {
            return false;
        }
        for (int i = 0; i < num_buffers; i++) {
            mal_buffer *buffer = source->buffers.values[i];
            alSourceQueueBuffers(source->al_source, 1, &buffer->al_buffer);
        }
        alGetError();
        return true;
    }
}

bool mal_source_has_buffer(const mal_source *source) {
    return source != NULL && source->buffers.length > 0;
}

bool mal_source_get_mute(const mal_source *source) {
    return source != NULL && source->mute;
}

void mal_source_set_mute(mal_source *source, const bool mute) {
    if (source != NULL) {
        source->mute = mute;
        alSourcef(source->al_source, AL_GAIN, source->mute ? 0 : source->gain);
        alGetError();
    }
}

float mal_source_get_gain(const mal_source *source) {
    return (source == NULL) ? 1.0f : source->gain;
}

void mal_source_set_gain(mal_source *source, const float gain) {
    if (source != NULL) {
        source->gain = gain;
        alSourcef(source->al_source, AL_GAIN, source->mute ? 0 : source->gain);
        alGetError();
    }
}

bool mal_source_is_looping(const mal_source *source) {
    return (source == NULL) ? false : source->looping;
}

void mal_source_set_looping(mal_source *source, const bool looping) {
    if (source != NULL) {
        source->looping = looping;
        alSourcei(source->al_source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        alGetError();
    }
}

bool mal_source_set_state(mal_source *source, const mal_source_state state) {
    if (source != NULL && source->buffers.length > 0) {
        if (state == MAL_SOURCE_STATE_PLAYING) {
            alSourcePlay(source->al_source);
        }
        else if (state == MAL_SOURCE_STATE_PAUSED) {
            alSourcePause(source->al_source);
        }
        else {
            alSourceStop(source->al_source);
        }
        alGetError();
        return true;
    }
    else {
        return false;
    }
}

mal_source_state mal_source_get_state(const mal_source *source) {
    if (source == NULL) {
        return MAL_SOURCE_STATE_STOPPED;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source->al_source, AL_SOURCE_STATE, &state);
    alGetError();
    if (state == AL_PAUSED) {
        return MAL_SOURCE_STATE_PAUSED;
    }
    else if (state == AL_PLAYING) {
        return MAL_SOURCE_STATE_PLAYING;
    }
    else {
        return MAL_SOURCE_STATE_STOPPED;
    }
}

static void mal_source_cleanup(mal_source *source) {
    if (source != NULL && source->al_source != 0) {
        mal_source_set_state(source, MAL_SOURCE_STATE_STOPPED);
        source->buffers.length = 0;
        alSourcei(source->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        alDeleteSources(1, &source->al_source);
        alGetError();
        source->al_source = 0;
    }
}

void mal_source_free(mal_source *source) {
    if (source != NULL) {
        if (source->context != NULL) {
            mal_vector_remove(&source->context->sources, source);
            source->context = NULL;
        }
        mal_source_cleanup(source);
        mal_vector_free(&source->buffers);
        free(source);
    }
}
