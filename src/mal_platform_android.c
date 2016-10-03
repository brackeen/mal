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

#if defined(ANDROID)

#include "mal_audio_opensl.h"

static void _mal_context_did_create(mal_context *context) {
    // Do nothing
}

static void _mal_context_will_dispose(mal_context *context) {
    // Do nothing
}

static void _mal_context_did_set_active(mal_context *context, bool active) {
    // Do nothing
}

#endif
