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

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IOS == 1

#include "mal_audio_coreaudio.h"
#include <AVFoundation/AVFoundation.h>

static void _malCheckRoutes(MalContext *context) {
    if (context) {
        memset(context->routes, 0, sizeof(context->routes));
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSArray *outputs = session.currentRoute.outputs;
        for (AVAudioSessionPortDescription *port in outputs) {
            // This covers all the ports up to iOS 10
            if ([port.portType isEqualToString:AVAudioSessionPortHeadphones]) {
                context->routes[MAL_ROUTE_HEADPHONES] = true;
            } else if ([port.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]) {
                context->routes[MAL_ROUTE_SPEAKER] = true;
            } else if ([port.portType isEqualToString:AVAudioSessionPortBuiltInReceiver]) {
                context->routes[MAL_ROUTE_RECIEVER] = true;
            } else if ([port.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
                       [port.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
                       [port.portType isEqualToString:AVAudioSessionPortBluetoothLE] ||
                       [port.portType isEqualToString:AVAudioSessionPortAirPlay]) {
                context->routes[MAL_ROUTE_WIRELESS] = true;
            } else if ([port.portType isEqualToString:AVAudioSessionPortLineOut] ||
                       [port.portType isEqualToString:AVAudioSessionPortHDMI] ||
                       [port.portType isEqualToString:AVAudioSessionPortUSBAudio] ||
                       [port.portType isEqualToString:AVAudioSessionPortCarAudio]) {
                context->routes[MAL_ROUTE_LINEOUT] = true;
            }
        }
    }
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
    } else if ([AVAudioSessionRouteChangeNotification isEqualToString:nsName]) {
        _malCheckRoutes(context);
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
    _malAddNotification(context, (__bridge CFStringRef)AVAudioSessionRouteChangeNotification);
    _malAddNotification(context,
                        (__bridge CFStringRef)AVAudioSessionMediaServicesWereResetNotification);
}

static void _malContextWillDispose(MalContext *context) {
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(), context);
}

static void _malContextDidSetActive(MalContext *context, const bool active) {
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];
    NSError *error;

    if (active) {
        // Set sample rate
        if (context->sampleRate > 0) {
            [audioSession setPreferredSampleRate:context->sampleRate error:&error];
            if (error) {
                MAL_LOG("Couldn't set sample rate to %f. %s", context->sampleRate,
                        error.localizedDescription.UTF8String);
                error = nil;
            }
        }
        // Set Category
        // allowBackgroundMusic might need to be an option
        bool allowBackgroundMusic = true;
        NSString *category = (allowBackgroundMusic ? AVAudioSessionCategoryAmbient :
                              AVAudioSessionCategorySoloAmbient);

        [audioSession setCategory:category error:&error];
        if (error) {
            MAL_LOG("Couldn't set audio session category. %s",
                    error.localizedDescription.UTF8String);
            error = nil;
        }
        _malCheckRoutes(context);
    }

    // NOTE: Setting the audio session to active should happen last
    [[AVAudioSession sharedInstance] setActive:active error:&error];
    if (error) {
        MAL_LOG("Couldn't set audio session to active (%s). %s", (active ? "true" : "false"),
                error.localizedDescription.UTF8String);
        error = nil;
    }
}

#endif
