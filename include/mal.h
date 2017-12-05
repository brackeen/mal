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

#ifndef MAL_H
#define MAL_H

/**
 * @file
 * Audio playback API.
 * Provides functions to play raw PCM audio on iOS, Android, and Emscripten.
 *
 * Uses the platform's audio system (Core Audio, OpenSL ES, etc.). 
 * No sofware audio rendering, no software mixing, no extra buffering.
 *
 * Caveats:
 * - No audio file format decoding. Bring your own WAV decoder.
 * - No streaming. All audio files must be fully decoded into memory.
 * - No effects.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAL_ROUTE_RECIEVER = 0,
    MAL_ROUTE_SPEAKER,
    MAL_ROUTE_HEADPHONES,
    MAL_ROUTE_LINEOUT,
    MAL_ROUTE_WIRELESS,
    NUM_MAL_ROUTES
} MalRoute;

typedef enum {
    MAL_PLAYER_STATE_STOPPED = 0,
    MAL_PLAYER_STATE_PLAYING,
    MAL_PLAYER_STATE_PAUSED,
} MalPlayerState;

typedef struct {
    double sampleRate;
    uint8_t bitDepth;
    uint8_t numChannels;
} MalFormat;

typedef struct MalContext MalContext;
typedef struct MalBuffer MalBuffer;
typedef struct MalPlayer MalPlayer;

typedef void (*malDeallocatorFunc)(void *);
typedef void (*malPlaybackFinishedFunc)(void *userData, MalPlayer *player);

/**
 * The value to use in the #malContextCreate() call to use the default platform sample rate.
 */
#define MAL_DEFAULT_SAMPLE_RATE 0.0

// MARK: Context

/**
 * Creates an audio context with the default options. Only one context should be created, and the
 * context should be destroyed with #malContextFree().
 */
MalContext *malContextCreate(void);

/**
 * Creates an audio context. Only one context should be created, and the context should be destroyed
 * with #malContextFree().
 *
 * @param sampleRate The requested output sample rate. Typical sample rates are 8000, 11025, 12000,
 * 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, and 192000, although most
 * platforms and devices will only support a few sample rates, and some will only support one.
 * 48000 is common on modern devices. To use the default sample rate of the platform, use
 * #MAL_DEFAULT_SAMPLE_RATE. Call #malContextGetSampleRate() to get the actual sample rate.
 *
 * @param androidActivity A reference to an `ANativeActivity` instance. The activity is used to
 * query the sample rate and buffer size (if the device is running API level 17 or newer). A
 * reference to the activity is not retained. May be `NULL`.
 *
 * @param errorMissingAudioSystem If the `MalContext` could not be created because of a missing
 * audio system (for example, "PulseAudio" on Linux), this is a pointer to the name of the missing
 * audio system. May be `NULL`.
 */
MalContext *malContextCreateWithOptions(double sampleRate, void *androidActivity,
                                        const char **errorMissingAudioSystem);
/**
 * Gets the output sample rate.
 */
double malContextGetSampleRate(const MalContext *context);

/**
 * Activates or deactivates the audio context. The context should be deactivated when the app enters
 * the background. By default, a newly created context is active.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param active If `true`, the context is activated; otherwise the context is deactivated.
 * @return `true` if successful; `false` otherwise.
 */
bool malContextSetActive(MalContext *context, bool active);

/**
 * Sends any pending events requested via #malPlayerSetFinishedFunc().
 *
 * This function is required only on Windows. On other platforms, this functions does nothing.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 */
void malContextPollEvents(MalContext *context);

/**
 * Checks if the audio is currently outputting through a specific route. Multiple output routes may
 * be enabled simultaneously. If all routes return `false`, the route could not be determined.
 *
 * Currently only the iOS implementation reports routes.
 *
 * @param context The audio context. If `NULL`, this function returns `false`.
 * @param route The audio route to check.
 * @return `true` if audio is outputting through the route.
 */
bool malContextIsRouteEnabled(const MalContext *context, MalRoute route);

/**
 * Checks if the audio context is muted.
 *
 * @param context The audio context. If `NULL`, this function returns `false`.
 * @return `true` if the context is muted; `false` otherwise.
 */
bool malContextGetMute(const MalContext *context);

/**
 * Sets the mute state of the context.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param mute If `true`, the context is muted (sound turned off); otherwise the context is 
 * unmuted (sound turned on).
 */
void malContextSetMute(MalContext *context, bool mute);

/**
 * Gets the gain (volume) for the context.
 *
 * @param context The audio context. If `NULL`, this function returns 1.0.
 * @return The gain from 0.0 to 1.0.
 */
float malContextGetGain(const MalContext *context);

/**
 * Sets the gain (volume) for the context.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param gain The gain, from 0.0 to 1.0.
 */
void malContextSetGain(MalContext *context, float gain);

/**
 * Checks if the context can play audio in the specified format. If this function returns `true`, 
 * and #malPlayerCreate() returns `NULL`, then the maximum number of players has been reached.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param format The audio format to check.
 * @return `true` if the format can be played by the context.
 */
bool malContextIsFormatValid(const MalContext *context, MalFormat format);

/**
 * Checks if two audio formats are equal.
 *
 * Either format's sample rate may be #MAL_DEFAULT_SAMPLE_RATE, in which case the context's sample
 * rate is used.
 *
 * @return `true` if the two formats are equal, `false` otherwise.
 */
bool malContextIsFormatEqual(const MalContext *context, MalFormat format1, MalFormat format2);

/**
 * Frees the context. All buffers and players created with the context will no longer be valid.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 */
void malContextFree(MalContext *context);

// MARK: Buffers

/**
 * Creates a new audio buffer from the provided data. The data buffer is copied.
 *
 * The data must be in signed linear PCM format. The byte order must be the same as the native
 * byte order (usually little endian). If stereo, the data must be interleaved.
 *
 * The buffer should be freed with #malBufferFree().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the provided data.
 * @param numFrames The number of frames in the provided data.
 * @param data The data buffer. The data buffer must have the same byte order as the native CPU, and
 * it must have a byte length of (`format.bitDepth / 8 * format.numChannels * numFrames`).
 * @return If successful, returns the audio buffer. Returns `NULL` if the format is invalid,
 * `numFrames` is zero, `data` is `NULL`, or an out-of-memory error occurs.
 */
MalBuffer *malBufferCreate(MalContext *context, MalFormat format, uint32_t numFrames,
                           const void *data);

/**
 * Creates a new audio buffer from the provided data.
 *
 * The data must be in signed linear PCM format. The byte order must be the same as the native
 * byte order (usually little endian). If stereo, the data must be interleaved.
 *
 * If possible, the data is used directly without copying. When the original data is no longer
 * needed, the `dataDeallocator` function is called. If the underlying implementation must copy
 * buffers, the `dataDeallocator` function is called immediately, before returning.
 *
 * The `dataDeallocator` is not called if this function returns `NULL`.
 *
 * The buffer should be freed with #malBufferFree().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the provided data.
 * @param numFrames The number of frames in the provided data.
 * @param data The data buffer. The data buffer must have the same byte order as the native CPU, and
 * it must have a byte length of (`format.bitDepth / 8 * format.numChannels * numFrames`).
 * @param dataDeallocator The deallocator to call when the data is no longer needed. May be `NULL`.
 * @return If successful, returns the audio buffer. Returns `NULL` if the format is invalid,
 * `numFrames` is zero, `data` is `NULL`, or an out-of-memory error occurs.
 */
MalBuffer *malBufferCreateNoCopy(MalContext *context, MalFormat format, uint32_t numFrames,
                                 void *data, malDeallocatorFunc dataDeallocator);

/**
 * Gets the format of the buffer.
 * 
 * The sample rate of the returned format may be slightly different than the one specified in 
 * #malBufferCreate() or #malBufferCreateNoCopy().
 *
 * @param buffer The audio buffer. If `NULL`, the returned format will have a sample rate of 0.
 * @return The audio format of the buffer.
 */
MalFormat malBufferGetFormat(const MalBuffer *buffer);

/**
 * Gets the number of frames for the buffer.
 *
 * @param buffer The audio buffer. If `NULL`, the returned value is 0.
 */
uint32_t malBufferGetNumFrames(const MalBuffer *buffer);

/**
 * Gets the pointer to the buffer's underlying data, if the buffer was created with
 * #malBufferCreateNoCopy() and the underlying implementation doesn't copy buffers.
 *
 * @param buffer The audio buffer. If `NULL`, the returns `NULL`.
 * @return The pointer to the buffer's underlying data, or `NULL` if the buffer was created with
 * #malBufferCreate() or if the underlying implementation must copy buffers.
 */
void *malBufferGetData(const MalBuffer *buffer);

/**
 * Frees the buffer. Any players using the buffer are stopped.
 *
 * @param buffer The audio buffer. If `NULL`, this function returns nothing.
 */
void malBufferFree(MalBuffer *buffer);

// MARK: Players

/**
 * Creates a new player with the specified format.
 *
 * Usually only a limited number of players may be created, depending on the implementation.
 * Typically 16 or 32.
 *
 * The player should be freed with #malPlayerFree().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the player to create.
 * @return The player, or `NULL` if the player could not be created.
 */
MalPlayer *malPlayerCreate(MalContext *context, MalFormat format);

/**
 * Gets the playback format of the player.
 *
 * @param player The audio player. If `NULL`, this function returns a format with a sample rate of 
 * 0.
 * @return The audio format of the player.
 */
MalFormat malPlayerGetFormat(const MalPlayer *player);

/**
 * Sets the playback format of the player.
 *
 * If playing, the player is stopped. The attached buffer, if any, is not changed.
 *
 * On OpenAL and Web Audio implementations, the format of the player is always the same as the 
 * buffer.
 *
 * The format's sample rate may be #MAL_DEFAULT_SAMPLE_RATE, in which case the context's sample
 * rate is used.
 *
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param format The audio format to set the player to.
 * @return `true` if successful.
 */
bool malPlayerSetFormat(MalPlayer *player, MalFormat format);

/**
 * Attaches a buffer to the player. Any existing buffer is removed.
 *
 * A buffer may be attached to multiple players.
 *
 * On OpenAL and Web Audio implementations, the player's playback format is set to the buffer's 
 * format.
 *
 * On other implementations, the player's playback format is not changed. To set the player's 
 * playback format to the buffer's format, call 
 * `malPlayerSetFormat(player, malBufferGetFormat(buffer));`.
 *
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param buffer The audio buffer. May be `NULL`.
 * @return `true` if successful.
 */
bool malPlayerSetBuffer(MalPlayer *player, const MalBuffer *buffer);

/**
 * Gets the buffer attached to the player.
 *
 * @param player The audio player. If `NULL`, this function returns `NULL`.
 * @return The buffer attached to the player, or `NULL` if no buffer is currently attached.
 */
const MalBuffer *malPlayerGetBuffer(const MalPlayer *player);

/**
 * Sets the function to call when a player has finished playing. The function is not called when
 * the player is forced to stop, for example when calling #malPlayerSetState() with the 
 * #MAL_PLAYER_STATE_STOPPED state, or setting the buffer with #malPlayerSetBuffer(),
 * or calling #malPlayerFree().
 *
 * The player may still be in the #MAL_PLAYER_STATE_PLAYING state when this function is called.
 *
 * On Windows, the function is invoked only when #malContextPollEvents() is invoked.
 *
 * On other platforms, this function is invoked on the main thread. (On Android, the main thread
 * is the thread that invoked #malContextSetActive().
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param onFinished The callback function, or `NULL`.
 * @param userData The user data to pass to the callback function. May be `NULL`.
 */
void malPlayerSetFinishedFunc(MalPlayer *player, malPlaybackFinishedFunc onFinished,
                              void *userData);

/**
 * Gets the function to call when a player has finished playing, or `NULL` if none.
 *
 * @param player The player. If `NULL`, this function returns `NULL`.
 * @return The callback function, or `NULL` if not defined.
 */
malPlaybackFinishedFunc malPlayerGetFinishedFunc(MalPlayer *player);

/**
 * Checks if the player muted.
 *
 * @param player The player. If `NULL`, this function returns `false`.
 * @return `true` if the player is muted; `false` otherwise.
 */
bool malPlayerGetMute(const MalPlayer *player);

/**
 * Sets the mute state of the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param mute If `true`, the player is muted (sound turned off); otherwise the player is
 * unmuted (sound turned on).
 */
void malPlayerSetMute(MalPlayer *player, bool mute);

/**
 * Gets the gain (volume) for the player.
 *
 * @param player The player. If `NULL`, this function returns 1.0.
 * @return The gain from 0.0 to 1.0.
 */
float malPlayerGetGain(const MalPlayer *player);

/**
 * Sets the gain (volume) for the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param gain The gain, from 0.0 to 1.0.
 */
void malPlayerSetGain(MalPlayer *player, float gain);

/**
 * Gets the looping state for the player.
 *
 * @param player The player. If `NULL`, this function returns `false`.
 * @return `true` if the player is set to loop; `false` otherwise.
 */
bool malPlayerIsLooping(const MalPlayer *player);

/**
 * Sets the looping state for the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param looping The looping state. Set to `true` to loop, `false` otherwise.
 */
void malPlayerSetLooping(MalPlayer *player, bool looping);

/**
 * Gets the state of the player.
 *
 * @param player The audio player. If `NULL`, this function returns #MAL_PLAYER_STATE_STOPPED.
 * @return The current state of the player.
 */
MalPlayerState malPlayerGetState(MalPlayer *player);

/**
 * Sets the state of the player. If a buffer is attached to the player, this function can be
 * used to play or stop the player.
 * 
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param state The player state.
 * @return `true` if successful.
 */
bool malPlayerSetState(MalPlayer *player, MalPlayerState state);

/**
 * Frees the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 */
void malPlayerFree(MalPlayer *player);

#ifdef __cplusplus
}
#endif

#endif
