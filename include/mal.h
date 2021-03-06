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

#ifndef MAL_H
#define MAL_H

/**
 * @file
 * Audio playback API.
 * Provides functions to play raw PCM audio on Windows, macOS, Linux, iOS, Android, and Emscripten.
 *
 * Uses the platform's audio system (XAudio2, PulseAudio, Core Audio, OpenSL ES, Web Audio).
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
typedef void (*malPlaybackFinishedFunc)(MalPlayer *player, void *userData);

/**
 * The value to use in the #malContextCreate() call to use the default platform sample rate.
 */
#define MAL_DEFAULT_SAMPLE_RATE 0.0

// MARK: Context

/**
 * Creates an audio context with the default options. Only one context should be created, and when
 * finished using the context, it should be released with #malContextRelease().
 */
MalContext *malContextCreate(void);

/**
 * Creates an audio context. Only one context should be created, and when finished using the
 * context, it should be released with #malContextRelease().
 *
 * @param sampleRate The requested output sample rate. Typical sample rates are 8000, 11025, 12000,
 * 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, and 192000, although most
 * platforms and devices will only support a few sample rates, and some will only support one.
 * 48000 is common on modern devices. To use the default sample rate of the platform, use
 * #MAL_DEFAULT_SAMPLE_RATE. Call #malContextGetSampleRate() to get the actual sample rate.
 *
 * @param androidActivity A reference to an `ANativeActivity` instance. The activity is used to
 * query the device's output sample rate (if the device is running API level 17 or newer). A
 * reference to the activity is not retained. May be `NULL`.
 *
 * @param errorMissingAudioSystem If the `MalContext` could not be created because of a missing
 * audio system (for example, "PulseAudio" on Linux), this is a pointer to the name of the missing
 * audio system. May be `NULL`.
 */
MalContext *malContextCreateWithOptions(double sampleRate, void *androidActivity,
                                        const char **errorMissingAudioSystem);

/**
 * Increases the reference count of the context by one.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 */
void malContextRetain(MalContext *context);

/**
 * Decreases the reference count of the context by one. When the reference count is zero, the
 * context is destroyed.
 *
 * The players and buffers should be released before releasing the context. When the context is
 * destroyed, all remaining buffers and players created with the context are invalid.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 */
void malContextRelease(MalContext *context);

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
 * Sends any pending events requested via #malPlayerSetFinishedFunc(). Typically,
 * #malContextPollEvents() should be called regularly in the game loop.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 */
void malContextPollEvents(MalContext *context);

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

// MARK: Buffers

/**
 * Creates a new audio buffer from the provided data. The data buffer is copied.
 *
 * The data must be in signed linear PCM format. The byte order must be the same as the native
 * byte order (usually little endian). If stereo, the data must be interleaved.
 *
 * The buffer should be released with #malBufferRelease().
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
 * The buffer should be released with #malBufferRelease().
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
 * Increases the reference count of the buffer by one.
 *
 * @param buffer The audio buffer. If `NULL`, this function returns nothing.
 */
void malBufferRetain(MalBuffer *buffer);

/**
 * Decreases the reference count of the buffer by one. When the reference count is zero, the
 * buffer is destroyed.
*
 * @param buffer The audio buffer. If `NULL`, this function returns nothing.
 */
void malBufferRelease(MalBuffer *buffer);

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

// MARK: Players

/**
 * Creates a new player with the specified format.
 *
 * Usually only a limited number of players may be created, depending on the implementation.
 * Typically 16 or 32.
 *
 * The player should be released with #malPlayerRelease().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the player to create.
 * @return The player, or `NULL` if the player could not be created.
 */
MalPlayer *malPlayerCreate(MalContext *context, MalFormat format);

/**
 * Increases the reference count of the player by one.
 *
 * @param player The audio player. If `NULL`, this function returns nothing.
 */
void malPlayerRetain(MalPlayer *player);

/**
 * Decreases the reference count of the player by one. When the reference count is zero, the
 * player is destroyed. When the player is destroyed, it's attached buffer (if any) is released.
 *
 * @param player The audio player. If `NULL`, this function returns nothing.
 */
void malPlayerRelease(MalPlayer *player);

/**
 * Gets the playback format of the player.
 *
 * @param player The audio player. If `NULL`, this function returns a format with a sample rate of 
 * 0.
 * @return The audio format of the player.
 */
MalFormat malPlayerGetFormat(const MalPlayer *player);

/**
 * Attaches a buffer to the player.
 *
 * A buffer may be attached to multiple players.
 *
 * When a buffer is attached to a player, it is retained (it's reference count is incremented), and
 * the previous buffer (if any) is released (it's reference count is decremented).
 *
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param buffer The audio buffer. May be `NULL`.
 * @return `true` if successful.
 */
bool malPlayerSetBuffer(MalPlayer *player, MalBuffer *buffer);

/**
 * Gets the buffer attached to the player.
 *
 * @param player The audio player. If `NULL`, this function returns `NULL`.
 * @return The buffer attached to the player, or `NULL` if no buffer is currently attached.
 */
MalBuffer *malPlayerGetBuffer(const MalPlayer *player);

/**
 * Sets the function to call when a player has finished playing. The function is not called when
 * the player is forced to stop, for example when calling #malPlayerSetState() with the 
 * #MAL_PLAYER_STATE_STOPPED state.
 *
 * When the player is finished, the `onFinished` function is invoked when #malContextPollEvents()
 * is invoked. Typically, #malContextPollEvents() should be called regularly in the game loop.
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
 * On Windows, this function will fail is `looping` is `true` and the player is not in the
 * #MAL_PLAYER_STATE_STOPPED state.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param looping The looping state. Set to `true` to loop, `false` otherwise.
 * @return `true` if successful.
 */
bool malPlayerSetLooping(MalPlayer *player, bool looping);

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

#ifdef __cplusplus
}
#endif

#endif
