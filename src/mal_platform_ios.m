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

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IPHONE

#include "mal_audio_coreaudio.h"
#include <AVFoundation/AVFoundation.h>
#include <UIKit/UIKit.h> // For notifications

static void _mal_check_routes(mal_context *context) {
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

static void _mal_notification_handler(CFNotificationCenterRef center, void *observer,
                                      CFStringRef name, const void *object,
                                      CFDictionaryRef userInfo) {
    NSString *nsName = (__bridge NSString *)name;
    mal_context *context = (mal_context *)observer;
    if ([AVAudioSessionInterruptionNotification isEqualToString:nsName]) {
        // NOTE: Test interruption on iOS by activating Siri
        NSDictionary *dict = (__bridge NSDictionary *)userInfo;
        NSNumber *interruptionType = dict[AVAudioSessionInterruptionTypeKey];
        if (interruptionType) {
            if ([interruptionType integerValue] == AVAudioSessionInterruptionTypeBegan) {
                mal_context_set_active(context, false);
            } else if ([interruptionType integerValue] == AVAudioSessionInterruptionTypeEnded) {
                mal_context_set_active(context, true);
            }
        }
    } else if ([AVAudioSessionRouteChangeNotification isEqualToString:nsName]) {
        _mal_check_routes(context);
    } else if ([AVAudioSessionMediaServicesWereResetNotification isEqualToString:nsName]) {
        _mal_context_reset(context);
    } else if ([UIApplicationDidEnterBackgroundNotification isEqualToString:nsName]) {
        mal_context_set_active(context, false);
    } else if ([UIApplicationWillEnterForegroundNotification isEqualToString:nsName]) {
        mal_context_set_active(context, true);
    }
}

static void _mal_add_notification(mal_context *context, CFStringRef name) {
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(),
                                    context,
                                    &_mal_notification_handler,
                                    name,
                                    NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
}

static void _mal_context_did_create(mal_context *context) {
    _mal_add_notification(context, (__bridge CFStringRef)AVAudioSessionInterruptionNotification);
    _mal_add_notification(context, (__bridge CFStringRef)AVAudioSessionRouteChangeNotification);
    _mal_add_notification(context, (__bridge CFStringRef)AVAudioSessionMediaServicesWereResetNotification);
    // Removed this because there is not an equivilent for Android.
    // User must call mal_context_set_active manually.
    //_mal_add_notification(context,
    //                     (__bridge CFStringRef)UIApplicationDidEnterBackgroundNotification);
    //_mal_add_notification(context,
    //                     (__bridge CFStringRef)UIApplicationWillEnterForegroundNotification);
}

static void _mal_context_will_dispose(mal_context *context) {
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(), context);
}

static void _mal_context_did_set_active(mal_context *context, const bool active) {
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];

    if (active) {
        // Set sample rate
        if (context->sample_rate > 0) {
            [audioSession setPreferredSampleRate:context->sample_rate error:NULL];
        }
        // Set Category
        // allowBackgroundMusic might need to be an option
        bool allowBackgroundMusic = true;
        NSString *category = allowBackgroundMusic ? AVAudioSessionCategoryAmbient : AVAudioSessionCategorySoloAmbient;
        NSError *categoryError;
        [audioSession setCategory:category error:&categoryError];
        if (categoryError) {
            NSLog(@"mal: Error setting audio session category. Error: %@",
                  [categoryError localizedDescription]);
        }
        _mal_check_routes(context);
    }

    // NOTE: Setting the audio session to active should happen after setting the AL context
    NSError *activeError;
    [[AVAudioSession sharedInstance] setActive:active error:&activeError];
    if (activeError) {
        NSLog(@"mal: Error setting audio session to active (%@). Error: %@",
              active ? @"true" : @"false", [activeError localizedDescription]);
    }
}

#endif
