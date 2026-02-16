// json_config.h - Simple JSON configuration parser for CClaw
// SPDX-License-Identifier: MIT
// This is a minimal JSON parser specifically designed for configuration files

#ifndef JSON_CONFIG_H
#define JSON_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// JSON value types
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

// Forward declaration
typedef struct json_value_t json_value_t;
typedef struct json_object_t json_object_t;
typedef struct json_array_t json_array_t;

// JSON value structure
struct json_value_t {
    json_type_t type;
    union {
        bool boolean;
        double number;
        char* string;
        json_array_t* array;
        json_object_t* object;
    };
};

// JSON array (linked list)
struct json_array_t {
    json_value_t value;
    json_array_t* next;
};

// JSON object entry (linked list)
typedef struct json_entry_t {
    char* key;
    json_value_t value;
    struct json_entry_t* next;
} json_entry_t;

// JSON object
struct json_object_t {
    json_entry_t* entries;
};

// Parse JSON from string
// Returns NULL on parse error
json_value_t* json_parse(const char* text);

// Parse JSON from file
json_value_t* json_parse_file(const char* filename);

// Free JSON value and all children
void json_free(json_value_t* value);

// Getters for object properties
json_value_t* json_object_get(json_object_t* obj, const char* key);
bool json_object_get_bool(json_object_t* obj, const char* key, bool default_val);
double json_object_get_number(json_object_t* obj, const char* key, double default_val);
const char* json_object_get_string(json_object_t* obj, const char* key, const char* default_val);
json_array_t* json_object_get_array(json_object_t* obj, const char* key);
json_object_t* json_object_get_object(json_object_t* obj, const char* key);

// Check if object has key
bool json_object_has(json_object_t* obj, const char* key);

// Array operations
size_t json_array_length(json_array_t* arr);
json_value_t* json_array_get(json_array_t* arr, size_t index);

// Type checks
bool json_is_null(json_value_t* val);
bool json_is_bool(json_value_t* val);
bool json_is_number(json_value_t* val);
bool json_is_string(json_value_t* val);
bool json_is_array(json_value_t* val);
bool json_is_object(json_value_t* val);

// Value extraction
bool json_as_bool(json_value_t* val, bool default_val);
double json_as_number(json_value_t* val, double default_val);
const char* json_as_string(json_value_t* val, const char* default_val);
json_array_t* json_as_array(json_value_t* val);
json_object_t* json_as_object(json_value_t* val);

// JSON serialization
char* json_print(json_value_t* value, bool pretty);

// Create JSON values
json_value_t* json_create_null(void);
json_value_t* json_create_bool(bool value);
json_value_t* json_create_number(double value);
json_value_t* json_create_string(const char* value);
json_value_t* json_create_array(void);
json_value_t* json_create_object(void);

// Add to array
void json_array_append(json_value_t* arr, json_value_t* item);

// Add to object (takes ownership of value)
void json_object_set(json_value_t* obj, const char* key, json_value_t* value);
void json_object_set_bool(json_value_t* obj, const char* key, bool value);
void json_object_set_number(json_value_t* obj, const char* key, double value);
void json_object_set_string(json_value_t* obj, const char* key, const char* value);

#endif // JSON_CONFIG_H
