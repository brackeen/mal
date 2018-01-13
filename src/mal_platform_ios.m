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
#if TARGET_OS_IOS || TARGET_OS_TV

#define MAL_INCLUDE_SAMPLE_RATE_FUNCTIONS
#include "mal_audio_coreaudio.h"
#include <AVFoundation/AVFoundation.h>

void malContextPollEvents(MalContext *context) {
    (void)context;
    // Do nothing
}

static void _malNotificationHandler(CFNotificationCenterRef center, void *observer,
                                    CFStringRef name, const void *object,
                                    CFDictionaryRef userInfo) {
    NSString *nsName = (__bridge NSString *)name;
    MalContext *context = (MalContext *)observer;
    if ([AVAudioSessionInterruptionNotification isEqualToString:nsName]) {
        // NOTE: Test interruption on iOS by activating Siri
        NSDictionary *dict = (__bridge NSDictionary *)userInfo;
        NSNumber *interruptionType = dict[AVAudioSessionInterruptionTypeKey];
        if (interruptionType) {
            if ([interruptionType integerValue] == AVAudioSessionInterruptionTypeBegan) {
                malContextSetActive(context, false);
            } else if ([interruptionType integerValue] == AVAudioSessionInterruptionTypeEnded) {
                malContextSetActive(context, true);
            }
        }
    } else if ([AVAudioSessionMediaServicesWereResetNotification isEqualToString:nsName]) {
        _malContextReset(context);
    }
}

static void _malAddNotification(MalContext *context, CFStringRef name) {
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(),
                                    context,
                                    &_malNotificationHandler,
                                    name,
                                    NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
}

static void _malContextDidCreate(MalContext *context) {
    _malAddNotification(context, (__bridge CFStringRef)AVAudioSessionInterruptionNotification);
    _malAddNotification(context,
                        (__bridge CFStringRef)AVAudioSessionMediaServicesWereResetNotification);
}

static void _malContextWillDispose(MalContext *context) {
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(), context);
}

static void _malContextSetSampleRate(MalContext *context) {
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];
    NSError *error = nil;

    if (context->requestedSampleRate > MAL_DEFAULT_SAMPLE_RATE) {
        double requestedSampleRate = _malGetClosestSampleRate(context->requestedSampleRate);
        [audioSession setPreferredSampleRate:requestedSampleRate error:&error];
        if (error) {
            MAL_LOG("Couldn't set sample rate to %f. %s", requestedSampleRate,
                    error.localizedDescription.UTF8String);
            error = nil;
        }
    }
    context->actualSampleRate = audioSession.sampleRate;
}

static void _malContextDidSetActive(MalContext *context, bool active) {
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];
    NSError *error = nil;

    if (active) {
        _malContextSetSampleRate(context);

        // Set Category
        // TODO: Make allowBackgroundMusic an option
        bool allowBackgroundMusic = true;
        NSString *category = (allowBackgroundMusic ? AVAudioSessionCategoryAmbient :
                              AVAudioSessionCategorySoloAmbient);

        [audioSession setCategory:category error:&error];
        if (error) {
            MAL_LOG("Couldn't set audio session category. %s",
                    error.localizedDescription.UTF8String);
            error = nil;
        }
    }

    // NOTE: Setting the audio session to active should happen last
    [audioSession setActive:active error:&error];
    if (error) {
        MAL_LOG("Couldn't set audio session to active (%s). %s", (active ? "true" : "false"),
                error.localizedDescription.UTF8String);
        error = nil;
    }

    if (active) {
        context->actualSampleRate = audioSession.sampleRate;
    }
}

#endif
