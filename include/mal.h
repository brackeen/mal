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

#ifndef _MAL_H_
#define _MAL_H_

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
} mal_route;

typedef enum {
    MAL_PLAYER_STATE_STOPPED = 0,
    MAL_PLAYER_STATE_PLAYING,
    MAL_PLAYER_STATE_PAUSED,
} mal_player_state;

typedef struct {
    double sample_rate;
    uint8_t bit_depth;
    uint8_t num_channels;
} mal_format;

typedef struct mal_context mal_context;
typedef struct mal_buffer mal_buffer;
typedef struct mal_player mal_player;

typedef void (*mal_deallocator_func)(void *);
typedef void (*mal_playback_finished_func)(void *user_data, mal_player *player);

// MARK: Context

/**
 * Creates an audio context. Only one context should be created, and the context should be destroyed
 * with #mal_context_free().
 *
 * @param sample_rate The output sample rate, typically 44100 or 22050.
 */
mal_context *mal_context_create(double sample_rate);

/**
 * Activates or deactivates the audio context. The context should be deactivated when the app enters
 * the background. By default, a newly created context is active.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param active If `true`, the context is activated; otherwise the context is deactivated.
 */
void mal_context_set_active(mal_context *context, bool active);

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
bool mal_context_is_route_enabled(const mal_context *context, mal_route route);

/**
 * Checks if the audio context is muted.
 *
 * @param context The audio context. If `NULL`, this function returns `false`.
 * @return `true` if the context is muted; `false` otherwise.
 */
bool mal_context_get_mute(const mal_context *context);

/**
 * Sets the mute state of the context.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param mute If `true`, the context is muted (sound turned off); otherwise the context is 
 * unmuted (sound turned on).
 */
void mal_context_set_mute(mal_context *context, bool mute);

/**
 * Gets the gain (volume) for the context.
 *
 * @param context The audio context. If `NULL`, this function returns 1.0.
 * @return The gain from 0.0 to 1.0.
 */
float mal_context_get_gain(const mal_context *context);

/**
 * Sets the gain (volume) for the context.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param gain The gain, from 0.0 to 1.0.
 */
void mal_context_set_gain(mal_context *context, float gain);

/**
 * Checks if the context can play audio in the specified format. If this function returns `true`, 
 * and #mal_player_create() returns `NULL`, then the maximum number of players has been reached.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 * @param format The audio format to check.
 * @return `true` if the format can be played by the context.
 */
bool mal_context_format_is_valid(const mal_context *context, mal_format format);

/**
 * Checks if two audio formats are equal.
 *
 * @return `true` if the two formats are equal, `false` otherwise.
 */
bool mal_formats_equal(mal_format format1, mal_format format2);

/**
 * Frees the context. All buffers and players created with the context will no longer be valid.
 *
 * @param context The audio context. If `NULL`, this function does nothing.
 */
void mal_context_free(mal_context *context);

// MARK: Buffers

/**
 * Creates a new audio buffer from the provided data. The data buffer is copied.
 *
 * The data must be in signed linear PCM format. The byte order must be the same as the native
 * byte order (usually little endian). If stereo, the data must be interleaved.
 *
 * The buffer should be freed with #mal_buffer_free().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the provided data.
 * @param num_frames The number of frames in the provided data.
 * @param data The data buffer. The data buffer must have the same byte order as the native CPU, and
 * it must have a byte length of (`format.bit_depth / 8 * format.num_channels * num_frames`).
 * @return If successful, returns the audio buffer. Returns `NULL` if the format is invalid,
 * `num_frames` is zero, `data` is `NULL`, or an out-of-memory error occurs.
 */
mal_buffer *mal_buffer_create(mal_context *context, mal_format format, uint32_t num_frames,
                              const void *data);

/**
 * Creates a new audio buffer from the provided data.
 *
 * The data must be in signed linear PCM format. The byte order must be the same as the native
 * byte order (usually little endian). If stereo, the data must be interleaved.
 *
 * If possible, the data is used directly without copying. When the original data is no longer
 * needed, the `data_deallocator` function is called. If the underlying implementation must copy
 * buffers, the `data_deallocator` function is called immediately, before returning.
 *
 * The `data_deallocator` is not called if this function returns `NULL`.
 *
 * The buffer should be freed with #mal_buffer_free().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the provided data.
 * @param num_frames The number of frames in the provided data.
 * @param data The data buffer. The data buffer must have the same byte order as the native CPU, and
 * it must have a byte length of (`format.bit_depth / 8 * format.num_channels * num_frames`).
 * @param data_deallocator The deallocator to call when the data is no longer needed. May be `NULL`.
 * @return If successful, returns the audio buffer. Returns `NULL` if the format is invalid,
 * `num_frames` is zero, `data` is `NULL`, or an out-of-memory error occurs.
 */
mal_buffer *mal_buffer_create_no_copy(mal_context *context, mal_format format, uint32_t num_frames,
                                      void *data, mal_deallocator_func data_deallocator);

/**
 * Gets the format of the buffer.
 * 
 * The sample rate of the returned format may be slightly different than the one specified in 
 * #mal_buffer_create() or #mal_buffer_create_no_copy().
 *
 * @param buffer The audio buffer. If `NULL`, the returned format will have a sample rate of 0.
 * @return The audio format of the buffer.
 */
mal_format mal_buffer_get_format(const mal_buffer *buffer);

/**
 * Gets the number of frames for the buffer.
 *
 * @param buffer The audio buffer. If `NULL`, the returned value is 0.
 */
uint32_t mal_buffer_get_num_frames(const mal_buffer *buffer);

/**
 * Gets the pointer to the buffer's underlying data, if the buffer was created with
 * #mal_buffer_create_no_copy() and the underlying implementation doesn't copy buffers.
 *
 * @param buffer The audio buffer. If `NULL`, the returns `NULL`.
 * @return The pointer to the buffer's underlying data, or `NULL` if the buffer was created with
 * #mal_buffer_create() or if the underlying implementation must copy buffers.
 */
void *mal_buffer_get_data(const mal_buffer *buffer);

/**
 * Frees the buffer. Any players using the buffer are stopped.
 *
 * @param buffer The audio buffer. If `NULL`, this function returns nothing.
 */
void mal_buffer_free(mal_buffer *buffer);

// MARK: Players

/**
 * Creates a new player with the specified format.
 *
 * Usually only a limited number of players may be created, depending on the implementation.
 * Typically 16 or 32.
 *
 * The player should be freed with #mal_player_free().
 *
 * @param context The audio context. If `NULL`, this function returns `NULL`.
 * @param format The format of the player to create.
 * @return The player, or `NULL` if the player could not be created.
 */
mal_player *mal_player_create(mal_context *context, mal_format format);

/**
 * Gets the playback format of the player.
 *
 * @param player The audio player. If `NULL`, this function returns a format with a sample rate of 
 * 0.
 * @return The audio format of the player.
 */
mal_format mal_player_get_format(const mal_player *player);

/**
 * Sets the playback format of the player.
 *
 * If playing, the player is stopped. The attached buffer, if any, is not changed.
 *
 * On OpenAL and Web Audio implementations, the format of the player is always the same as the 
 * buffer.
 *
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param format The audio format to set the player to.
 * @return `true` if successful.
 */
bool mal_player_set_format(mal_player *player, mal_format format);

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
 * `mal_player_set_format(player, mal_buffer_get_format(buffer));`.
 *
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param buffer The audio buffer. May be `NULL`.
 * @return `true` if successful.
 */
bool mal_player_set_buffer(mal_player *player, const mal_buffer *buffer);

/**
 * Gets the buffer attached to the player.
 *
 * @param player The audio player. If `NULL`, this function returns `NULL`.
 * @return The buffer attached to the player, or `NULL` if no buffer is currently attached.
 */
const mal_buffer *mal_player_get_buffer(const mal_player *player);

/**
 * Sets the function to call when a player has finished playing. The function is not called when
 * the player is forced to stop, for example when calling #mal_player_set_state() with the 
 * #MAL_PLAYER_STATE_STOPPED state, or setting the buffer with #mal_player_set_buffer(),
 * or calling #mal_player_free().
 *
 * The player may still be in the #MAL_PLAYER_STATE_PLAYING state when this function is called.
 *
 * The function is invoked on the main thread. On Android, the main thread is the thread that
 * invoked #mal_context_set_active().
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param on_finished The callback function, or `NULL`.
 * @param user_data The user data to pass to the callback function. May be `NULL`.
 */
void mal_player_set_finished_func(mal_player *player, mal_playback_finished_func on_finished,
                                  void *user_data);

/**
 * Gets the function to call when a player has finished playing, or `NULL` if none.
 *
 * @param player The player. If `NULL`, this function returns `NULL`.
 * @return The callback function, or `NULL` if not defined.
 */
mal_playback_finished_func mal_player_get_finished_func(mal_player *player);

/**
 * Checks if the player muted.
 *
 * @param player The player. If `NULL`, this function returns `false`.
 * @return `true` if the player is muted; `false` otherwise.
 */
bool mal_player_get_mute(const mal_player *player);

/**
 * Sets the mute state of the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param mute If `true`, the player is muted (sound turned off); otherwise the player is
 * unmuted (sound turned on).
 */
void mal_player_set_mute(mal_player *player, bool mute);

/**
 * Gets the gain (volume) for the player.
 *
 * @param player The player. If `NULL`, this function returns 1.0.
 * @return The gain from 0.0 to 1.0.
 */
float mal_player_get_gain(const mal_player *player);

/**
 * Sets the gain (volume) for the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param gain The gain, from 0.0 to 1.0.
 */
void mal_player_set_gain(mal_player *player, float gain);

/**
 * Gets the looping state for the player.
 *
 * @param player The player. If `NULL`, this function returns `false`.
 * @return `true` if the player is set to loop; `false` otherwise.
 */
bool mal_player_is_looping(const mal_player *player);

/**
 * Sets the looping state for the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 * @param looping The looping state. Set to `true` to loop, `false` otherwise.
 */
void mal_player_set_looping(mal_player *player, bool looping);

/**
 * Sets the state of the player. If a buffer is attached to the player, this function can be
 * used to play or stop the player.
 * 
 * @param player The audio player. If `NULL`, this function does nothing.
 * @param state The player state.
 * @return `true` if successful.
 */
bool mal_player_set_state(mal_player *player, mal_player_state state);

/**
 * Gets the state of the player.
 *
 * @param player The audio player. If `NULL`, this function returns #MAL_PLAYER_STATE_STOPPED.
 * @return The current state of the player.
 */
mal_player_state mal_player_get_state(mal_player *player);

/**
 * Frees the player.
 *
 * @param player The player. If `NULL`, this function does nothing.
 */
void mal_player_free(mal_player *player);

#ifdef __cplusplus
}
#endif

#endif
