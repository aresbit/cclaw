// cclaw.h - Main header for CClaw (ZeroClaw C port)
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CClaw Contributors

#ifndef CCLAW_H
#define CCLAW_H

#include "core/types.h"
#include "core/error.h"
#include "core/config.h"
#include "core/agent.h"
#include "core/extension.h"
#include "runtime/agent_loop.h"

// Version information
#define CCLAW_VERSION_MAJOR 0
#define CCLAW_VERSION_MINOR 1
#define CCLAW_VERSION_PATCH 0

#define CCLAW_VERSION_STRING "0.1.0"

// Platform detection
#ifdef _WIN32
    #define CCLAW_PLATFORM_WINDOWS 1
    #define CCLAW_PLATFORM_LINUX 0
    #define CCLAW_PLATFORM_MACOS 0
    #define CCLAW_PLATFORM_ANDROID 0
#elif __ANDROID__
    #define CCLAW_PLATFORM_WINDOWS 0
    #define CCLAW_PLATFORM_LINUX 0
    #define CCLAW_PLATFORM_MACOS 0
    #define CCLAW_PLATFORM_ANDROID 1
#elif __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define CCLAW_PLATFORM_WINDOWS 0
        #define CCLAW_PLATFORM_LINUX 0
        #define CCLAW_PLATFORM_MACOS 1
        #define CCLAW_PLATFORM_ANDROID 0
    #endif
#elif __linux__
    #define CCLAW_PLATFORM_WINDOWS 0
    #define CCLAW_PLATFORM_LINUX 1
    #define CCLAW_PLATFORM_MACOS 0
    #define CCLAW_PLATFORM_ANDROID 0
#else
    #error "Unsupported platform"
#endif

// Export macros for shared libraries
#ifdef _WIN32
    #ifdef CCLAW_BUILDING_DLL
        #define CCLAW_API __declspec(dllexport)
    #else
        #define CCLAW_API __declspec(dllimport)
    #endif
#else
    #define CCLAW_API __attribute__((visibility("default")))
#endif

// Inline function support
#ifdef _MSC_VER
    #define CCLAW_INLINE __inline
#else
    #define CCLAW_INLINE inline
#endif

// Null pointer constant
#ifndef NULL
    #ifdef __cplusplus
        #define NULL 0
    #else
        #define NULL ((void*)0)
    #endif
#endif

// Boolean type
#include <stdbool.h>

// Standard integer types
#include <stdint.h>
#include <stddef.h>

// Main initialization and shutdown
CCLAW_API err_t cclaw_init(void);
CCLAW_API void cclaw_shutdown(void);

// Version queries
CCLAW_API void cclaw_get_version(uint32_t* major, uint32_t* minor, uint32_t* patch);
CCLAW_API const char* cclaw_get_version_string(void);

// Platform information
CCLAW_API const char* cclaw_get_platform_name(void);
CCLAW_API bool cclaw_is_platform_windows(void);
CCLAW_API bool cclaw_is_platform_linux(void);
CCLAW_API bool cclaw_is_platform_macos(void);
CCLAW_API bool cclaw_is_platform_android(void);

#endif // CCLAW_H