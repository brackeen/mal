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

#ifndef MAL_AUDIO_OPENAL_H
#define MAL_AUDIO_OPENAL_H

#include <stdbool.h>

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

struct _MalContext {
    ALCcontext *alContext;
    alBufferDataStaticProcPtr alBufferDataStaticProc;
    alcMacOSXMixerOutputRateProcPtr alcMacOSXMixerOutputRateProc;
};

struct _MalBuffer {
    ALuint alBuffer;
    bool alBufferValid;
};

struct _MalPlayer {
    ALuint alSource;
    bool alSourceValid;
};

#include "mal_audio_abstract.h"

// MARK: Context

static bool _malContextInit(MalContext *context) {
    ALCdevice *device = alcOpenDevice(NULL);
    if (!device) {
        return false;
    } else {
        context->data.alBufferDataStaticProc =
            ((alBufferDataStaticProcPtr)alcGetProcAddress(NULL, "alBufferDataStatic"));
        context->data.alcMacOSXMixerOutputRateProc =
            ((alcMacOSXMixerOutputRateProcPtr)alcGetProcAddress(NULL, "alcMacOSXMixerOutputRate"));

        if (context->data.alcMacOSXMixerOutputRateProc && context->sampleRate > 0) {
            context->data.alcMacOSXMixerOutputRateProc(context->sampleRate);
        }

        context->data.alContext = alcCreateContext(device, 0);
        return (context->data.alContext != NULL);
    }
}

static void _malContextDispose(MalContext *context) {
    if (context->data.alContext) {
        ALCdevice *device = alcGetContextsDevice(context->data.alContext);
        alcDestroyContext(context->data.alContext);
        if (device) {
            alcCloseDevice(device);
        }
        alGetError();
        context->data.alContext = NULL;
    }
}

static void _malContextSetActive(MalContext *context, const bool active) {
    if (active) {
        alcMakeContextCurrent(context->data.alContext);
        alcProcessContext(context->data.alContext);
    } else {
        alcSuspendContext(context->data.alContext);
        alcMakeContextCurrent(NULL);
    }
}

static void _malContextSetMute(MalContext *context, const bool mute) {
    alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
    alGetError();
}

static void _malContextSetGain(MalContext *context, const float gain) {
    alListenerf(AL_GAIN, context->mute ? 0 : context->gain);
    alGetError();
}

// MARK: Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           const malDeallocatorFunc dataDeallocator) {
    alGenBuffers(1, &buffer->data.alBuffer);
    ALenum error;
    if ((error = alGetError()) != AL_NO_ERROR) {
        return false;
    } else {
        buffer->data.alBufferValid = true;
        const ALsizei dataLength = ((buffer->format.bitDepth / 8) *
                                    buffer->format.numChannels * buffer->numFrames);
        const ALenum alFormat = (buffer->format.bitDepth == 8 ?
                                 (buffer->format.numChannels == 2 ? AL_FORMAT_STEREO8 :
                                  AL_FORMAT_MONO8) :
                                 (buffer->format.numChannels == 2 ? AL_FORMAT_STEREO16 :
                                  AL_FORMAT_MONO16));
        const ALsizei freq = (ALsizei)buffer->format.sampleRate;
        if (copiedData) {
            alBufferData(buffer->data.alBuffer, alFormat, copiedData, dataLength, freq);
            if ((error = alGetError()) != AL_NO_ERROR) {
                return false;
            }
        } else {
            if (context->data.alBufferDataStaticProc) {
                context->data.alBufferDataStaticProc(buffer->data.alBuffer, alFormat,
                                                     managedData, dataLength, freq);
                if ((error = alGetError()) != AL_NO_ERROR) {
                    return false;
                } else {
                    buffer->managedData = managedData;
                    buffer->managedDataDeallocator = dataDeallocator;
                }
            } else {
                alBufferData(buffer->data.alBuffer, alFormat, managedData, dataLength, freq);
                if ((error = alGetError()) != AL_NO_ERROR) {
                    return false;
                } else {
                    // Managed data was copied because there is no alBufferDataStaticProc, so
                    // dealloc immediately.
                    if (dataDeallocator) {
                        dataDeallocator(managedData);
                    }
                }
            }
        }
    }
    return true;
}

static void _malBufferDispose(MalBuffer *buffer) {
    if (buffer->data.alBufferValid) {
        alDeleteBuffers(1, &buffer->data.alBuffer);
        alGetError();
        buffer->data.alBufferValid = false;
    }
}

// MARK: Player

static bool _malPlayerInit(MalPlayer *player) {
    alGenSources(1, &player->data.alSource);
    player->data.alSourceValid = (alGetError() == AL_NO_ERROR);
    return player->data.alSourceValid;
}

static void _malPlayerDispose(MalPlayer *player) {
    if (player->data.alSourceValid) {
        alSourcei(player->data.alSource, AL_BUFFER, AL_NONE);
        alGetError();
        alDeleteSources(1, &player->data.alSource);
        alGetError();
        player->data.alSourceValid = false;
    }
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    // Do nothing
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    // Do nothing
    return true;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    if (!player->data.alSourceValid) {
        return false;
    }
    alSourcei(player->data.alSource, AL_BUFFER, AL_NONE);
    alGetError();
    if (!buffer) {
        return true;
    } else {
        // Check if format valid
        if (!malContextIsFormatValid(player->context, buffer->format) ||
            !buffer->data.alBufferValid) {
            return false;
        }

        // Queue buffer
        alSourceQueueBuffers(player->data.alSource, 1, &buffer->data.alBuffer);
        return (alGetError() == AL_NO_ERROR);
    }
}

static void _malPlayerSetMute(MalPlayer *player, bool mute) {
    if (player->data.alSourceValid) {
        player->mute = mute;
        alSourcef(player->data.alSource, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

static void _malPlayerSetGain(MalPlayer *player, float gain) {
    if (player->data.alSourceValid) {
        player->gain = gain;
        alSourcef(player->data.alSource, AL_GAIN, player->mute ? 0 : player->gain);
        alGetError();
    }
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
    if (player->data.alSourceValid) {
        player->looping = looping;
        alSourcei(player->data.alSource, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        alGetError();
    }
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    ALint state = AL_STOPPED;
    if (player->data.alSourceValid) {
        alGetSourcei(player->data.alSource, AL_SOURCE_STATE, &state);
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

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState,
                                  MalPlayerState state) {
    if (player->data.alSourceValid) {
        if (state == MAL_PLAYER_STATE_PLAYING) {
            alSourcePlay(player->data.alSource);
        } else if (state == MAL_PLAYER_STATE_PAUSED) {
            alSourcePause(player->data.alSource);
        } else {
            alSourceStop(player->data.alSource);
        }
        return (alGetError() == AL_NO_ERROR);
    } else {
        return false;
    }
}

#endif
