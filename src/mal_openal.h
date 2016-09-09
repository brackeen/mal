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

#ifndef _MAL_OPENAL_H_
#define _MAL_OPENAL_H_

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif
#include "mal_vector.h"

#ifndef AL_APIENTRY
#define AL_APIENTRY
#endif

/**
 Engines using this class need to implement mal_did_create_context, mal_will_destory_context, and mal_will_set_active 
 */

static void mal_did_create_context(mal_context *context);
static void mal_will_destory_context(mal_context *context);
static void mal_did_set_active(mal_context *context, const bool active);

typedef ALvoid AL_APIENTRY (*alcMacOSXMixerOutputRateProcPtr)(const ALdouble value);
typedef ALvoid AL_APIENTRY (*alBufferDataStaticProcPtr)(ALint bid, ALenum format,
                                                        const ALvoid *data, ALsizei size,
                                                        ALsizei freq);

struct mal_context {
    mal_vector players;
    mal_vector buffers;
    bool routes[NUM_MAL_ROUTES];
    float gain;
    bool mute;

    ALCcontext *al_context;
    alBufferDataStaticProcPtr alBufferDataStaticProc;
    alcMacOSXMixerOutputRateProcPtr alcMacOSXMixerOutputRateProc;
};

struct mal_buffer {
    mal_context *context;
    mal_format format;
    uint32_t num_frames;
    void *managed_data;
    mal_deallocator managed_data_deallocator;

    ALuint al_buffer;
    bool al_buffer_valid;
};

struct mal_player {
    mal_context *context;
    mal_format format;
    const mal_buffer *buffer;
    float gain;
    bool mute;
    bool looping;

    ALuint al_source;
    bool al_source_valid;
};

// Deletes OpenAL resources without deleting other info
static void mal_player_cleanup(mal_player *player);
static void mal_buffer_cleanup(mal_buffer *buffer);

// MARK: Context

mal_context *mal_context_create(const double output_sample_rate) {
    mal_context *context = calloc(1, sizeof(mal_context));
    if (context) {
        context->mute = false;
        context->gain = 1.0f;
        ALCdevice *device = alcOpenDevice(NULL);
        if (!device) {
            mal_context_free(context);
            context = NULL;
        } else {
            context->alBufferDataStaticProc =
                ((alBufferDataStaticProcPtr)alcGetProcAddress(NULL, "alBufferDataStatic"));
            context->alcMacOSXMixerOutputRateProc =
                ((alcMacOSXMixerOutputRateProcPtr)alcGetProcAddress(NULL,
                                                                    "alcMacOSXMixerOutputRate"));

            if (context->alcMacOSXMixerOutputRateProc) {
                context->alcMacOSXMixerOutputRateProc(output_sample_rate);
            }

            context->al_context = alcCreateContext(device, 0);
            if (!context->al_context) {
                mal_context_free(context);
                context = NULL;
            } else {
                mal_did_create_context(context);
                mal_context_set_active(context, true);
            }
        }
    }
    return context;
}

void mal_context_set_active(mal_context *context, const bool active) {
    if (context) {
        if (active) {
            alcMakeContextCurrent(context->al_context);
            alcProcessContext(context->al_context);
        } else {
            alcSuspendContext(context->al_context);
            alcMakeContextCurrent(NULL);
        }
        mal_did_set_active(context, active);
    }
}

bool mal_context_get_mute(const mal_context *context) {
    return context ? context->mute : false;
}

void mal_context_set_mute(mal_context *context, const bool mute) {
    if (context) {
        context->mute = mute;
        alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
        alGetError();
    }
}

float mal_context_get_gain(const mal_context *context) {
    return context ? context->gain : 1.0f;
}

void mal_context_set_gain(mal_context *context, const float gain) {
    if (context) {
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

bool mal_context_is_route_enabled(const mal_context *context, const mal_route route) {
    if (context && route < NUM_MAL_ROUTES) {
        return context->routes[route];
    } else {
        return false;
    }
}

void mal_context_free(mal_context *context) {
    if (context) {
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
        if (context->al_context) {
            ALCdevice *device = alcGetContextsDevice(context->al_context);
            alcDestroyContext(context->al_context);
            if (device) {
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
                                              void *managed_data,
                                              const mal_deallocator data_deallocator) {
    // Check params
    const bool oneNonNullData = (copied_data == NULL) != (managed_data == NULL);
    if (!context || !mal_context_format_is_valid(context, format) || num_frames == 0 ||
        !oneNonNullData) {
        return NULL;
    }
    mal_buffer *buffer = calloc(1, sizeof(mal_buffer));
    if (buffer) {
        mal_vector_add(&context->buffers, buffer);
        buffer->context = context;
        buffer->format = format;
        buffer->num_frames = num_frames;
        alGenBuffers(1, &buffer->al_buffer);
        ALenum error;
        if ((error = alGetError()) != AL_NO_ERROR) {
            mal_buffer_free(buffer);
            buffer = NULL;
        } else {
            buffer->al_buffer_valid = true;
            const ALsizei data_length = (format.bit_depth / 8) * format.num_channels * num_frames;
            const ALenum al_format = (format.bit_depth == 8 ?
                                      (format.num_channels == 2 ? AL_FORMAT_STEREO8 :
                                       AL_FORMAT_MONO8) :
                                      (format.num_channels == 2 ? AL_FORMAT_STEREO16 :
                                       AL_FORMAT_MONO16));
            const ALsizei freq = (ALsizei)format.sample_rate;
            if (copied_data) {
                alBufferData(buffer->al_buffer, al_format, copied_data, data_length, freq);
                if ((error = alGetError()) != AL_NO_ERROR) {
                    mal_buffer_free(buffer);
                    return NULL;
                }
            } else {
                if (context->alBufferDataStaticProc) {
                    context->alBufferDataStaticProc(buffer->al_buffer, al_format, managed_data,
                                                    data_length, freq);
                    if ((error = alGetError()) != AL_NO_ERROR) {
                        mal_buffer_free(buffer);
                        return NULL;
                    } else {
                        buffer->managed_data = managed_data;
                        buffer->managed_data_deallocator = data_deallocator;
                    }
                } else {
                    alBufferData(buffer->al_buffer, al_format, managed_data, data_length, freq);
                    if ((error = alGetError()) != AL_NO_ERROR) {
                        mal_buffer_free(buffer);
                        return NULL;
                    } else {
                        // Managed data was copied because there is no alBufferDataStaticProc, so
                        // dealloc immediately.
                        if (data_deallocator) {
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
    if (buffer) {
        return buffer->format;
    } else {
        mal_format null_format = {0, 0, 0};
        return null_format;
    }
}

uint32_t mal_buffer_get_num_frames(const mal_buffer *buffer) {
    return buffer ? buffer->num_frames : 0;
}

void *mal_buffer_get_data(const mal_buffer *buffer) {
    return buffer ? buffer->managed_data : NULL;
}

static void mal_buffer_cleanup(mal_buffer *buffer) {
    if (buffer && buffer->al_buffer_valid) {
        // First, stop all players that are using this buffer.
        if (buffer->context) {
            for (unsigned int i = 0; i < buffer->context->players.length; i++) {
                mal_player *player = buffer->context->players.values[i];
                if (player->buffer == buffer) {
                    mal_player_set_buffer(player, NULL);
                }
            }
        }

        // Delete buffer
        alDeleteBuffers(1, &buffer->al_buffer);
        alGetError();
        buffer->al_buffer_valid = false;
    }
}

void mal_buffer_free(mal_buffer *buffer) {
    if (buffer) {
        mal_buffer_cleanup(buffer);
        if (buffer->context) {
            mal_vector_remove(&buffer->context->buffers, buffer);
            buffer->context = NULL;
        }
        if (buffer->managed_data) {
            if (buffer->managed_data_deallocator) {
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
    if (!context || !mal_context_format_is_valid(context, format)) {
        return NULL;
    }
    mal_player *player = calloc(1, sizeof(mal_player));
    if (player) {
        mal_vector_add(&context->players, player);
        player->context = context;
        player->format = format;

        alGenSources(1, &player->al_source);
        ALenum error;
        if ((error = alGetError()) != AL_NO_ERROR) {
            mal_player_free(player);
            return NULL;
        }
        player->al_source_valid = true;
    }
    return player;
}

mal_format mal_player_get_format(const mal_player *player) {
    if (player) {
        return player->format;
    } else {
        mal_format null_format = {0, 0, 0};
        return null_format;
    }
}

bool mal_player_set_format(mal_player *player, const mal_format format) {
    if (player && mal_context_format_is_valid(player->context, format)) {
        mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
        player->buffer = NULL;
        if (player->al_source_valid) {
            alSourcei(player->al_source, AL_BUFFER, AL_NONE);
            alGetError();
        }
        player->format = format;
        return true;
    } else {
        return false;
    }
}

bool mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    if (!player) {
        return false;
    } else {
        // Stop and clear
        if (player->buffer) {
            mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
            player->buffer = NULL;
        }
        if (!player->al_source_valid) {
            return false;
        }
        alSourcei(player->al_source, AL_BUFFER, AL_NONE);
        alGetError();
        if (!buffer) {
            return true;
        } else {
            // Check if format valid
            if (!mal_context_format_is_valid(player->context, buffer->format) ||
                !buffer->al_buffer_valid) {
                return false;
            }

            // Queue buffer
            player->buffer = buffer;
            alSourceQueueBuffers(player->al_source, 1, &buffer->al_buffer);
            alGetError();
            return true;
        }
    }
}

const mal_buffer *mal_player_get_buffer(const mal_player *player) {
    return player ? player->buffer : NULL;
}

bool mal_player_get_mute(const mal_player *player) {
    return player && player->mute;
}

void mal_player_set_mute(mal_player *player, const bool mute) {
    if (player && player->al_source_valid) {
        player->mute = mute;
        alSourcef(player->al_source, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

float mal_player_get_gain(const mal_player *player) {
    return player ? player->gain : 1.0f;
}

void mal_player_set_gain(mal_player *player, const float gain) {
    if (player && player->al_source_valid) {
        player->gain = gain;
        alSourcef(player->al_source, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

bool mal_player_is_looping(const mal_player *player) {
    return player ? player->looping : false;
}

void mal_player_set_looping(mal_player *player, const bool looping) {
    if (player && player->al_source_valid) {
        player->looping = looping;
        alSourcei(player->al_source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        alGetError();
    }
}

bool mal_player_set_state(mal_player *player, const mal_player_state state) {
    if (player && player->al_source_valid && player->buffer &&
        state != mal_player_get_state(player)) {
        if (state == MAL_PLAYER_STATE_PLAYING) {
            alSourcePlay(player->al_source);
        } else if (state == MAL_PLAYER_STATE_PAUSED) {
            alSourcePause(player->al_source);
        } else {
            alSourceStop(player->al_source);
        }
        alGetError();
        return true;
    } else {
        return false;
    }
}

mal_player_state mal_player_get_state(const mal_player *player) {
    if (!player || !player->al_source_valid) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(player->al_source, AL_SOURCE_STATE, &state);
    alGetError();
    if (state == AL_PAUSED) {
        return MAL_PLAYER_STATE_PAUSED;
    } else if (state == AL_PLAYING) {
        return MAL_PLAYER_STATE_PLAYING;
    } else {
        return MAL_PLAYER_STATE_STOPPED;
    }
}

static void mal_player_cleanup(mal_player *player) {
    if (player) {
        mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
        player->buffer = NULL;
        if (player->al_source_valid) {
            alSourcei(player->al_source, AL_BUFFER, AL_NONE);
            alGetError();
            alDeleteSources(1, &player->al_source);
            alGetError();
            player->al_source_valid = false;
        }
    }
}

void mal_player_free(mal_player *player) {
    if (player) {
        if (player->context) {
            mal_vector_remove(&player->context->players, player);
            player->context = NULL;
        }
        mal_player_cleanup(player);
        free(player);
    }
}

#endif
