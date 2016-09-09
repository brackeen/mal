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

#ifndef _MAL_VECTOR_H_
#define _MAL_VECTOR_H_

#include <memory.h>
#include <stdbool.h>
#include <stdlib.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

typedef struct {
    void **values;
    unsigned int length;
    unsigned int capacity;
} mal_vector;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

static bool mal_vector_ensure_capacity(mal_vector *list, const unsigned int additional_values) {
    if (list) {
        if (!list->values || list->length + additional_values > list->capacity) {
            const unsigned int new_capacity = MAX(list->length + additional_values,
                                                  list->capacity << 1);
            void **new_data = realloc(list->values, sizeof(void *) * new_capacity);
            if (!new_data) {
                return false;
            }
            list->values = new_data;
            list->capacity = new_capacity;
        }
        return true;
    }
    return false;
}

static bool mal_vector_add(mal_vector *list, void *value) {
    if (mal_vector_ensure_capacity(list, 1)) {
        list->values[list->length++] = value;
        return true;
    } else {
        return false;
    }
}

static bool mal_vector_add_all(mal_vector *list, unsigned int num_values, void **values) {
    if (mal_vector_ensure_capacity(list, num_values)) {
        memcpy(list->values + list->length, values, sizeof(void *) * num_values);
        list->length += num_values;
        return true;
    } else {
        return false;
    }
}

static bool mal_vector_contains(const mal_vector *list, void *value) {
    if (list) {
        for (unsigned int i = 0; i < list->length; i++) {
            if (list->values[i] == value) {
                return true;
            }
        }
    }
    return false;
}

static bool mal_vector_remove(mal_vector *list, void *value) {
    if (list) {
        for (unsigned int i = 0; i < list->length; i++) {
            if (list->values[i] == value) {
                for (unsigned int j = i; j < list->length - 1; j++) {
                    list->values[j] = list->values[j + 1];
                }
                list->length--;
                return true;
            }
        }
    }
    return false;
}

static void mal_vector_free(mal_vector *list) {
    if (list && list->values) {
        free(list->values);
        list->values = NULL;
        list->length = 0;
        list->capacity = 0;
    }
}

#pragma clang diagnostic pop

#endif
