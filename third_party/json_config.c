// json_config.c - Simple JSON configuration parser implementation
// SPDX-License-Identifier: MIT

#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

// Parser state
typedef struct {
    const char* text;
    size_t pos;
    size_t len;
} json_parser_t;

// Skip whitespace
static void skip_whitespace(json_parser_t* p) {
    while (p->pos < p->len && isspace((unsigned char)p->text[p->pos])) {
        p->pos++;
    }
}

// Peek current character
static char peek(json_parser_t* p) {
    if (p->pos >= p->len) return '\0';
    return p->text[p->pos];
}

// Advance and return current character
static char advance(json_parser_t* p) {
    if (p->pos >= p->len) return '\0';
    return p->text[p->pos++];
}

// Check if at end
static bool is_at_end(json_parser_t* p) {
    return p->pos >= p->len;
}

// Parse string (handles basic escape sequences)
static char* parse_string_raw(json_parser_t* p) {
    if (peek(p) != '"') return NULL;
    advance(p); // consume opening quote

    size_t start = p->pos;
    size_t len = 0;

    // First pass: calculate length
    while (!is_at_end(p) && peek(p) != '"') {
        if (peek(p) == '\\') {
            advance(p);
            if (!is_at_end(p)) advance(p);
        } else {
            advance(p);
        }
        len++;
    }

    if (is_at_end(p)) return NULL; // Unterminated string

    // Allocate and copy
    char* str = malloc(len + 1);
    if (!str) return NULL;

    p->pos = start;
    size_t i = 0;

    while (peek(p) != '"') {
        if (peek(p) == '\\') {
            advance(p);
            char c = advance(p);
            switch (c) {
                case '"': str[i++] = '"'; break;
                case '\\': str[i++] = '\\'; break;
                case '/': str[i++] = '/'; break;
                case 'b': str[i++] = '\b'; break;
                case 'f': str[i++] = '\f'; break;
                case 'n': str[i++] = '\n'; break;
                case 'r': str[i++] = '\r'; break;
                case 't': str[i++] = '\t'; break;
                default: str[i++] = c; break;
            }
        } else {
            str[i++] = advance(p);
        }
    }

    str[i] = '\0';
    advance(p); // consume closing quote
    return str;
}

// Forward declaration
static json_value_t* parse_value(json_parser_t* p);

// Parse number
static json_value_t* parse_number(json_parser_t* p) {
    size_t start = p->pos;
    bool has_digits = false;

    // Optional minus sign
    if (peek(p) == '-') advance(p);

    // Integer part
    while (isdigit((unsigned char)peek(p))) {
        advance(p);
        has_digits = true;
    }

    // Fractional part
    if (peek(p) == '.') {
        advance(p);
        while (isdigit((unsigned char)peek(p))) {
            advance(p);
            has_digits = true;
        }
    }

    // Exponent
    if (peek(p) == 'e' || peek(p) == 'E') {
        advance(p);
        if (peek(p) == '+' || peek(p) == '-') advance(p);
        while (isdigit((unsigned char)peek(p))) advance(p);
    }

    if (!has_digits) return NULL;

    char* num_str = malloc(p->pos - start + 1);
    if (!num_str) return NULL;

    strncpy(num_str, p->text + start, p->pos - start);
    num_str[p->pos - start] = '\0';

    json_value_t* val = malloc(sizeof(json_value_t));
    if (val) {
        val->type = JSON_NUMBER;
        val->number = strtod(num_str, NULL);
    }

    free(num_str);
    return val;
}

// Parse array
static json_value_t* parse_array(json_parser_t* p) {
    if (peek(p) != '[') return NULL;
    advance(p); // consume '['

    json_value_t* arr_val = malloc(sizeof(json_value_t));
    if (!arr_val) return NULL;

    arr_val->type = JSON_ARRAY;
    arr_val->array = malloc(sizeof(json_array_t));
    if (!arr_val->array) {
        free(arr_val);
        return NULL;
    }
    arr_val->array->next = NULL;

    json_array_t* current = NULL;

    skip_whitespace(p);

    if (peek(p) == ']') {
        advance(p);
        return arr_val;
    }

    while (true) {
        skip_whitespace(p);

        json_value_t* item = parse_value(p);
        if (!item) {
            json_free(arr_val);
            return NULL;
        }

        json_array_t* new_item = malloc(sizeof(json_array_t));
        if (!new_item) {
            json_free(item);
            json_free(arr_val);
            return NULL;
        }

        new_item->value = *item;
        free(item); // We copied the value
        new_item->next = NULL;

        if (current) {
            current->next = new_item;
        } else {
            arr_val->array = new_item;
        }
        current = new_item;

        skip_whitespace(p);

        if (peek(p) == ',') {
            advance(p);
            continue;
        } else if (peek(p) == ']') {
            advance(p);
            break;
        } else {
            json_free(arr_val);
            return NULL;
        }
    }

    return arr_val;
}

// Parse object
static json_value_t* parse_object(json_parser_t* p) {
    if (peek(p) != '{') return NULL;
    advance(p); // consume '{'

    json_value_t* obj_val = malloc(sizeof(json_value_t));
    if (!obj_val) return NULL;

    obj_val->type = JSON_OBJECT;
    obj_val->object = malloc(sizeof(json_object_t));
    if (!obj_val->object) {
        free(obj_val);
        return NULL;
    }
    obj_val->object->entries = NULL;

    json_entry_t* current = NULL;

    skip_whitespace(p);

    if (peek(p) == '}') {
        advance(p);
        return obj_val;
    }

    while (true) {
        skip_whitespace(p);

        // Parse key
        char* key = parse_string_raw(p);
        if (!key) {
            json_free(obj_val);
            return NULL;
        }

        skip_whitespace(p);

        if (peek(p) != ':') {
            free(key);
            json_free(obj_val);
            return NULL;
        }
        advance(p); // consume ':'

        skip_whitespace(p);

        // Parse value
        json_value_t* value = parse_value(p);
        if (!value) {
            free(key);
            json_free(obj_val);
            return NULL;
        }

        // Create entry
        json_entry_t* entry = malloc(sizeof(json_entry_t));
        if (!entry) {
            free(key);
            json_free(value);
            json_free(obj_val);
            return NULL;
        }

        entry->key = key;
        entry->value = *value;
        free(value); // We copied the value
        entry->next = NULL;

        if (current) {
            current->next = entry;
        } else {
            obj_val->object->entries = entry;
        }
        current = entry;

        skip_whitespace(p);

        if (peek(p) == ',') {
            advance(p);
            continue;
        } else if (peek(p) == '}') {
            advance(p);
            break;
        } else {
            json_free(obj_val);
            return NULL;
        }
    }

    return obj_val;
}

// Parse value
static json_value_t* parse_value(json_parser_t* p) {
    skip_whitespace(p);

    if (is_at_end(p)) return NULL;

    char c = peek(p);

    // String
    if (c == '"') {
        char* str = parse_string_raw(p);
        if (!str) return NULL;

        json_value_t* val = malloc(sizeof(json_value_t));
        if (val) {
            val->type = JSON_STRING;
            val->string = str;
        } else {
            free(str);
        }
        return val;
    }

    // Object
    if (c == '{') {
        return parse_object(p);
    }

    // Array
    if (c == '[') {
        return parse_array(p);
    }

    // Number
    if (c == '-' || isdigit((unsigned char)c)) {
        return parse_number(p);
    }

    // Boolean or null
    const char* text = p->text + p->pos;
    size_t remaining = p->len - p->pos;

    if (remaining >= 4 && strncmp(text, "true", 4) == 0) {
        p->pos += 4;
        json_value_t* val = malloc(sizeof(json_value_t));
        if (val) {
            val->type = JSON_BOOL;
            val->boolean = true;
        }
        return val;
    }

    if (remaining >= 5 && strncmp(text, "false", 5) == 0) {
        p->pos += 5;
        json_value_t* val = malloc(sizeof(json_value_t));
        if (val) {
            val->type = JSON_BOOL;
            val->boolean = false;
        }
        return val;
    }

    if (remaining >= 4 && strncmp(text, "null", 4) == 0) {
        p->pos += 4;
        json_value_t* val = malloc(sizeof(json_value_t));
        if (val) {
            val->type = JSON_NULL;
        }
        return val;
    }

    return NULL;
}

// Parse JSON from string
json_value_t* json_parse(const char* text) {
    if (!text) return NULL;

    json_parser_t parser = {
        .text = text,
        .pos = 0,
        .len = strlen(text)
    };

    json_value_t* value = parse_value(&parser);
    if (!value) return NULL;

    skip_whitespace(&parser);

    if (!is_at_end(&parser)) {
        json_free(value);
        return NULL;
    }

    return value;
}

// Parse JSON from file
json_value_t* json_parse_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return NULL;
    }

    char* content = malloc((size_t)size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(content, 1, (size_t)size, file);
    fclose(file);

    content[read] = '\0';

    json_value_t* value = json_parse(content);
    free(content);

    return value;
}

// Internal: free value contents without freeing the value itself
static void json_free_contents(json_value_t* value) {
    if (!value) return;

    switch (value->type) {
        case JSON_STRING:
            free(value->string);
            value->string = NULL;
            break;

        case JSON_ARRAY: {
            json_array_t* item = value->array;
            while (item) {
                json_array_t* next = item->next;
                json_free_contents(&item->value);
                free(item);
                item = next;
            }
            value->array = NULL;
            break;
        }

        case JSON_OBJECT: {
            json_entry_t* entry = value->object->entries;
            while (entry) {
                json_entry_t* next = entry->next;
                free(entry->key);
                json_free_contents(&entry->value);
                free(entry);
                entry = next;
            }
            free(value->object);
            value->object = NULL;
            break;
        }

        default:
            break;
    }
}

// Free JSON value
void json_free(json_value_t* value) {
    if (!value) return;

    json_free_contents(value);
    free(value);
}

// Get value from object by key
json_value_t* json_object_get(json_object_t* obj, const char* key) {
    if (!obj || !key) return NULL;

    json_entry_t* entry = obj->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return &entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

// Get bool from object
bool json_object_get_bool(json_object_t* obj, const char* key, bool default_val) {
    json_value_t* val = json_object_get(obj, key);
    return json_as_bool(val, default_val);
}

// Get number from object
double json_object_get_number(json_object_t* obj, const char* key, double default_val) {
    json_value_t* val = json_object_get(obj, key);
    return json_as_number(val, default_val);
}

// Get string from object
const char* json_object_get_string(json_object_t* obj, const char* key, const char* default_val) {
    json_value_t* val = json_object_get(obj, key);
    return json_as_string(val, default_val);
}

// Get array from object
json_array_t* json_object_get_array(json_object_t* obj, const char* key) {
    json_value_t* val = json_object_get(obj, key);
    return json_as_array(val);
}

// Get object from object
json_object_t* json_object_get_object(json_object_t* obj, const char* key) {
    json_value_t* val = json_object_get(obj, key);
    return json_as_object(val);
}

// Check if object has key
bool json_object_has(json_object_t* obj, const char* key) {
    return json_object_get(obj, key) != NULL;
}

// Get array length
size_t json_array_length(json_array_t* arr) {
    if (!arr) return 0;

    size_t count = 0;
    while (arr) {
        count++;
        arr = arr->next;
    }
    return count;
}

// Get array item at index
json_value_t* json_array_get(json_array_t* arr, size_t index) {
    if (!arr) return NULL;

    size_t i = 0;
    while (arr) {
        if (i == index) return &arr->value;
        i++;
        arr = arr->next;
    }

    return NULL;
}

// Type checks
bool json_is_null(json_value_t* val) { return val && val->type == JSON_NULL; }
bool json_is_bool(json_value_t* val) { return val && val->type == JSON_BOOL; }
bool json_is_number(json_value_t* val) { return val && val->type == JSON_NUMBER; }
bool json_is_string(json_value_t* val) { return val && val->type == JSON_STRING; }
bool json_is_array(json_value_t* val) { return val && val->type == JSON_ARRAY; }
bool json_is_object(json_value_t* val) { return val && val->type == JSON_OBJECT; }

// Value extraction
bool json_as_bool(json_value_t* val, bool default_val) {
    if (!val || val->type != JSON_BOOL) return default_val;
    return val->boolean;
}

double json_as_number(json_value_t* val, double default_val) {
    if (!val || val->type != JSON_NUMBER) return default_val;
    return val->number;
}

const char* json_as_string(json_value_t* val, const char* default_val) {
    if (!val || val->type != JSON_STRING) return default_val;
    return val->string;
}

json_array_t* json_as_array(json_value_t* val) {
    if (!val || val->type != JSON_ARRAY) return NULL;
    return val->array;
}

json_object_t* json_as_object(json_value_t* val) {
    if (!val || val->type != JSON_OBJECT) return NULL;
    return val->object;
}

// Forward declarations for printing helpers
static void append_char(char** out, size_t* cap, size_t* len, char c);
static void append_string(char** out, size_t* cap, size_t* len, const char* str);

// Helper for printing
static void print_indent(char** out, size_t* cap, size_t* len, int indent) {
    for (int i = 0; i < indent; i++) {
        append_char(out, cap, len, ' ');
        append_char(out, cap, len, ' ');
    }
}

static void append_char(char** out, size_t* cap, size_t* len, char c) {
    if (*len + 1 >= *cap) {
        *cap *= 2;
        char* new_out = realloc(*out, *cap);
        if (!new_out) return;
        *out = new_out;
    }
    (*out)[(*len)++] = c;
}

static void append_string(char** out, size_t* cap, size_t* len, const char* str) {
    size_t slen = strlen(str);
    while (*len + slen + 1 >= *cap) {
        *cap *= 2;
        char* new_out = realloc(*out, *cap);
        if (!new_out) return;
        *out = new_out;
    }
    memcpy(*out + *len, str, slen);
    *len += slen;
}

static void print_value(json_value_t* val, char** out, size_t* cap, size_t* len, int indent, bool pretty);

static void print_string_escaped(const char* str, char** out, size_t* cap, size_t* len) {
    append_char(out, cap, len, '"');

    while (*str) {
        char c = *str++;
        switch (c) {
            case '"': append_string(out, cap, len, "\\\""); break;
            case '\\': append_string(out, cap, len, "\\\\"); break;
            case '\b': append_string(out, cap, len, "\\b"); break;
            case '\f': append_string(out, cap, len, "\\f"); break;
            case '\n': append_string(out, cap, len, "\\n"); break;
            case '\r': append_string(out, cap, len, "\\r"); break;
            case '\t': append_string(out, cap, len, "\\t"); break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    append_string(out, cap, len, buf);
                } else {
                    append_char(out, cap, len, c);
                }
        }
    }

    append_char(out, cap, len, '"');
}

static void print_array(json_array_t* arr, char** out, size_t* cap, size_t* len, int indent, bool pretty) {
    append_char(out, cap, len, '[');

    bool first = true;
    while (arr) {
        if (!first) append_char(out, cap, len, ',');
        if (pretty) append_char(out, cap, len, ' ');

        print_value(&arr->value, out, cap, len, indent, pretty);

        first = false;
        arr = arr->next;
    }

    append_char(out, cap, len, ']');
}

static void print_object(json_object_t* obj, char** out, size_t* cap, size_t* len, int indent, bool pretty) {
    append_char(out, cap, len, '{');

    if (pretty && obj && obj->entries) {
        append_char(out, cap, len, '\n');
    }

    json_entry_t* entry = obj ? obj->entries : NULL;
    bool first = true;

    while (entry) {
        if (!first) {
            append_char(out, cap, len, ',');
            if (pretty) append_char(out, cap, len, '\n');
        }

        if (pretty) print_indent(out, cap, len, indent + 1);

        print_string_escaped(entry->key, out, cap, len);
        append_char(out, cap, len, ':');
        if (pretty) append_char(out, cap, len, ' ');

        print_value(&entry->value, out, cap, len, indent + 1, pretty);

        first = false;
        entry = entry->next;
    }

    if (pretty && obj && obj->entries) {
        append_char(out, cap, len, '\n');
        print_indent(out, cap, len, indent);
    }

    append_char(out, cap, len, '}');
}

static void print_value(json_value_t* val, char** out, size_t* cap, size_t* len, int indent, bool pretty) {
    if (!val) {
        append_string(out, cap, len, "null");
        return;
    }

    switch (val->type) {
        case JSON_NULL:
            append_string(out, cap, len, "null");
            break;

        case JSON_BOOL:
            append_string(out, cap, len, val->boolean ? "true" : "false");
            break;

        case JSON_NUMBER: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val->number);
            append_string(out, cap, len, buf);
            break;
        }

        case JSON_STRING:
            print_string_escaped(val->string, out, cap, len);
            break;

        case JSON_ARRAY:
            print_array(val->array, out, cap, len, indent, pretty);
            break;

        case JSON_OBJECT:
            print_object(val->object, out, cap, len, indent, pretty);
            break;
    }
}

// Serialize JSON to string
char* json_print(json_value_t* value, bool pretty) {
    if (!value) return NULL;

    size_t cap = 1024;
    char* out = malloc(cap);
    if (!out) return NULL;

    size_t len = 0;
    print_value(value, &out, &cap, &len, 0, pretty);

    if (len < cap) {
        out[len] = '\0';
    } else {
        out[cap - 1] = '\0';
    }

    return out;
}

// Create JSON null
json_value_t* json_create_null(void) {
    json_value_t* val = malloc(sizeof(json_value_t));
    if (val) val->type = JSON_NULL;
    return val;
}

// Create JSON bool
json_value_t* json_create_bool(bool value) {
    json_value_t* val = malloc(sizeof(json_value_t));
    if (val) {
        val->type = JSON_BOOL;
        val->boolean = value;
    }
    return val;
}

// Create JSON number
json_value_t* json_create_number(double value) {
    json_value_t* val = malloc(sizeof(json_value_t));
    if (val) {
        val->type = JSON_NUMBER;
        val->number = value;
    }
    return val;
}

// Create JSON string
json_value_t* json_create_string(const char* value) {
    if (!value) return NULL;

    json_value_t* val = malloc(sizeof(json_value_t));
    if (!val) return NULL;

    val->type = JSON_STRING;
    val->string = strdup(value);

    return val;
}

// Create JSON array
json_value_t* json_create_array(void) {
    json_value_t* val = malloc(sizeof(json_value_t));
    if (!val) return NULL;

    val->type = JSON_ARRAY;
    val->array = NULL;  // Empty array, no head node

    return val;
}

// Create JSON object
json_value_t* json_create_object(void) {
    json_value_t* val = malloc(sizeof(json_value_t));
    if (!val) return NULL;

    val->type = JSON_OBJECT;
    val->object = malloc(sizeof(json_object_t));
    if (!val->object) {
        free(val);
        return NULL;
    }
    val->object->entries = NULL;

    return val;
}

// Append to array
void json_array_append(json_value_t* arr, json_value_t* item) {
    if (!arr || arr->type != JSON_ARRAY || !item) return;

    json_array_t* new_item = malloc(sizeof(json_array_t));
    if (!new_item) return;

    new_item->value = *item;
    new_item->next = NULL;

    if (!arr->array || !arr->array->next) {
        // First item or empty
        if (arr->array) {
            // Find end
            json_array_t* last = arr->array;
            while (last->next) last = last->next;
            last->next = new_item;
        } else {
            arr->array = new_item;
        }
    } else {
        // Find end
        json_array_t* last = arr->array;
        while (last->next) last = last->next;
        last->next = new_item;
    }

    free(item); // We copied the value
}

// Set object property
void json_object_set(json_value_t* obj, const char* key, json_value_t* value) {
    if (!obj || obj->type != JSON_OBJECT || !key || !value) return;

    // Check if key already exists
    json_entry_t* entry = obj->object->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Replace value
            json_free(&entry->value);
            entry->value = *value;
            free(value);
            return;
        }
        entry = entry->next;
    }

    // Add new entry
    entry = malloc(sizeof(json_entry_t));
    if (!entry) return;

    entry->key = strdup(key);
    entry->value = *value;
    entry->next = obj->object->entries;
    obj->object->entries = entry;

    free(value);
}

void json_object_set_bool(json_value_t* obj, const char* key, bool value) {
    json_value_t* val = json_create_bool(value);
    if (val) json_object_set(obj, key, val);
}

void json_object_set_number(json_value_t* obj, const char* key, double value) {
    json_value_t* val = json_create_number(value);
    if (val) json_object_set(obj, key, val);
}

void json_object_set_string(json_value_t* obj, const char* key, const char* value) {
    json_value_t* val = json_create_string(value);
    if (val) json_object_set(obj, key, val);
}
