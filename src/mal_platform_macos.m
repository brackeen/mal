/*
 Mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2018 David Brackeen

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

#ifdef USE_PULSEAUDIO

#include "mal_audio_pulseaudio.h"

static void _malContextDidCreate(MalContext *context) {
    (void)context;
    // Do nothing
}

static void _malContextWillDispose(MalContext *context) {
    (void)context;
    // Do nothing
}

static void _malContextDidSetActive(MalContext *context, bool active) {
    (void)context;
    (void)active;
    // Do nothing
}

#else

#include "mal_audio_coreaudio.h"

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

typedef struct ok_queue_of(struct MalNotification) MalNotificationQueue;

static MalNotificationQueue _malPendingNotifications = OK_QUEUE_INIT;

static void _malCancelNotifications(MalContext *context) {
    bool hasMoreNotifications = false;
    MalNotificationQueue moreNotifications = OK_QUEUE_INIT;
    struct MalNotification notification;
    while (ok_queue_pop(&_malPendingNotifications, &notification)) {
        if (notification.context != context) {
            hasMoreNotifications = true;
            ok_queue_push(&moreNotifications, notification);
        }
    }
    if (hasMoreNotifications) {
        while (ok_queue_pop(&moreNotifications, &notification)) {
            ok_queue_push(&_malPendingNotifications, notification);
        }
        ok_queue_deinit(&moreNotifications);
    }
}

static void _malHandleNotifications(void *userData) {
    (void)userData;
    bool hasUnhandledNotifications = false;
    MalNotificationQueue unhandledNotifications = OK_QUEUE_INIT;
    struct MalNotification notification;
    while (ok_queue_pop(&_malPendingNotifications, &notification)) {
        bool handled = true;
        if (notification.type == MAL_NOTIFICATION_TYPE_DEVICE_CHANGED) {
            // Do nothing, for now
        } else if (notification.type == MAL_NOTIFICATION_TYPE_RESTART) {
            handled = _malAttemptRestart(notification.context);
            if (!handled) {
                // Give up after 200 attempts
                notification.data++;
                if (notification.data >= 200) {
                    handled = true;
                }
            }
        }
        if (!handled) {
            hasUnhandledNotifications = true;
            ok_queue_push(&unhandledNotifications, notification);
        }
    }

    if (hasUnhandledNotifications) {
        while (ok_queue_pop(&unhandledNotifications, &notification)) {
            ok_queue_push(&_malPendingNotifications, notification);
        }
        ok_queue_deinit(&unhandledNotifications);
        dispatch_async_f(dispatch_get_main_queue(), NULL, _malHandleNotifications);
    }
}


static void _malAddNotification(MalContext *context, MalNotificationType type) {
    struct MalNotification notification;
    notification.context = context;
    notification.type = type;
    notification.data = 0;
    ok_queue_push(&_malPendingNotifications, notification);
    dispatch_async_f(dispatch_get_main_queue(), NULL, _malHandleNotifications);
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
        // It may take a while for the reset to complete.
        usleep(4000);
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

#endif
