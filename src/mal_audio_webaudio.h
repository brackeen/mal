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

#ifndef _MAL_AUDIO_WEBAUDIO_H_
#define _MAL_AUDIO_WEBAUDIO_H_

#include <emscripten/emscripten.h>

static int next_context_id = 1;
static int next_buffer_id = 1;
static int next_player_id = 1;

struct _mal_context {
    int context_id;
};

struct _mal_buffer {
    int buffer_id;
};

struct _mal_player {
    int player_id;
};

#include "mal_audio_abstract.h"

// MARK: Context

static bool _mal_context_init(mal_context *context) {
    int success = EM_ASM_INT({
        mal_contexts = window.mal_contexts || {};
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
            mal_contexts[$0] = data;
            return 1;
        } else {
            return 0;
        }
    }, next_context_id);
    if (success) {
        context->data.context_id = next_context_id;
        next_context_id++;
        return true;
    } else {
        return false;
    }
}

static void _mal_context_dispose(mal_context *context) {
    if (context->data.context_id) {
        EM_ASM_ARGS({
            delete mal_contexts[$0];
        }, context->data.context_id);
        context->data.context_id = 0;
    }
}

static void _mal_context_set_active(mal_context *context, bool active) {
    // Do nothing
}

static void _mal_context_set_mute(mal_context *context, bool mute) {
    _mal_context_set_gain(context, context->gain);
}

static void _mal_context_set_gain(mal_context *context, float gain) {
    if (context->data.context_id) {
        float total_gain = context->mute ? 0.0f : gain;
        EM_ASM_ARGS({
            mal_contexts[$0].outputNode.gain.value = $1;
        }, context->data.context_id, total_gain);
    }
}

// MARK: Buffer

static bool _mal_buffer_init(mal_context *context, mal_buffer *buffer,
                             const void *copied_data, void *managed_data,
                             const mal_deallocator_func data_deallocator) {
    if (!context->data.context_id) {
        return false;
    }
    const void *data = copied_data ? copied_data : managed_data;
    mal_format format = buffer->format;
    // Convert from interleaved 16-bit signed integer to non-interleaved 32-bit float.
    // This uses Emscripten's HEAP16 Int16Array to access the data, and may not be future-proof.
    int success = EM_ASM_INT({
        var context_data = mal_contexts[$0];
        var channels = $2;
        var frames = $3;
        var sample_rate = $4;
        var data = $5 >> 1;
        var buffer;
        try {
            buffer = context_data.context.createBuffer(channels, frames, sample_rate);
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
            context_data.buffers[$1] = buffer;
            return 1;
        } else {
            return 0;
        }
    }, context->data.context_id, next_buffer_id,
                format.num_channels, buffer->num_frames, format.sample_rate, data);
    if (success) {
        buffer->data.buffer_id = next_buffer_id;
        next_buffer_id++;
        // Data is always copied. Delete any managed data immediately.
        if (managed_data && data_deallocator) {
            data_deallocator(managed_data);
        }
        return true;
    } else {
        return false;
    }
}

static void _mal_buffer_dispose(mal_buffer *buffer) {
    mal_context *context = buffer->context;
    if (context && context->data.context_id && buffer->data.buffer_id) {
        EM_ASM_ARGS({
            delete mal_contexts[$0].buffers[$1];
        }, context->data.context_id, buffer->data.buffer_id);
        buffer->data.buffer_id = 0;
    }
}

// MARK: Player

static bool _mal_player_init(mal_player *player) {
    mal_context *context = player->context;
    if (context && context->data.context_id) {
        player->data.player_id = next_player_id;
        next_player_id++;
        EM_ASM_ARGS({
            mal_contexts[$0].players[$1] = { };
        }, context->data.context_id, player->data.player_id);
        return true;
    } else {
        return false;
    }
}

static void _mal_player_dispose(mal_player *player) {
    mal_context *context = player->context;
    if (context && context->data.context_id && player->data.player_id) {
        EM_ASM_ARGS({
            var player = mal_contexts[$0].players[$1];
            if (player.gainNode) {
                player.gainNode.disconnect();
            }
            if (player.sourceNode) {
                player.sourceNode.disconnect();
            }
            delete mal_contexts[$0].players[$1];
        }, context->data.context_id, player->data.player_id);
    }
    player->data.player_id = 0;
}

static bool _mal_player_set_format(mal_player *player, mal_format format) {
    // Do nothing - format determined by attached buffer
    return true;
}

static bool _mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    mal_context *context = player->context;
    if (!context || !player->data.player_id) {
        return false;
    } else if (!buffer) {
        return true;
    } else if (!mal_context_format_is_valid(context, buffer->format) || !buffer->data.buffer_id) {
        return false;
    } else {
        return true;
    }
}

static void _mal_player_set_mute(mal_player *player, bool mute) {
    _mal_player_set_gain(player, player->gain);
}

static void _mal_player_set_gain(mal_player *player, float gain) {
    mal_context *context = player->context;
    if (context && context->data.context_id && player->data.player_id) {
        float total_gain = player->mute ? 0.0f : gain;
        EM_ASM_ARGS({
            var player = mal_contexts[$0].players[$1];
            if (player && player.gainNode) {
                player.gainNode.gain.value = $2;;
            }
        }, context->data.context_id, player->data.player_id, total_gain);
    }
}

static void _mal_player_set_looping(mal_player *player, bool looping) {
    mal_context *context = player->context;
    if (context && context->data.context_id && player->data.player_id) {
        EM_ASM_ARGS({
            var player = mal_contexts[$0].players[$1];
            if (player && player.sourceNode) {
                player.sourceNode.loop = $2;
            }
        }, context->data.context_id, player->data.player_id, looping);
    }
}

static mal_player_state _mal_player_get_state(const mal_player *player) {
    mal_context *context = player->context;
    if (!context || !context->data.context_id || !player->data.player_id) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    int state = EM_ASM_INT({
        var player = mal_contexts[$0].players[$1];
        if (!player) {
            return 0;
        } else if (player.sourceNode) {
            return 1;
        } else if (player.pausedTime) {
            return 2;
        } else {
            return 0;
        }
    }, context->data.context_id, player->data.player_id);
    switch (state) {
        case 0: default: return MAL_PLAYER_STATE_STOPPED;
        case 1: return MAL_PLAYER_STATE_PLAYING;
        case 2: return MAL_PLAYER_STATE_PAUSED;
    }
}

static bool _mal_player_set_state(mal_player *player, mal_player_state old_state,
                                  mal_player_state state) {
    mal_context *context = player->context;
    if (!context || !context->data.context_id || !player->data.player_id) {
        return false;
    }

    // NOTE: A new AudioBufferSourceNode must be created everytime it is played.
    if (state == MAL_PLAYER_STATE_STOPPED || state == MAL_PLAYER_STATE_PAUSED) {
        EM_ASM_ARGS({
            var player = mal_contexts[$0].players[$1];
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
        }, context->data.context_id, player->data.player_id, (state == MAL_PLAYER_STATE_PAUSED));
        return true;
    } else if (player->buffer && player->buffer->data.buffer_id) {
        EM_ASM_ARGS({
            var context_data = mal_contexts[$0];
            var player = context_data.players[$1];
            if (player) {
                try {
                    player.sourceNode = context_data.context.createBufferSource();
                    player.gainNode = context_data.context.createGain();
                    player.sourceNode.connect(player.gainNode);
                    player.gainNode.connect(context_data.outputNode);
                    player.sourceNode.buffer = context_data.buffers[$2];
                } catch (e) { }
            }
        }, context->data.context_id, player->data.player_id, player->buffer->data.buffer_id);
        _mal_player_set_gain(player, player->gain);
        _mal_player_set_looping(player, player->looping);
        int success = EM_ASM_INT({
            var context_data = mal_contexts[$0];
            var player = context_data.players[$1];
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
                    try {
                        Module.ccall('_mal_on_finished', 'void', ['number', 'number'], [ $0, $1 ]);
                    } catch (e) { }
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
        }, context->data.context_id, player->data.player_id);
        return success != 0;
    } else {
        return false;
    }
}

EMSCRIPTEN_KEEPALIVE void _mal_on_finished(int context_id, int player_id) {
    // Find the player
    mal_player *player = NULL;
    for (unsigned int i = 0; i < contexts.length; i++) {
        mal_context *context = contexts.values[i];
        if (context->data.context_id == context_id) {
            for (unsigned int j = 0; j < context->players.length; j++) {
                mal_player *p = context->players.values[j];
                if (p->data.player_id == player_id) {
                    player = p;
                    break;
                }
            }
        }

        if (player) {
            break;
        }
    }
    if (player && player->on_finished) {
        player->on_finished(player->on_finished_user_data, player);
    }
}

#endif
