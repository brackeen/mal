/*
 mal
 Copyright (c) 2014 David Brackeen
 
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
    
    typedef void (*mal_deallocator)(void*);
    
    //
    // MARK: Context
    //
    
    /// Creates an audio context. Only one context should be created.
    /// The output sample rate is typically 44100 or 22050.
    mal_context *mal_context_create(const double output_sample_rate);
    
    void mal_context_set_active(mal_context *context, const bool active);
    
    /// Checks if the audio is currently outputting through a specific route. If all routes return false,
    /// the route could not be determined (currently the case on Android).
    bool mal_context_is_route_enabled(const mal_context *context, const mal_route route);
    bool mal_context_get_mute(const mal_context *context);
    void mal_context_set_mute(mal_context *context, const bool mute);
    float mal_context_get_gain(const mal_context *context);
    void mal_context_set_gain(mal_context *context, const float gain);
    
    /// Returns true if the format can be played. For a particular format, if this function returns true, and
    /// mal_player_create returns NULL, then the maximum number of players has been reached.
    bool mal_context_format_is_valid(const mal_context *context, const mal_format format);
    
    /// Frees the context. All buffers and players created with this context will not longer be valid.
    void mal_context_free(mal_context *context);
    
    bool mal_formats_equal(const mal_format format1, const mal_format format2);
    
    //
    // MARK: Buffers
    //
    
    /**
     Creates a new audio buffer. The data buffer must have the same byte order as
     the native CPU, and it must have a byte length of (format.bit_depth/8 * format.num_channels * num_frames).
     The data buffer is copied.
     Returns NULL if the format is invalid, num_frames is zero, data is NULL, or an out-of-memory error occurs.
     */
    mal_buffer *mal_buffer_create(mal_context *context, const mal_format format,
                                  const uint32_t num_frames, const void *data);
    
    /**
     Creates a new audio buffer. The data buffer must have the same byte order as
     the native CPU, and it must have a byte length of (format.bit_depth/8 * format.num_channels * num_frames).
     If possible, the data is used directly without copying. When the original data is no longer needed,
     the data_deallocator function is called. The data_deallocator function may be NULL. If the underlying
     implementation must copy buffers, the data_deallocator function is called immediately, before returning.
     Returns NULL if the format is invalid, num_frames is zero, data is NULL, or an out-of-memory error occurs.
     The data_deallocator is not called if this function returns NULL.
     */
    mal_buffer *mal_buffer_create_no_copy(mal_context *context, const mal_format format,
                                          const uint32_t num_frames, void *data,
                                          const mal_deallocator data_deallocator);
    
    /**
     Gets the format of this buffer. Note, the sample rate may be slightly different than the one
     */
    mal_format mal_buffer_get_format(const mal_buffer *buffer);
    
    /**
     Gets the number of frames for this buffer.
     */
    uint32_t mal_buffer_get_num_frames(const mal_buffer *buffer);
    
    /**
     Gets the pointer to this buffer's underlying data, if this buffer was created with mal_buffer_create_no_copy. 
     Returns NULL if this buffer was created with mal_buffer_create, or if the underlying implementation must 
     copy buffers.
     */
    void *mal_buffer_get_data(const mal_buffer *buffer);
    
    /**
     Frees this buffer. Any mal_players using this buffer are stopped.
     */
    void mal_buffer_free(mal_buffer *buffer);
    
    //
    // MARK: Players
    //
    
    /**
     Creates a new player with the specified format.
     Usually only a limited number of players may be created, typically 16.
     Returns NULL if the player could not be created.
     */
    mal_player *mal_player_create(mal_context *context, const mal_format format);
    
    /**
     Gets the format of this player.
     */
    mal_format mal_player_get_format(const mal_player *player);
    
    /**
     Sets the format of this player.
     Returns true if successfull.
     */
    bool mal_player_set_format(mal_player *player, const mal_format format);
    
    /**
     Binds a buffer to this player. If buffer is NULL, any existing binding is removed.
     Note, a buffer may be bound to multiple players. 
     Returns true if successfull.
     */
    bool mal_player_set_buffer(mal_player *player, const mal_buffer *buffer);
    
    /**
     Binds a sequence of buffers to this player. If num_buffers is 0, any existing binding is removed.
     All buffers must be the same format and must be non-NULL.
     Returns true if successfull.
     */
    bool mal_player_set_buffer_sequence(mal_player *player, const unsigned int num_buffers,
                                        const mal_buffer **buffers);
    
    /**
     Returns true if this player has a buffer bound to it.
     */
    bool mal_player_has_buffer(const mal_player *player);
    
    bool mal_player_get_mute(const mal_player *player);
    void mal_player_set_mute(mal_player *player, const bool mute);
    float mal_player_get_gain(const mal_player *player);
    void mal_player_set_gain(mal_player *player, const float gain);
    bool mal_player_is_looping(const mal_player *player);
    void mal_player_set_looping(mal_player *player, const bool looping);
    
    /// Returns true if successfull.
    bool mal_player_set_state(mal_player *player, const mal_player_state state);
    
    mal_player_state mal_player_get_state(const mal_player *player);
    
    /**
     Frees this player. 
     */
    void mal_player_free(mal_player *player);
    
#ifdef __cplusplus
}
#endif

#endif
