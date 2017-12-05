/*
 Mal
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

#if defined(_DEBUG)
#  define MAL_LOG(s) OutputDebugString("Mal: " s "\n");
#else
#  define MAL_LOG(...) do { } while(0)
#endif

// Use XAudio 2.7 for Windows 7 compatability.
// 1. Install DirectX SDK (June 2010) from
//    https://www.microsoft.com/en-us/download/details.aspx?id=6812
// 2. Add Include Directory "C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Include"
// (Note, XAudio2.h does not compile as C and must be compiled as C++.)
// This macro can be removed in the future, when targeting XAudio 2.8.
#define USE_XAUDIO_2_7 1

#pragma warning(push)
#pragma warning(disable:4668)
#pragma warning(disable:4514)
#include <math.h> // Incude math.h here to avoid warnings
#if USE_XAUDIO_2_7
#include <C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Include\comdecl.h>
#include <C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Include\XAudio2.h>
#else
#include <xaudio2.h>
#pragma comment(lib,"xaudio2.lib")
#endif
#pragma warning(pop)

#include "ok_lib.h"
#include "mal_audio_abstract_types.h"
#include "mal.h"

class MalVoiceCallback; 

struct _MalContext {
#if USE_XAUDIO_2_7
    // Keep a reference to the DLL as a workaround to this bug in XAudio 2.7:
    // https://blogs.msdn.microsoft.com/chuckw/2015/10/09/known-issues-xaudio-2-7/
    HMODULE xAudio2DLL;
#endif
    IXAudio2 *xAudio2;
    IXAudio2MasteringVoice *masteringVoice;
    CRITICAL_SECTION lock;
    struct ok_vec_of(MalCallbackId) finishedCallbackIds;
    bool hasPolledEvents;
    bool shouldUninitializeCOM;
};

struct _MalBuffer {
    int dummy;
};

struct _MalPlayer {
    IXAudio2SourceVoice *sourceVoice;
    MalVoiceCallback *callback;
    MalPlayerState state;
    bool bufferQueued;
    // Same value as MalPlayer, but locked via CRITICAL_SECTION
    MalCallbackId onFinishedId;
};

#define MAL_INCLUDE_SAMPLE_RATE_FUNCTIONS
#include "mal_audio_abstract.h"

#pragma region Context

static bool _malContextInit(MalContext *context, void *androidActivity,
                            const char **errorMissingAudioSystem) {
    (void)androidActivity;

    ok_vec_init(&context->data.finishedCallbackIds);

    if (!InitializeCriticalSectionAndSpinCount(&context->data.lock, 20)) {
        return false;
    }

    // Initialize COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        context->data.shouldUninitializeCOM = false;
    } else if (SUCCEEDED(hr)) {
        context->data.shouldUninitializeCOM = true;
    } else {
        return false;
    }

    // Load DLL
    HMODULE xAudio2DLL = NULL;
    DWORD creationFlags = 0;
#if USE_XAUDIO_2_7
#if defined(_DEBUG)
    xAudio2DLL = LoadLibraryExW(L"XAudioD2_7.DLL", NULL,
        0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    if (xAudio2DLL) {
        creationFlags |= XAUDIO2_DEBUG_ENGINE;
    }
#endif
    if (!xAudio2DLL) {
        xAudio2DLL = LoadLibraryExW(L"XAudio2_7.DLL", NULL,
            0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    }
    if (xAudio2DLL) {
        context->data.xAudio2DLL = xAudio2DLL;
    } else {
        _malContextDispose(context);
        if (!errorMissingAudioSystem) {
            *errorMissingAudioSystem = "XAudio 2.7 (DirectX End-User Runtimes (June 2010))";
        }
        return false;
    }
#endif

    // Create XAudio2 object
    hr = XAudio2Create(&context->data.xAudio2, creationFlags);
    if (FAILED(hr) || context->data.xAudio2 == NULL) {
        _malContextDispose(context);
        return false;
    }

    // Create the output voice
    // Because the szDeviceId is NULL, OnCriticalError won't be raised.
    UINT32 sampleRate = XAUDIO2_DEFAULT_SAMPLERATE;
    if (context->requestedSampleRate > MAL_DEFAULT_SAMPLE_RATE) {
        sampleRate = (UINT32)_malGetClosestSampleRate(context->requestedSampleRate);
    }
    hr = context->data.xAudio2->CreateMasteringVoice(&context->data.masteringVoice, 
                                                     XAUDIO2_DEFAULT_CHANNELS, sampleRate);
    if (FAILED(hr) || context->data.masteringVoice == NULL) {
        _malContextDispose(context);
        return false;
    }

    // Get sample rate
    XAUDIO2_VOICE_DETAILS voiceDetails = { 0 };
    context->data.masteringVoice->GetVoiceDetails(&voiceDetails);
    context->actualSampleRate = voiceDetails.InputSampleRate;

    // Success
    context->active = true;
    return true;
}

static void _malContextDispose(MalContext *context) {
    if (context->data.masteringVoice) {
        context->data.masteringVoice->DestroyVoice();
        context->data.masteringVoice = NULL;
    }
    if (context->data.xAudio2) {
        context->data.xAudio2->Release();
        context->data.xAudio2 = NULL;
    }
#if USE_XAUDIO_2_7
    if (context->data.xAudio2DLL) {
        FreeLibrary(context->data.xAudio2DLL);
        context->data.xAudio2DLL = NULL;
    }
#endif
    if (context->data.shouldUninitializeCOM) {
        context->data.shouldUninitializeCOM = false;
        CoUninitialize();
    }
    DeleteCriticalSection(&context->data.lock);
}

static bool _malContextSetActive(MalContext *context, bool active) {
    if (context->active == active) {
        return true;
    } else if (active) {
        return SUCCEEDED(context->data.xAudio2->StartEngine());
    } else {
        context->data.xAudio2->StopEngine();
        return true;
    }
}

static void _malContextSetMute(MalContext *context, bool mute) {
    (void)mute;
    _malContextSetGain(context, context->gain);
}

static void _malContextSetGain(MalContext *context, float gain) {
    float totalGain = context->mute ? 0.0f : gain;
    context->data.masteringVoice->SetVolume(totalGain);
}

void malContextPollEvents(MalContext *context) {
    if (!context) {
        return;
    }
    context->data.hasPolledEvents = true;

    while (true) {
        bool done = false;
        MalCallbackId onFinishedId = 0;

        EnterCriticalSection(&context->data.lock);
        size_t count = context->data.finishedCallbackIds.count;
        if (count == 0) {
            done = true;
        } else {
            onFinishedId = ok_vec_get(&context->data.finishedCallbackIds, count - 1);
            ok_vec_remove_at(&context->data.finishedCallbackIds, count - 1);
        }
        LeaveCriticalSection(&context->data.lock);

        if (done) {
            break;
        } else {
            _malHandleOnFinishedCallback(onFinishedId);
        }
    }
}

#pragma endregion

#pragma region Buffer

static bool _malBufferInit(MalContext *context, MalBuffer *buffer,
                           const void *copiedData, void *managedData,
                           malDeallocatorFunc dataDeallocator) {
    (void)context;
    if (managedData) {
        buffer->managedData = managedData;
        buffer->managedDataDeallocator = dataDeallocator;
    } else {
        const size_t dataLength = ((buffer->format.bitDepth / 8) *
            buffer->format.numChannels * buffer->numFrames);
        void *newBuffer = malloc(dataLength);
        if (!newBuffer) {
            return false;
        }
        memcpy(newBuffer, copiedData, dataLength);
        buffer->managedData = newBuffer;
        buffer->managedDataDeallocator = free;
    }
    return true;
}

static void _malBufferDispose(MalBuffer *buffer) {
    (void)buffer;
    // Do nothing
}

#pragma endregion

#pragma region Player

class MalVoiceCallback : public IXAudio2VoiceCallback {
private:
    MalPlayer * const player;

public:
    MalVoiceCallback(MalPlayer *player) : player(player) { }
    MalVoiceCallback(const MalVoiceCallback&) = delete;
    MalVoiceCallback& operator=(const MalVoiceCallback&) = delete;
    MalVoiceCallback(MalVoiceCallback&&) = delete;
    MalVoiceCallback& operator=(MalVoiceCallback&&) = delete;

    void STDMETHODCALLTYPE OnStreamEnd() override {
        player->data.bufferQueued = false;
        if (player->data.onFinishedId) {
            MalContext *context = player->context;
            if (context && context->data.hasPolledEvents) {
                EnterCriticalSection(&context->data.lock);
                ok_vec_push(&context->data.finishedCallbackIds, player->data.onFinishedId);
                LeaveCriticalSection(&context->data.lock);
            }
        }
    }

    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {
    }

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 bytesRequired) override {
        (void)bytesRequired;
    }

    void STDMETHODCALLTYPE OnBufferEnd(void *pBufferContext) override {
        (void)pBufferContext;
    }

    void STDMETHODCALLTYPE OnBufferStart(void *pBufferContext) override {
        (void)pBufferContext;
    }

    void STDMETHODCALLTYPE OnLoopEnd(void *pBufferContext) override {
        (void)pBufferContext;
    }

    void STDMETHODCALLTYPE OnVoiceError(void *pBufferContext, HRESULT error) override {
        (void)pBufferContext;
        (void)error;
    }
};

static bool _malPlayerInit(MalPlayer *player) {
    player->data.callback = new MalVoiceCallback(player);
    return (player->data.callback != NULL);
}

static void _malPlayerDispose(MalPlayer *player) {
    if (player->data.sourceVoice) {
        player->data.sourceVoice->DestroyVoice();
        player->data.sourceVoice = NULL;
    }
    if (player->data.callback) {
        delete player->data.callback;
        player->data.callback = NULL;
    }
}

static bool _malPlayerSetFormat(MalPlayer *player, MalFormat format) {
    bool success = false;
    if (!player->context) {
        return false;
    }

    double sampleRate = (format.sampleRate <= MAL_DEFAULT_SAMPLE_RATE ?
                         malContextGetSampleRate(player->context) : format.sampleRate);

    // Change sample rate without creating a new voice
    if (player->data.sourceVoice) {
        if (malContextIsFormatEqual(player->context, player->format, format)) {
            return true;
        } else if (player->format.bitDepth == format.bitDepth &&
                   player->format.numChannels == format.numChannels) {
            player->data.sourceVoice->FlushSourceBuffers();
            player->data.bufferQueued = false;
            HRESULT hr = player->data.sourceVoice->SetSourceSampleRate((UINT32)sampleRate);
            success = SUCCEEDED(hr);
        }
    }

    // Create a new voice
    if (!success) {
        if (player->data.sourceVoice) {
            player->data.sourceVoice->DestroyVoice();
            player->data.sourceVoice = NULL;
            player->data.bufferQueued = false;
        }

        WAVEFORMATEX xAudioFormat = { 0 };
        xAudioFormat.wFormatTag = WAVE_FORMAT_PCM;
        xAudioFormat.nChannels = format.numChannels;
        xAudioFormat.nSamplesPerSec = (DWORD)sampleRate;
        xAudioFormat.wBitsPerSample = format.bitDepth;
        xAudioFormat.nBlockAlign = (xAudioFormat.nChannels * xAudioFormat.wBitsPerSample) / 8u;
        xAudioFormat.nAvgBytesPerSec = (DWORD)sampleRate * xAudioFormat.nBlockAlign;
        xAudioFormat.cbSize = 0;

        IXAudio2 *xAudio2 = player->context->data.xAudio2;
        HRESULT hr = xAudio2->CreateSourceVoice(&player->data.sourceVoice, &xAudioFormat, 0,
                                                XAUDIO2_DEFAULT_FREQ_RATIO, player->data.callback);
        success = SUCCEEDED(hr) && player->data.sourceVoice;
    }

    if (success) {
        _malPlayerSetGain(player, player->gain);
        _malPlayerSetBuffer(player, player->buffer);
    }
    return success;
}

static bool _malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer) {
    if (!player->data.sourceVoice) {
        return false;
    }
    player->data.sourceVoice->FlushSourceBuffers();
    if (!buffer) {
        player->data.bufferQueued = false;
        return true;
    } else {
        XAUDIO2_BUFFER bufferInfo = { 0 };
        bufferInfo.Flags = XAUDIO2_END_OF_STREAM;
        bufferInfo.AudioBytes = ((buffer->format.bitDepth / 8) *
                                 buffer->format.numChannels * buffer->numFrames);
        bufferInfo.pAudioData = (const BYTE *)buffer->managedData;
        bufferInfo.LoopCount = (UINT32)(player->looping ? XAUDIO2_LOOP_INFINITE : 0);
        bool success = SUCCEEDED(player->data.sourceVoice->SubmitSourceBuffer(&bufferInfo));
        player->data.bufferQueued = success;
        return success;
    }
}

static void _malPlayerSetMute(MalPlayer *player, bool mute) {
    (void)mute;
    _malPlayerSetGain(player, player->gain);
}

static void _malPlayerSetGain(MalPlayer *player, float gain) {
    if (player->data.sourceVoice) {
        float totalGain = player->mute ? 0.0f : gain;
        player->data.sourceVoice->SetVolume(totalGain);
    }
}

static void _malPlayerSetLooping(MalPlayer *player, bool looping) {
    if (_malPlayerGetState(player) == MAL_PLAYER_STATE_STOPPED) {
        _malPlayerSetBuffer(player, player->buffer);
    } else if (player->data.sourceVoice) {
        if (looping) {
            // TODO: Return failure - can't enable looping on an already playing sound
        } else {
            player->data.sourceVoice->ExitLoop();
        }
    }
}

static void _malPlayerDidSetFinishedCallback(MalPlayer *player) {
    if (player->context) {
        EnterCriticalSection(&player->context->data.lock);
        player->data.onFinishedId = player->onFinishedId;
        LeaveCriticalSection(&player->context->data.lock);
    }
}

static MalPlayerState _malPlayerGetState(const MalPlayer *player) {
    if (!player->data.sourceVoice) {
        return MAL_PLAYER_STATE_STOPPED;
    }
    if (player->data.state == MAL_PLAYER_STATE_PLAYING && !player->data.bufferQueued) {
        return MAL_PLAYER_STATE_STOPPED;
    } else {
        return player->data.state;
    }
}

static bool _malPlayerSetState(MalPlayer *player, MalPlayerState oldState, MalPlayerState state) {
    (void)oldState;
    if (!player->data.sourceVoice) {
        return false;
    }
    switch (state) {
    case MAL_PLAYER_STATE_STOPPED: default:
        player->data.sourceVoice->Stop();
        _malPlayerSetBuffer(player, player->buffer);
        break;
    case MAL_PLAYER_STATE_PLAYING:
        if (!player->data.bufferQueued) {
            _malPlayerSetBuffer(player, player->buffer);
        }
        player->data.sourceVoice->Start();
        break;
    case MAL_PLAYER_STATE_PAUSED:
        player->data.sourceVoice->Stop();
        break;
    }
    player->data.state = state;
    return true;
}

#pragma endregion

#endif
