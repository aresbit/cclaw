// string.c - String utilities for CClaw
// SPDX-License-Identifier: MIT

#include "core/types.h"
#include "core/alloc.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// String duplication
str_t str_dup(str_t s, allocator_t* alloc) {
    (void)alloc; // TODO: Use allocator when implemented

    if (str_empty(s)) {
        return STR_NULL;
    }

    char* data = malloc(s.len + 1);
    if (!data) {
        return STR_NULL;
    }

    memcpy(data, s.data, s.len);
    data[s.len] = '\0';

    return (str_t){ .data = data, .len = s.len };
}

str_t str_dup_cstr(const char* s, allocator_t* alloc) {
    (void)alloc; // TODO: Use allocator when implemented

    if (!s) {
        return STR_NULL;
    }

    size_t len = strlen(s);
    char* data = malloc(len + 1);
    if (!data) {
        return STR_NULL;
    }

    memcpy(data, s, len);
    data[len] = '\0';

    return (str_t){ .data = data, .len = (uint32_t)len };
}

// String formatting (allocates memory)
str_t str_format(allocator_t* alloc, const char* fmt, ...) {
    (void)alloc; // TODO: Use allocator when implemented

    va_list args;
    va_start(args, fmt);

    // Determine required size
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (size < 0) {
        va_end(args);
        return STR_NULL;
    }

    // Allocate buffer
    char* buffer = malloc((size_t)size + 1);
    if (!buffer) {
        va_end(args);
        return STR_NULL;
    }

    // Format string
    vsnprintf(buffer, (size_t)size + 1, fmt, args);
    va_end(args);

    return (str_t){ .data = buffer, .len = (uint32_t)size };
}