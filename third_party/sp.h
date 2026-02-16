// sp.h - Single-header C standard library replacement (placeholder)
// Note: This is a minimal placeholder for the cclaw project.
// In a real project, you would use the full sp.h library from:
// https://github.com/your-org/sp.h

#ifndef SP_H
#define SP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Basic type definitions
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef char c8;

// String type (ptr + len, not null-terminated)
typedef struct sp_str_t {
    const c8* data;
    u32 len;
} sp_str_t;

// Context for sp.h (simplified)
typedef struct sp_ctx_t {
    u32 flags;
    void* user_data;
} sp_ctx_t;

// Logging macro (simplified)
#define SP_LOG(...) \
    do { \
        fprintf(stderr, "[CClaw] "); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while (0)

// Formatting macros (simplified)
#define SP_FMT_CSTR(s) (s)
#define SP_FMT_STR(s) ((s).data)
#define SP_FMT_S32(v) (v)
#define SP_FMT_U32(v) (v)
#define SP_FMT_S64(v) (v)
#define SP_FMT_U64(v) (v)
#define SP_FMT_F32(v) (v)
#define SP_FMT_F64(v) (v)
#define SP_FMT_BOOL(v) ((v) ? "true" : "false")

// String utilities
static inline sp_str_t sp_str_lit(const char* str) {
    return (sp_str_t){.data = str, .len = (u32)strlen(str)};
}

static inline bool sp_str_equal(sp_str_t a, sp_str_t b) {
    if (a.len != b.len) return false;
    if (a.data == b.data) return true;
    if (!a.data || !b.data) return false;
    return memcmp(a.data, b.data, a.len) == 0;
}

static inline bool sp_str_empty(sp_str_t s) {
    return s.len == 0 || !s.data;
}

// Context management (simplified)
sp_ctx_t* sp_ctx_create(u32 flags);
void sp_ctx_destroy(sp_ctx_t* ctx);
void sp_ctx_set_current(sp_ctx_t* ctx);
sp_ctx_t* sp_ctx_get_current(void);

// Default context flags
#define SP_CTX_DEFAULT_FLAGS 0

// Dynamic array (simplified stb-style)
#define sp_da(type) type*

#define sp_dyn_array_push(arr, val) \
    do { \
        /* Simplified implementation */ \
        if (0) { \
            (void)(arr); \
            (void)(val); \
        } \
    } while (0)

#define sp_dyn_array_size(arr) 0
#define sp_dyn_array_free(arr) ((void)0)

// Assertion macros
#define SP_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)

#define SP_FATAL(msg, ...) \
    do { \
        fprintf(stderr, "Fatal error: " msg "\n", ##__VA_ARGS__); \
        abort(); \
    } while (0)

// Unreachable macro
#define SP_UNREACHABLE() \
    do { \
        fprintf(stderr, "Unreachable code reached (%s:%d)\n", __FILE__, __LINE__); \
        abort(); \
    } while (0)

#endif // SP_H

// Implementation section (only included in one .c file)
#ifdef SP_IMPLEMENTATION

sp_ctx_t* sp_ctx_create(u32 flags) {
    (void)flags;
    static sp_ctx_t dummy_ctx = {0};
    return &dummy_ctx;
}

void sp_ctx_destroy(sp_ctx_t* ctx) {
    (void)ctx;
}

void sp_ctx_set_current(sp_ctx_t* ctx) {
    (void)ctx;
}

sp_ctx_t* sp_ctx_get_current(void) {
    static sp_ctx_t dummy_ctx = {0};
    return &dummy_ctx;
}

#endif // SP_IMPLEMENTATION