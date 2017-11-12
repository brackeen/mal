/*
 mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2017 David Brackeen

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

#ifndef MAL_AUDIO_XAUDIO2_H
#define MAL_AUDIO_XAUDIO2_H

struct _MalContext {
    int dummy;
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    int dummy;
};

#include "mal_audio_abstract.h"

static bool _malContextInit(MalContext *context) {
    return false;
}

static void _malContextDispose(MalContext *context) {

}

static void _malContextSetActive(MalContext *context, bool active) {

}

static void _malContextSetMute(MalContext *context, bool mute) {

}

static void _malContextSetGain(MalContext *context, float gain) {

}

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           malDeallocatorFunc dataDeallocator) {
    return false;
}

static void _malBufferDispose(MalBuffer *buffer) {

}

static bool _malPlayerInit(MalPlayer *player) {
    return false;
}

static void _malPlayerDispose(MalPlayer *player) {

}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    return false;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    return false;
}

static void _malPlayerSetMute(MalPlayer *player, bool mute) {

}

static void _malPlayerSetGain(MalPlayer *player, float gain) {

}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {

}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {

}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    return MAL_PLAYER_STATE_STOPPED;
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state) {
    return false;
}


#endif