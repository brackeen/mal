#include "mal.h"
#include "mal_openal.h"
#include <AVFoundation/AVFoundation.h>

typedef struct {
    bool routes[NUM_MAL_ROUTES];
} mal_context_internal;

static void check_routes(mal_context *context) {
    if (context->internal_data != NULL) {
        mal_context_internal *internal = (mal_context_internal *)context->internal_data;
        memset(internal, 0, sizeof(context->internal_data));
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSArray *outputs = session.currentRoute.outputs;
        for (AVAudioSessionPortDescription *port in outputs) {
            if ([port.portType isEqual:AVAudioSessionPortHeadphones]) {
                internal->routes[MAL_ROUTE_HEADPHONES] = true;
            }
            else if ([port.portType isEqual:AVAudioSessionPortBuiltInSpeaker]) {
                internal->routes[MAL_ROUTE_SPEAKER] = true;
            }
            else if ([port.portType isEqual:AVAudioSessionPortBuiltInReceiver]) {
                internal->routes[MAL_ROUTE_RECIEVER] = true;
            }
            else if ([port.portType isEqual:AVAudioSessionPortBluetoothA2DP] ||
                     [port.portType isEqual:AVAudioSessionPortBluetoothLE] ||
                     [port.portType isEqual:AVAudioSessionPortBluetoothHFP] ||
                     [port.portType isEqual:AVAudioSessionPortAirPlay]) {
                internal->routes[MAL_ROUTE_WIRELESS] = true;
            }
            else if ([port.portType isEqual:AVAudioSessionPortLineOut] ||
                     [port.portType isEqual:AVAudioSessionPortHDMI] ||
                     [port.portType isEqual:AVAudioSessionPortUSBAudio] ||
                     [port.portType isEqual:AVAudioSessionPortCarAudio]) {
                internal->routes[MAL_ROUTE_LINEOUT] = true;
            }
        }
    }
}

static void notification_handler(CFNotificationCenterRef center, void *observer,
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
        check_routes(context);
    }
}

static void mal_did_create_context(mal_context *context) {
    if (context->internal_data == NULL) {
        context->internal_data = calloc(1, sizeof(mal_context_internal));
    }
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(),
                                    context,
                                    &notification_handler,
                                    (__bridge CFStringRef)AVAudioSessionInterruptionNotification,
                                    NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
    
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(),
                                    context,
                                    &notification_handler,
                                    (__bridge CFStringRef)AVAudioSessionRouteChangeNotification,
                                    NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
}

static void mal_will_destory_context(mal_context *context) {
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(), context);
    if (context->internal_data != NULL) {
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
        check_routes(context);
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
    if (context->internal_data != NULL && route < NUM_MAL_ROUTES) {
        mal_context_internal *internal = (mal_context_internal *)context->internal_data;
        return internal->routes[route];
    }
    else {
        return false;
    }
}