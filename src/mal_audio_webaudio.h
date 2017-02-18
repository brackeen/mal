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

#ifndef MAL_AUDIO_WEBAUDIO_H
#define MAL_AUDIO_WEBAUDIO_H

#include <emscripten/emscripten.h>

static int nextContextId = 1;
static int nextBufferId = 1;
static int nextPlayerId = 1;

struct _MalContext {
    int contextId;
};

struct _MalBuffer {
    int bufferId;
};

struct _MalPlayer {
    int playerId;
};

#include "mal_audio_abstract.h"

// MARK: Context

static bool _malContextInit(MalContext *context) {
    int success = EM_ASM_INT({
        malContexts = window.malContexts || {};
        var context;
        try {
            if (window.AudioContext) {
                context = new AudioContext();
            } else if (window.webkitAudioContext) {
                context = new webkitAudioContext();
            }
        } catch (e) { }

        if (context) {
            var gainNode = context.createGain();
            gainNode.connect(context.destination);

            var data = {};
            data.context = context;
            data.outputNode = gainNode;
            data.buffers = {};
            data.players = {};
            malContexts[$0] = data;
            return 1;
        } else {
            return 0;
        }
    }, nextContextId);
    if (success) {
        context->data.contextId = nextContextId;
        nextContextId++;
        return true;
    } else {
        return false;
    }
}

static void _malContextDispose(MalContext *context) {
    if (context->data.contextId) {
        EM_ASM_ARGS({
            delete malContexts[$0];
        }, context->data.contextId);
        context->data.contextId = 0;
    }
}

static void _malContextSetActive(MalContext *context, bool active) {
    (void)context;
    (void)active;
    // Do nothing
}

static void _malContextSetMute(MalContext *context, bool mute) {
    (void)mute;
    _malContextSetGain(context, context->gain);
}

static void _malContextSetGain(MalContext *context, float gain) {
    if (context->data.contextId) {
        float totalGain = context->mute ? 0.0f : gain;
        EM_ASM_ARGS({
            malContexts[$0].outputNode.gain.value = $1;
        }, context->data.contextId, totalGain);
    }
}

// MARK: Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                             const void *copiedData, void *managedData,
                             const malDeallocatorFunc dataDeallocator) {
    if (!context->data.contextId) {
        return false;
    }
    const void *data = copiedData ? copiedData : managedData;
    MalFormat format = buffer->format;
    // Convert from interleaved 16-bit signed integer to non-interleaved 32-bit float.
    // This uses Emscripten's HEAP16 Int16Array to access the data, and may not be future-proof.
    int success = EM_ASM_INT({
        var contextData = malContexts[$0];
        var channels = $2;
        var frames = $3;
        var sampleRate = $4;
        var data = $5 >> 1;
        var buffer;
        try {
            buffer = contextData.context.createBuffer(channels, frames, sampleRate);
        } catch (e) { }

        if (buffer) {
            for (var i = 0; i < channels; i++) {
                var dst = buffer.getChannelData(i);
                var src = data + i;
                for (var j = 0; j < frames; j++) {
                    dst[j] = HEAP16[src] / 32768.0;
                    src += channels;
                }
            }
            contextData.buffers[$1] = buffer;
            return 1;
        } else {
            return 0;
        }
    }, context->data.contextId, nextBufferId,
                format.numChannels, buffer->numFrames, format.sampleRate, data);
    if (success) {
        buffer->data.bufferId = nextBufferId;
        nextBufferId++;
        // Data is always copied. Delete any managed data immediately.
        if (managedData && dataDeallocator) {
            dataDeallocator(managedData);
        }
        return true;
    } else {
        return false;
    }
}

static void _malBufferDispose(MalBuffer *buffer) {
    MalContext *context = buffer->context;
    if (context && context->data.contextId && buffer->data.bufferId) {
        EM_ASM_ARGS({
            delete malContexts[$0].buffers[$1];
        }, context->data.contextId, buffer->data.bufferId);
        buffer->data.bufferId = 0;
    }
}

// MARK: Player

static bool _malPlayerInit(MalPlayer *player) {
    MalContext *context = player->context;
    if (context && context->data.contextId) {
        player->data.playerId = nextPlayerId;
        nextPlayerId++;
        EM_ASM_ARGS({
            malContexts[$0].players[$1] = { };
        }, context->data.contextId, player->data.playerId);
        return true;
    } else {
        return false;
    }
}

static void _malPlayerDispose(MalPlayer *player) {
    MalContext *context = player->context;
    if (context && context->data.contextId && player->data.playerId) {
        EM_ASM_ARGS({
            var player = malContexts[$0].players[$1];
            if (player.gainNode) {
                player.gainNode.disconnect();
            }
            if (player.sourceNode) {
                player.sourceNode.disconnect();
            }
            delete malContexts[$0].players[$1];
        }, context->data.contextId, player->data.playerId);
    }
    player->data.playerId = 0;
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    MalContext *context = player->context;
    if (context && context->data.contextId && player->data.playerId) {
        EM_ASM_ARGS({
            var player = malContexts[$0].players[$1];
            player.onFinishedIdLow = $2;
            player.onFinishedIdHigh = $3;
        }, context->data.contextId, player->data.playerId,
                    (uint32_t)(player->onFinishedId & 0xffffffff),
                    (uint32_t)(player->onFinishedId >> 32));
    }
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    (void)player;
    (void)format;
    // Do nothing - format determined by attached buffer
    return true;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    MalContext *context = player->context;
    if (!context || !player->data.playerId) {
        return false;
    } else if (!buffer) {
        return true;
    } else if (!malContextIsFormatValid(context, buffer->format) || !buffer->data.bufferId) {
        return false;
    } else {
        return true;
    }
}

static void _malPlayerSetMute(MalPlayer *player, bool mute) {
    (void)mute;
    _malPlayerSetGain(player, player->gain);
}

static void _malPlayerSetGain(MalPlayer *player, float gain) {
    MalContext *context = player->context;
    if (context && context->data.contextId && player->data.playerId) {
        float totalGain = player->mute ? 0.0f : gain;
        EM_ASM_ARGS({
            var player = malContexts[$0].players[$1];
            if (player && player.gainNode) {
                player.gainNode.gain.value = $2;;
            }
        }, context->data.contextId, player->data.playerId, totalGain);
    }
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
    MalContext *context = player->context;
    if (context && context->data.contextId && player->data.playerId) {
        EM_ASM_ARGS({
            var player = malContexts[$0].players[$1];
            if (player && player.sourceNode) {
                player.sourceNode.loop = $2;
            }
        }, context->data.contextId, player->data.playerId, looping);
    }
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    MalContext *context = player->context;
    if (!context || !context->data.contextId || !player->data.playerId) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    int state = EM_ASM_INT({
        var player = malContexts[$0].players[$1];
        if (!player) {
            return 0;
        } else if (player.sourceNode) {
            return 1;
        } else if (player.pausedTime) {
            return 2;
        } else {
            return 0;
        }
    }, context->data.contextId, player->data.playerId);
    switch (state) {
        case 0: default: return MAL_PLAYER_STATE_STOPPED;
        case 1: return MAL_PLAYER_STATE_PLAYING;
        case 2: return MAL_PLAYER_STATE_PAUSED;
    }
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState,
                               MalPlayerState state) {
    (void)oldState;
    MalContext *context = player->context;
    if (!context || !context->data.contextId || !player->data.playerId) {
        return false;
    }

    // NOTE: A new AudioBufferSourceNode must be created everytime it is played.
    if (state == MAL_PLAYER_STATE_STOPPED || state == MAL_PLAYER_STATE_PAUSED) {
        EM_ASM_ARGS({
            var player = malContexts[$0].players[$1];
            var pause = $2;
            if (player) {
                if (pause && player.startTime) {
                    player.pausedTime = Date.now() - player.startTime;
                } else {
                    player.pausedTime = null;
                }
                player.startTime = null;

                if (player.sourceNode) {
                    player.sourceNode.onended = null;
                    player.sourceNode.stop();
                    player.sourceNode.disconnect();
                    player.sourceNode = null;
                }
                if (player.gainNode) {
                    player.gainNode.disconnect();
                    player.gainNode = null;
                }
            }
        }, context->data.contextId, player->data.playerId, (state == MAL_PLAYER_STATE_PAUSED));
        return true;
    } else if (player->buffer && player->buffer->data.bufferId) {
        EM_ASM_ARGS({
            var contextData = malContexts[$0];
            var player = contextData.players[$1];
            if (player) {
                try {
                    player.sourceNode = contextData.context.createBufferSource();
                    player.gainNode = contextData.context.createGain();
                    player.sourceNode.connect(player.gainNode);
                    player.gainNode.connect(contextData.outputNode);
                    player.sourceNode.buffer = contextData.buffers[$2];
                } catch (e) { }
            }
        }, context->data.contextId, player->data.playerId, player->buffer->data.bufferId);
        _malPlayerSetGain(player, player->gain);
        _malPlayerSetLooping(player, player->looping);
        int success = EM_ASM_INT({
            var contextData = malContexts[$0];
            var player = contextData.players[$1];
            if (player) {
                player.sourceNode.onended = function() {
                    player.pausedTime = null;
                    player.startTime = null;
                    player.sourceNode.onended = null;
                    player.sourceNode.disconnect();
                    player.sourceNode = null;
                    if (player.gainNode) {
                        player.gainNode.disconnect();
                        player.gainNode = null;
                    }
                    if (player.onFinishedIdLow || player.onFinishedIdHigh) {
                        try {
                            Module.ccall('_malHandleOnFinishedCallback2', 'void',
                                         ['number', 'number'],
                                         [ player.onFinishedIdHigh, player.onFinishedIdLow ]);
                        } catch (e) { }
                    }
                };
                try {
                    if (player.pausedTime && player.sourceNode.buffer) {
                        var playTime = ((player.pausedTime / 1000) %
                                        player.sourceNode.buffer.duration);
                        player.startTime = Date.now() - playTime * 1000;
                        player.sourceNode.start(0, playTime);
                    } else {
                        player.startTime = Date.now();
                        player.sourceNode.start();
                    }
                    player.pausedTime = null;
                    return 1;
                } catch (e) {
                    player.startTime = null;
                    player.pausedTime = null;
                    return 0;
                }
            } else {
                return 0;
            }
        }, context->data.contextId, player->data.playerId);
        return success != 0;
    } else {
        return false;
    }
}

EMSCRIPTEN_KEEPALIVE
static void _malHandleOnFinishedCallback2(uint32_t onFinishedHighId, uint32_t onFinishedLowId) {
    uint64_t onFinishedId = (((uint64_t)onFinishedHighId) << 32) | onFinishedLowId;
    _malHandleOnFinishedCallback(onFinishedId);
}

#endif
