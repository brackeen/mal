#if defined(__APPLE__) && TARGET_OS_IPHONE

#include "mal.h"
#include "mal_openal.h"
#include <AVFoundation/AVFoundation.h>

typedef struct {
    bool routes[NUM_MAL_ROUTES];
} mal_context_internal;

static void mal_check_routes(mal_context *context) {
    if (context != NULL && context->internal_data != NULL) {
        mal_context_internal *internal = (mal_context_internal *)context->internal_data;
        memset(internal->routes, 0, sizeof(internal->routes));
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSArray *outputs = session.currentRoute.outputs;
        for (AVAudioSessionPortDescription *port in outputs) {
            // This covers all the ports up to iOS 8 but there could be more in the future.
            if ([port.portType isEqualToString:AVAudioSessionPortHeadphones]) {
                internal->routes[MAL_ROUTE_HEADPHONES] = true;
            }
            else if ([port.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]) {
                internal->routes[MAL_ROUTE_SPEAKER] = true;
            }
            else if ([port.portType isEqualToString:AVAudioSessionPortBuiltInReceiver]) {
                internal->routes[MAL_ROUTE_RECIEVER] = true;
            }
            else if ([port.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
                     [port.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
                     [port.portType isEqualToString:AVAudioSessionPortBluetoothLE] ||
                     [port.portType isEqualToString:AVAudioSessionPortAirPlay]) {
                internal->routes[MAL_ROUTE_WIRELESS] = true;
            }
            else if ([port.portType isEqualToString:AVAudioSessionPortLineOut] ||
                     [port.portType isEqualToString:AVAudioSessionPortHDMI] ||
                     [port.portType isEqualToString:AVAudioSessionPortUSBAudio] ||
                     [port.portType isEqualToString:AVAudioSessionPortCarAudio]) {
                internal->routes[MAL_ROUTE_LINEOUT] = true;
            }
        }
    }
}

static void mal_notification_handler(CFNotificationCenterRef center, void *observer,
                                     CFStringRef name, const void *object,
                                     CFDictionaryRef userInfo) {
    NSString *nsName = (__bridge NSString *)name;
    mal_context *context = (mal_context*)observer;
    if ([AVAudioSessionInterruptionNotification isEqualToString:nsName]) {
        // NOTE: Test interruption on iOS by activating Siri
        NSDictionary *dict = (__bridge NSDictionary*)userInfo;
        NSNumber *interruptionType = dict[AVAudioSessionInterruptionTypeKey];
        if (interruptionType != nil) {
            if ([interruptionType integerValue] == AVAudioSessionInterruptionTypeBegan) {
                mal_context_set_active(context, false);
            }
            else if ([interruptionType integerValue] == AVAudioSessionInterruptionTypeEnded) {
                mal_context_set_active(context, true);
            }
        }
    }
    else if ([AVAudioSessionRouteChangeNotification isEqualToString:nsName]) {
        mal_check_routes(context);
    }
    else if ([UIApplicationDidEnterBackgroundNotification isEqualToString:nsName]) {
        mal_context_set_active(context, false);
    }
    else if ([UIApplicationWillEnterForegroundNotification isEqualToString:nsName]) {
        mal_context_set_active(context, true);
    }
}

static void mal_add_notification(mal_context *context, CFStringRef name) {
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(),
                                    context,
                                    &mal_notification_handler,
                                    name,
                                    NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
}

static void mal_did_create_context(mal_context *context) {
    if (context != NULL && context->internal_data == NULL) {
        context->internal_data = calloc(1, sizeof(mal_context_internal));
    }
    mal_add_notification(context, (__bridge CFStringRef)AVAudioSessionInterruptionNotification);
    mal_add_notification(context, (__bridge CFStringRef)AVAudioSessionRouteChangeNotification);
    mal_add_notification(context, (__bridge CFStringRef)UIApplicationDidEnterBackgroundNotification);
    mal_add_notification(context, (__bridge CFStringRef)UIApplicationWillEnterForegroundNotification);
}

static void mal_will_destory_context(mal_context *context) {
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(), context);
    if (context != NULL && context->internal_data != NULL) {
        free(context->internal_data);
        context->internal_data = NULL;
    }
}

static void mal_did_set_active(mal_context *context, const bool active) {
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];
    
    if (active) {
        // Set Category
        // TODO: allowBackgroundMusic might need to be an option
        bool allowBackgroundMusic = true;
        NSString *category = allowBackgroundMusic ? AVAudioSessionCategoryAmbient : AVAudioSessionCategorySoloAmbient;
        NSError *categoryError = nil;
        [audioSession setCategory:category error:&categoryError];
        if (categoryError != nil) {
            NSLog(@"mal: Error setting audio session category. Error: %@", [categoryError localizedDescription]);
        }
        mal_check_routes(context);
    }
    
    // NOTE: Setting the audio session to active should happen after setting the AL context
    NSError *activeError = nil;
    [[AVAudioSession sharedInstance] setActive:active error:&activeError];
    if (activeError != nil) {
        NSLog(@"mal: Error setting audio session to active (%@). Error: %@", active?@"true":@"false",
              [activeError localizedDescription]);
    }
}

bool mal_context_is_route_enabled(const mal_context *context, const mal_route route) {
    if (context != NULL && context->internal_data != NULL && route < NUM_MAL_ROUTES) {
        mal_context_internal *internal = (mal_context_internal *)context->internal_data;
        return internal->routes[route];
    }
    else {
        return false;
    }
}

#endif
