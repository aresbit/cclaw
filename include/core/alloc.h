// alloc.h - Memory allocator for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_ALLOC_H
#define CCLAW_CORE_ALLOC_H

#include "types.h"
#include "error.h"

// Allocator interface (compatible with sp.h)
typedef struct allocator_t allocator_t;

typedef struct allocator_vtable_t {
    void* (*alloc)(allocator_t* alloc, size_t size, size_t alignment);
    void* (*realloc)(allocator_t* alloc, void* ptr, size_t old_size, size_t new_size, size_t alignment);
    void (*free)(allocator_t* alloc, void* ptr, size_t size);
    void (*destroy)(allocator_t* alloc);
} allocator_vtable_t;

struct allocator_t {
    allocator_vtable_t* vtable;
    void* user_data;
};

// Built-in allocator types
typedef enum {
    ALLOCATOR_DEFAULT,     // System malloc/free
    ALLOCATOR_ARENA,       // Arena/region allocator
    ALLOCATOR_POOL,        // Fixed-size pool allocator
    ALLOCATOR_TRACKING,    // Tracking allocator for debugging
    ALLOCATOR_SCRATCH      // Scratch/temporary allocator
} allocator_type_t;

// Arena allocator (region allocator)
typedef struct arena_allocator_t {
    allocator_t base;
    void* region;
    size_t region_size;
    size_t used;
    bool owns_region;
} arena_allocator_t;

// Pool allocator (fixed-size blocks)
typedef struct pool_allocator_t {
    allocator_t base;
    size_t block_size;
    size_t blocks_per_chunk;
    void** free_list;
    void* chunks;
    uint32_t chunk_count;
} pool_allocator_t;

// Tracking allocator (debugging)
typedef struct tracking_allocator_t {
    allocator_t base;
    allocator_t* backing;
    size_t total_allocated;
    size_t total_freed;
    size_t peak_allocated;
    uint32_t allocation_count;
    uint32_t leak_count;
} tracking_allocator_t;

// Scratch allocator (temporary memory)
typedef struct scratch_allocator_t {
    allocator_t base;
    void* buffer;
    size_t buffer_size;
    size_t used;
    size_t saved;
} scratch_allocator_t;

// Global allocators
allocator_t* allocator_default(void);
allocator_t* allocator_scratch(void);

// Allocator creation
allocator_t* allocator_create(allocator_type_t type, size_t param1, size_t param2);
void allocator_destroy(allocator_t* alloc);

// Arena allocator
arena_allocator_t* arena_create(size_t size);
arena_allocator_t* arena_create_from_buffer(void* buffer, size_t size);
void arena_destroy(arena_allocator_t* arena);
void arena_reset(arena_allocator_t* arena);

// Pool allocator
pool_allocator_t* pool_create(size_t block_size, size_t blocks_per_chunk);
void pool_destroy(pool_allocator_t* pool);

// Tracking allocator
tracking_allocator_t* tracking_create(allocator_t* backing);
void tracking_destroy(tracking_allocator_t* tracker);
void tracking_report(tracking_allocator_t* tracker);

// Scratch allocator
scratch_allocator_t* scratch_create(size_t size);
scratch_allocator_t* scratch_create_from_buffer(void* buffer, size_t size);
void scratch_destroy(scratch_allocator_t* scratch);
void scratch_reset(scratch_allocator_t* scratch);
size_t scratch_save(scratch_allocator_t* scratch);
void scratch_restore(scratch_allocator_t* scratch, size_t mark);

// Allocation functions (use these instead of malloc/free directly)
void* alloc(allocator_t* alloc, size_t size);
void* alloc_aligned(allocator_t* alloc, size_t size, size_t alignment);
void* realloc_ptr(allocator_t* alloc, void* ptr, size_t old_size, size_t new_size);
void* realloc_aligned_ptr(allocator_t* alloc, void* ptr, size_t old_size, size_t new_size, size_t alignment);
void free_ptr(allocator_t* alloc, void* ptr, size_t size);

// Convenience macros
#define ALLOC(alloc, type) ((type*)alloc((alloc), sizeof(type)))
#define ALLOC_ARRAY(alloc, type, count) ((type*)alloc((alloc), sizeof(type) * (count)))
#define ALLOC_ALIGNED(alloc, type, alignment) ((type*)alloc_aligned((alloc), sizeof(type), (alignment)))

#define REALLOC(alloc, ptr, type, old_count, new_count) \
    ((type*)realloc_ptr((alloc), (ptr), sizeof(type) * (old_count), sizeof(type) * (new_count)))

#define FREE(alloc, ptr, type) free_ptr((alloc), (ptr), sizeof(type))
#define FREE_ARRAY(alloc, ptr, type, count) free_ptr((alloc), (ptr), sizeof(type) * (count))

// String allocation
str_t alloc_str(allocator_t* alloc, str_t src);
str_t alloc_str_cstr(allocator_t* alloc, const char* src);
void free_str(allocator_t* alloc, str_t str);

// Array allocation helpers
void* alloc_array(allocator_t* alloc, size_t element_size, size_t count);
void* realloc_array(allocator_t* alloc, void* ptr, size_t element_size, size_t old_count, size_t new_count);

// Memory utilities
void zero_memory(void* ptr, size_t size);
void copy_memory(void* dst, const void* src, size_t size);
void move_memory(void* dst, const void* src, size_t size);
bool compare_memory(const void* a, const void* b, size_t size);

// Memory tracking (debug builds only)
#ifdef DEBUG
    #define TRACK_ALLOC(alloc, size) track_alloc((alloc), (size), __FILE__, __LINE__)
    #define TRACK_FREE(alloc, ptr, size) track_free((alloc), (ptr), (size), __FILE__, __LINE__)

    void track_alloc(allocator_t* alloc, size_t size, const char* file, uint32_t line);
    void track_free(allocator_t* alloc, void* ptr, size_t size, const char* file, uint32_t line);
    void track_report(void);
#else
    #define TRACK_ALLOC(alloc, size) (alloc)
    #define TRACK_FREE(alloc, ptr, size) ((void)0)
    #define track_report() ((void)0)
#endif

#endif // CCLAW_CORE_ALLOC_H