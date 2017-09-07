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

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_OSX
#include <IOKit/audio/IOAudioTypes.h> // For terminal types
#include "mal_audio_coreaudio.h"

static AudioDeviceID _malGetDeviceId() {
    OSStatus status = noErr;
    AudioDeviceID defaultOutputDeviceID = kAudioDeviceUnknown;
    UInt32 propertySize = sizeof(defaultOutputDeviceID);
    AudioObjectPropertyAddress  propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL,
                                        &propertySize, &defaultOutputDeviceID);
    if (status != noErr) {
        return kAudioDeviceUnknown;
    } else {
        return defaultOutputDeviceID;
    }
}

static void _malCheckRoutes(MalContext *context) {
    if (!context) {
        return;
    }

    memset(context->routes, 0, sizeof(context->routes));

    AudioObjectPropertyAddress  propertyAddress;
    AudioDeviceID defaultOutputDeviceID;
    UInt32 propertySize;
    AudioStreamID *streamIds = NULL;
    UInt32 streamCount;
    UInt32 transportType;
    UInt32 terminalType;
    OSStatus status = noErr;

    // Get current output device
    defaultOutputDeviceID = _malGetDeviceId();
    if (defaultOutputDeviceID == kAudioDeviceUnknown) {
        goto done;
    }

    // Get transport type
    propertyAddress.mSelector = kAudioDevicePropertyTransportType;
    propertyAddress.mScope = kAudioObjectPropertyScopeOutput;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;
    propertySize = sizeof(transportType);
    status = AudioObjectGetPropertyData(defaultOutputDeviceID, &propertyAddress, 0, NULL,
                                        &propertySize, &transportType);
    if (status == noErr) {
        // Quick exit if wireless
        if (transportType == kAudioDeviceTransportTypeBluetooth ||
            transportType == kAudioDeviceTransportTypeBluetoothLE ||
            transportType == kAudioDeviceTransportTypeAirPlay) {
            context->routes[MAL_ROUTE_WIRELESS] = true;
            goto done;
        }
    }

    // Get stream count
    propertyAddress.mSelector = kAudioDevicePropertyStreams;
    propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;
    propertySize = 0;
    status = AudioObjectGetPropertyDataSize(defaultOutputDeviceID, &propertyAddress, 0, NULL,
                                            &propertySize);
    if (status != noErr) {
        goto done;
    }

    streamCount = propertySize / sizeof(AudioStreamID);
    if (streamCount == 0) {
        goto done;
    }

    // Get stream ids
    streamIds = malloc(propertySize);
    if (!streamIds) {
        goto done;
    }
    status = AudioObjectGetPropertyData(defaultOutputDeviceID, &propertyAddress, 0, NULL,
                                        &propertySize, streamIds);
    if (status != noErr) {
        goto done;
    }

    // Get routes
    for (UInt32 i = 0; i < streamCount; i++) {
        propertyAddress.mSelector = kAudioStreamPropertyTerminalType;
        propertyAddress.mScope = kAudioObjectPropertyScopeOutput;
        propertyAddress.mElement = kAudioObjectPropertyElementMaster;
        propertySize = sizeof(terminalType);

        status = AudioObjectGetPropertyData(streamIds[i], &propertyAddress, 0, NULL,
                                            &propertySize, &terminalType);
        if (status != noErr) {
            continue;
        }

        // Some devices use the terminal type enums defined in IOKit/audio/IOAudioTypes.h,
        // others use the terminal type enums defined in CoreAudio/AudioHardwareBase.h.
        if (terminalType == kAudioStreamTerminalTypeReceiverSpeaker) {
            context->routes[MAL_ROUTE_RECIEVER] = true;
        } else if (terminalType == kAudioStreamTerminalTypeHeadphones ||
                   terminalType == OUTPUT_HEADPHONES ||
                   terminalType == OUTPUT_HEAD_MOUNTED_DISPLAY_AUDIO) {
            context->routes[MAL_ROUTE_HEADPHONES] = true;
        } else if (terminalType == kAudioStreamTerminalTypeLine ||
                   terminalType == kAudioStreamTerminalTypeDigitalAudioInterface ||
                   terminalType == kAudioStreamTerminalTypeHDMI ||
                   terminalType == kAudioStreamTerminalTypeDisplayPort) {
            context->routes[MAL_ROUTE_LINEOUT] = true;
        } else {
            context->routes[MAL_ROUTE_SPEAKER] = true;
        }
    }

done:
    free(streamIds);
}

static OSStatus _malNotificationHandler(AudioObjectID inObjectID,
                                        UInt32 inNumberAddresses,
                                        const AudioObjectPropertyAddress inAddresses[],
                                        void *inClientData) {
    MalContext *context = inClientData;

    // Perform on main queue and wait until finished, so it is always executed before
    // _malContextWillDispose()
    dispatch_sync(dispatch_get_main_queue(), ^{
        _malCheckRoutes(context);
    });

    return noErr;
}

static void _malContextDidCreate(MalContext *context) {
    AudioObjectPropertyAddress  propertyAddress;

    // Set rate
    if (context->sampleRate > 0) {
        Float64 sampleRate = (Float64)context->sampleRate;
        AudioDeviceID defaultOutputDeviceID = _malGetDeviceId();
        if (defaultOutputDeviceID != kAudioDeviceUnknown) {
            propertyAddress.mSelector = kAudioDevicePropertyNominalSampleRate;
            propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
            propertyAddress.mElement = kAudioObjectPropertyElementMaster;
            AudioObjectSetPropertyData(defaultOutputDeviceID, &propertyAddress,
                                       0, NULL, sizeof(sampleRate), &sampleRate);
        }
    }

    // Set device listener
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                   &_malNotificationHandler, context);
}

static void _malContextWillDispose(MalContext *context) {
    AudioObjectPropertyAddress  propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                      &_malNotificationHandler, context);
}

static void _malContextDidSetActive(MalContext *context, bool active) {
    if (active) {
        _malCheckRoutes(context);
    }
}

#endif
