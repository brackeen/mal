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

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_OSX
#include <IOKit/audio/IOAudioTypes.h> // For terminal types
#include "mal_audio_coreaudio.h"

void malContextPollEvents(MalContext *context) {
    (void)context;
    // Do nothing
}

static bool _malAttemptRestart(MalContext *context);

// MARK: Notifications

typedef enum {
    MAL_NOTIFICATION_TYPE_DEVICE_CHANGED = 0,
    MAL_NOTIFICATION_TYPE_RESTART
} MalNotificationType;

struct MalNotification {
    MalContext *context;
    MalNotificationType type;
    uintptr_t data;
};

typedef struct ok_vec_of(struct MalNotification) MalNotificationVec;

static MalNotificationVec _malPendingNotifications = { 0 };

static void _malCancelNotifications(MalContext *context) {
    pthread_mutex_lock(&globalMutex);
    for (size_t i = 0; i < _malPendingNotifications.count;) {
        struct MalNotification *notification = ok_vec_get_ptr(&_malPendingNotifications, i);
        if (notification->context == context) {
            ok_vec_remove_at(&_malPendingNotifications, i);
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&globalMutex);
}

static void _malHandleNotifications() {
    pthread_mutex_lock(&globalMutex);
    for (size_t i = 0; i < _malPendingNotifications.count;) {
        struct MalNotification *notification = ok_vec_get_ptr(&_malPendingNotifications, i);
        bool handled = true;
        if (notification->type == MAL_NOTIFICATION_TYPE_DEVICE_CHANGED) {
            _malContextCheckRoutes(notification->context);
            _malContextSetSampleRate(notification->context);
        } else if (notification->type == MAL_NOTIFICATION_TYPE_RESTART) {
            handled = _malAttemptRestart(notification->context);
            if (!handled) {
                // Give up after 100 attempts
                notification->data++;
                if (notification->data >= 100) {
                    handled = true;
                }
            }
        }

        if (handled) {
            ok_vec_remove_at(&_malPendingNotifications, i);
        } else {
            i++;
        }
    }
    if (_malPendingNotifications.count > 0) {
        dispatch_async(dispatch_get_main_queue(), ^{
            _malHandleNotifications();
        });
    }
    pthread_mutex_unlock(&globalMutex);
}


static void _malAddNotification(MalContext *context, MalNotificationType type) {
    pthread_mutex_lock(&globalMutex);
    struct MalNotification *notification = ok_vec_push_new(&_malPendingNotifications);
    if (notification) {
        notification->context = context;
        notification->type = type;
        notification->data = 0;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        _malHandleNotifications();
    });
    pthread_mutex_unlock(&globalMutex);
}

// MARK: macOS helpers

static UInt32 _malGetPropertyUInt32(AudioObjectID object,
                                    AudioObjectPropertySelector selector,
                                    AudioObjectPropertyScope scope,
                                    UInt32 defaultValue) {
    OSStatus status = noErr;
    UInt32 propertyValue = defaultValue;
    UInt32 propertySize = sizeof(propertyValue);
    AudioObjectPropertyAddress  propertyAddress;
    propertyAddress.mSelector = selector;
    propertyAddress.mScope = scope;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    status = AudioObjectGetPropertyData(object, &propertyAddress, 0, NULL,
                                        &propertySize, &propertyValue);
    if (status != noErr) {
        return defaultValue;
    } else {
        return propertyValue;
    }
}

static void _malContextCheckRoutes(MalContext *context) {
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

    // TODO: Check all devices? There may be multiple output devices.
    // kAudioHardwarePropertyDevices

    // Get current output device
    defaultOutputDeviceID = _malGetPropertyUInt32(kAudioObjectSystemObject,
                                                  kAudioHardwarePropertyDefaultOutputDevice,
                                                  kAudioObjectPropertyScopeGlobal,
                                                  kAudioDeviceUnknown);
    if (defaultOutputDeviceID == kAudioDeviceUnknown) {
        goto done;
    }

    // Get transport type - quick exit if wireless
    transportType = _malGetPropertyUInt32(defaultOutputDeviceID, kAudioDevicePropertyTransportType,
                                          kAudioObjectPropertyScopeOutput,
                                          kAudioDeviceTransportTypeUnknown);
    if (transportType == kAudioDeviceTransportTypeBluetooth ||
        transportType == kAudioDeviceTransportTypeBluetoothLE ||
        transportType == kAudioDeviceTransportTypeAirPlay) {
        context->routes[MAL_ROUTE_WIRELESS] = true;
        goto done;
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
        terminalType = _malGetPropertyUInt32(streamIds[i], kAudioStreamPropertyTerminalType,
                                             kAudioObjectPropertyScopeOutput,
                                             kAudioStreamTerminalTypeUnknown);

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
        } else if (terminalType != kAudioStreamTerminalTypeUnknown) {
            context->routes[MAL_ROUTE_SPEAKER] = true;
        }
    }

done:
    free(streamIds);
}

static bool _malAttemptRestart(MalContext *context) {
    AudioDeviceID defaultOutputDeviceID;
    defaultOutputDeviceID = _malGetPropertyUInt32(kAudioObjectSystemObject,
                                                  kAudioHardwarePropertyDefaultOutputDevice,
                                                  kAudioObjectPropertyScopeGlobal,
                                                  kAudioDeviceUnknown);
    if (defaultOutputDeviceID != kAudioDeviceUnknown) {
        _malContextReset(context);
        return true;
    } else {
        return false;
    }
}

static OSStatus _malOnDeviceChangedHandler(AudioObjectID inObjectID,
                                           UInt32 inNumberAddresses,
                                           const AudioObjectPropertyAddress inAddresses[],
                                           void *inClientData) {
    MalContext *context = inClientData;
    _malAddNotification(context, MAL_NOTIFICATION_TYPE_DEVICE_CHANGED);
    return noErr;
}


// Test with `sudo killall coreaudiod`
static OSStatus _malOnRestartHandler(AudioObjectID inObjectID,
                                     UInt32 inNumberAddresses,
                                     const AudioObjectPropertyAddress inAddresses[],
                                     void *inClientData) {
    MalContext *context = inClientData;
    _malAddNotification(context, MAL_NOTIFICATION_TYPE_RESTART);
    return noErr;
}

static void _malContextSetSampleRate(MalContext *context) {
    AudioDeviceID defaultOutputDeviceID;
    AudioObjectPropertyAddress propertyAddress;
    OSStatus status = noErr;

    defaultOutputDeviceID = _malGetPropertyUInt32(kAudioObjectSystemObject,
                                                  kAudioHardwarePropertyDefaultOutputDevice,
                                                  kAudioObjectPropertyScopeGlobal,
                                                  kAudioDeviceUnknown);
    if (defaultOutputDeviceID == kAudioDeviceUnknown) {
        context->actualSampleRate = 44100;
        return;
    }

    // Get sample rate
    Float64 currentSampleRate = 0;
    UInt32 sampleRateSize = sizeof(currentSampleRate);
    propertyAddress.mSelector = kAudioDevicePropertyNominalSampleRate;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;
    AudioObjectGetPropertyData(defaultOutputDeviceID, &propertyAddress,
                               0, NULL, &sampleRateSize, &currentSampleRate);

    // Set sample rate
    Float64 requestedSampleRate = context->requestedSampleRate;
    if (requestedSampleRate > MAL_DEFAULT_SAMPLE_RATE &&
        !_malSampleRatesEqual(requestedSampleRate, currentSampleRate)) {
        // Find best match in kAudioDevicePropertyAvailableNominalSampleRates
        // If a valid value isn't chosen, setting the sample rate could fail.
        AudioValueRange *values = NULL;
        UInt32 size = 0;
        propertyAddress.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
        propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
        propertyAddress.mElement = kAudioObjectPropertyElementMaster;

        status = AudioObjectGetPropertyDataSize(defaultOutputDeviceID, &propertyAddress,
                                                0, NULL, &size);
        UInt32 numElements = size / sizeof(*values);

        if (status == noErr && numElements > 0) {
            values = malloc(sizeof(*values) * numElements);
            if (values) {
                status = AudioObjectGetPropertyData(defaultOutputDeviceID, &propertyAddress,
                                                    0, NULL, &size, values);
                if (status == noErr) {
                    double closestSampleRate = values[0].mMinimum;
                    for (UInt32 i = 0; i < numElements; i++) {
                        if (requestedSampleRate >= values[i].mMinimum &&
                            requestedSampleRate <= values[i].mMaximum) {
                            closestSampleRate = requestedSampleRate;
                            break;
                        } else {
                            if (fabs(requestedSampleRate - values[i].mMinimum) <=
                                fabs(closestSampleRate - values[i].mMinimum)) {
                                closestSampleRate = values[i].mMinimum;
                            }
                            if (fabs(requestedSampleRate - values[i].mMaximum) <=
                                fabs(closestSampleRate - values[i].mMaximum)) {
                                closestSampleRate = values[i].mMaximum;
                            }
                        }
                    }
                    requestedSampleRate = closestSampleRate;
                }
                free(values);
            }
        }

        // Set sample rate (if different from current rate)
        if (!_malSampleRatesEqual(requestedSampleRate, currentSampleRate)) {
            // Set sample rate
            Float64 sampleRate = (Float64)requestedSampleRate;
            propertyAddress.mSelector = kAudioDevicePropertyNominalSampleRate;
            propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
            propertyAddress.mElement = kAudioObjectPropertyElementMaster;
            status = AudioObjectSetPropertyData(defaultOutputDeviceID, &propertyAddress,
                                                0, NULL, sizeof(sampleRate), &sampleRate);

            // Wait for change to occur.
            // This usually happens within 0.0001 seconds. Max wait 0.25 seconds.
            if (status == noErr) {
                for (int i = 0; i < 250; i++) {
                    Float64 newSampleRate = 0.0;
                    status = AudioObjectGetPropertyData(defaultOutputDeviceID, &propertyAddress,
                                                        0, NULL, &sampleRateSize, &newSampleRate);
                    if (status == noErr &&
                        !_malSampleRatesEqual(newSampleRate, currentSampleRate)) {
                        currentSampleRate = newSampleRate;
                        break;
                    }
                    usleep(1000);
                }
            }
        }
    }
    context->actualSampleRate = currentSampleRate;
}

static void _malContextDidCreate(MalContext *context) {
    AudioObjectPropertyAddress propertyAddress;

    // Set device listener
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                   &_malOnDeviceChangedHandler, context);

    // Set restart listener
    propertyAddress.mSelector = kAudioHardwarePropertyServiceRestarted;
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                   &_malOnRestartHandler, context);
}

static void _malContextWillDispose(MalContext *context) {
    AudioObjectPropertyAddress  propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                      &_malOnDeviceChangedHandler, context);

    propertyAddress.mSelector = kAudioHardwarePropertyServiceRestarted;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &propertyAddress,
                                      &_malOnRestartHandler, context);

    _malCancelNotifications(context);
}

static void _malContextDidSetActive(MalContext *context, bool active) {
    // Do nothing
}

#endif
