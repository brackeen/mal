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

#ifndef _MAL_AUDIO_OPENAL_H_
#define _MAL_AUDIO_OPENAL_H_

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#ifndef AL_APIENTRY
#define AL_APIENTRY
#endif

typedef ALvoid AL_APIENTRY (*alcMacOSXMixerOutputRateProcPtr)(const ALdouble value);
typedef ALvoid AL_APIENTRY (*alBufferDataStaticProcPtr)(ALint bid, ALenum format,
                                                        const ALvoid *data, ALsizei size,
                                                        ALsizei freq);

struct _mal_context {
    ALCcontext *al_context;
    alBufferDataStaticProcPtr alBufferDataStaticProc;
    alcMacOSXMixerOutputRateProcPtr alcMacOSXMixerOutputRateProc;
};

struct _mal_buffer {
    ALuint al_buffer;
    bool al_buffer_valid;
};

struct _mal_player {
    ALuint al_source;
    bool al_source_valid;
};

#include "mal_audio_abstract.h"

// MARK: Context

static bool _mal_context_init(mal_context *context, double output_sample_rate) {
    ALCdevice *device = alcOpenDevice(NULL);
    if (!device) {
        return false;
    } else {
        context->data.alBufferDataStaticProc =
            ((alBufferDataStaticProcPtr)alcGetProcAddress(NULL, "alBufferDataStatic"));
        context->data.alcMacOSXMixerOutputRateProc =
            ((alcMacOSXMixerOutputRateProcPtr)alcGetProcAddress(NULL, "alcMacOSXMixerOutputRate"));

        if (context->data.alcMacOSXMixerOutputRateProc) {
            context->data.alcMacOSXMixerOutputRateProc(output_sample_rate);
        }

        context->data.al_context = alcCreateContext(device, 0);
        return (context->data.al_context != NULL);
    }
}

static void _mal_context_dispose(mal_context *context) {
    if (context->data.al_context) {
        ALCdevice *device = alcGetContextsDevice(context->data.al_context);
        alcDestroyContext(context->data.al_context);
        if (device) {
            alcCloseDevice(device);
        }
        alGetError();
        context->data.al_context = NULL;
    }
}

static void _mal_context_set_active(mal_context *context, const bool active) {
    if (active) {
        alcMakeContextCurrent(context->data.al_context);
        alcProcessContext(context->data.al_context);
    } else {
        alcSuspendContext(context->data.al_context);
        alcMakeContextCurrent(NULL);
    }
}

static void _mal_context_set_mute(mal_context *context, const bool mute) {
    alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
    alGetError();
}

static void _mal_context_set_gain(mal_context *context, const float gain) {
    alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
    alGetError();
}

// MARK: Buffer

static bool _mal_buffer_init(mal_context *context, mal_buffer *buffer,
                             const void *copied_data, void *managed_data,
                             const mal_deallocator data_deallocator) {
    alGenBuffers(1, &buffer->data.al_buffer);
    ALenum error;
    if ((error = alGetError()) != AL_NO_ERROR) {
        return false;
    } else {
        buffer->data.al_buffer_valid = true;
        const ALsizei data_length = ((buffer->format.bit_depth / 8) *
                                     buffer->format.num_channels * buffer->num_frames);
        const ALenum al_format = (buffer->format.bit_depth == 8 ?
                                  (buffer->format.num_channels == 2 ? AL_FORMAT_STEREO8 :
                                   AL_FORMAT_MONO8) :
                                  (buffer->format.num_channels == 2 ? AL_FORMAT_STEREO16 :
                                   AL_FORMAT_MONO16));
        const ALsizei freq = (ALsizei)buffer->format.sample_rate;
        if (copied_data) {
            alBufferData(buffer->data.al_buffer, al_format, copied_data, data_length, freq);
            if ((error = alGetError()) != AL_NO_ERROR) {
                return false;
            }
        } else {
            if (context->data.alBufferDataStaticProc) {
                context->data.alBufferDataStaticProc(buffer->data.al_buffer, al_format,
                                                     managed_data, data_length, freq);
                if ((error = alGetError()) != AL_NO_ERROR) {
                    return false;
                } else {
                    buffer->managed_data = managed_data;
                    buffer->managed_data_deallocator = data_deallocator;
                }
            } else {
                alBufferData(buffer->data.al_buffer, al_format, managed_data, data_length, freq);
                if ((error = alGetError()) != AL_NO_ERROR) {
                    return false;
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
    return true;
}

static void _mal_buffer_dispose(mal_buffer *buffer) {
    if (buffer->data.al_buffer_valid) {
        alDeleteBuffers(1, &buffer->data.al_buffer);
        alGetError();
        buffer->data.al_buffer_valid = false;
    }
}

// MARK: Player

static bool _mal_player_init(mal_player *player) {
    alGenSources(1, &player->data.al_source);
    player->data.al_source_valid = (alGetError() == AL_NO_ERROR);
    return player->data.al_source_valid;
}

static void _mal_player_dispose(mal_player *player) {
    if (player->data.al_source_valid) {
        alSourcei(player->data.al_source, AL_BUFFER, AL_NONE);
        alGetError();
        alDeleteSources(1, &player->data.al_source);
        alGetError();
        player->data.al_source_valid = false;
    }
}

static bool _mal_player_set_format(mal_player *player, mal_format format) {
    // Do nothing
    return true;
}

static bool _mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    if (!player->data.al_source_valid) {
        return false;
    }
    alSourcei(player->data.al_source, AL_BUFFER, AL_NONE);
    alGetError();
    if (!buffer) {
        return true;
    } else {
        // Check if format valid
        if (!mal_context_format_is_valid(player->context, buffer->format) ||
            !buffer->data.al_buffer_valid) {
            return false;
        }

        // Queue buffer
        alSourceQueueBuffers(player->data.al_source, 1, &buffer->data.al_buffer);
        return (alGetError() == AL_NO_ERROR);
    }
}

static void _mal_player_set_mute(mal_player *player, bool mute) {
    if (player->data.al_source_valid) {
        player->mute = mute;
        alSourcef(player->data.al_source, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

static void _mal_player_set_gain(mal_player *player, float gain) {
    if (player->data.al_source_valid) {
        player->gain = gain;
        alSourcef(player->data.al_source, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

static void _mal_player_set_looping(mal_player *player, bool looping) {
    if (player->data.al_source_valid) {
        player->looping = looping;
        alSourcei(player->data.al_source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        alGetError();
    }
}

static mal_player_state _mal_player_get_state(const mal_player *player) {
    ALint state = AL_STOPPED;
    if (player->data.al_source_valid) {
        alGetSourcei(player->data.al_source, AL_SOURCE_STATE, &state);
        alGetError();
    }
    if (state == AL_PLAYING) {
        return MAL_PLAYER_STATE_PLAYING;
    } else if (state == AL_PAUSED) {
        return MAL_PLAYER_STATE_PAUSED;
    } else {
        return MAL_PLAYER_STATE_STOPPED;
    }
}

static bool _mal_player_set_state(mal_player *player, mal_player_state state) {
    if (player->data.al_source_valid) {
        if (state == MAL_PLAYER_STATE_PLAYING) {
            alSourcePlay(player->data.al_source);
        } else if (state == MAL_PLAYER_STATE_PAUSED) {
            alSourcePause(player->data.al_source);
        } else {
            alSourceStop(player->data.al_source);
        }
        return (alGetError() == AL_NO_ERROR);
    } else {
        return false;
    }
}

#endif
