#include "LineProtocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>

// Default buffer size
#define LP_DEFAULT_CAPACITY 1024
#define LP_GROWTH_FACTOR 2
#define LP_MAX_CAPACITY (1024 * 1024)  // 1MB max

// Internal builder structure
struct LineProtocolBuilder {
    char* buffer;
    size_t capacity;
    size_t position;
    size_t measurement_end;     // Track end of measurement part
    size_t tags_end;            // Track end of tags part
    size_t fields_start;        // Track start of fields part
    bool has_measurement;
    bool has_fields;
    bool has_timestamp;
    bool finalized;
};

// Helper function to escape special characters (quotes and backslashes) in strings.
static char* escape_string(const char* input) {
    if (!input) return NULL;
    
    size_t input_len = strlen(input);
    size_t escaped_len = 0;
    
    // First pass: calculate required length
    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];
        if (c == '"' || c == '\\') {
            escaped_len += 2;  // Need to escape with backslash
        } else {
            escaped_len += 1;
        }
    }
    
    char* escaped = malloc(escaped_len + 1);
    if (!escaped) return NULL;
    
    // Second pass: build escaped string
    size_t pos = 0;
    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];
        if (c == '"' || c == '\\') {
            escaped[pos++] = '\\';
            escaped[pos++] = c;
        } else {
            escaped[pos++] = c;
        }
    }
    escaped[pos] = '\0';
    
    return escaped;
}

// Helper function to ensure buffer capacity
static LineProtocolError ensure_capacity(LineProtocolBuilder* builder, size_t needed_space) {
    if (!builder) return LP_ERROR_INVALID_PARAM;
    
    size_t required_capacity = builder->position + needed_space + 1; // +1 for null terminator
    
    if (required_capacity <= builder->capacity) {
        return LP_SUCCESS;
    }
    
    // Calculate new capacity
    size_t new_capacity = builder->capacity * LP_GROWTH_FACTOR;
    while (new_capacity < required_capacity) {
        new_capacity *= LP_GROWTH_FACTOR;
    }
    
    if (new_capacity > LP_MAX_CAPACITY) {
        return LP_ERROR_BUFFER_FULL;
    }
    
    char* new_buffer = realloc(builder->buffer, new_capacity);
    if (!new_buffer) {
        return LP_ERROR_MEMORY_ALLOC;
    }
    
    builder->buffer = new_buffer;
    builder->capacity = new_capacity;
    return LP_SUCCESS;
}

// Helper function to append string to buffer
static LineProtocolError append_string(LineProtocolBuilder* builder, const char* str) {
    if (!builder || !str) return LP_ERROR_INVALID_PARAM;
    
    size_t str_len = strlen(str);
    LineProtocolError err = ensure_capacity(builder, str_len);
    if (err != LP_SUCCESS) return err;
    
    memcpy(builder->buffer + builder->position, str, str_len);
    builder->position += str_len;
    builder->buffer[builder->position] = '\0';
    
    return LP_SUCCESS;
}

// Helper function to append formatted string to buffer
static LineProtocolError append_formatted(LineProtocolBuilder* builder, const char* format, ...) {
    if (!builder || !format) return LP_ERROR_INVALID_PARAM;
    
    va_list args1, args2;
    va_start(args1, format);
    va_copy(args2, args1);
    
    // Calculate required space
    int needed = vsnprintf(NULL, 0, format, args1);
    va_end(args1);
    
    if (needed < 0) {
        va_end(args2);
        return LP_ERROR_INVALID_PARAM;
    }
    
    LineProtocolError err = ensure_capacity(builder, needed);
    if (err != LP_SUCCESS) {
        va_end(args2);
        return err;
    }
    
    int written = vsnprintf(builder->buffer + builder->position, 
                           builder->capacity - builder->position, 
                           format, args2);
    va_end(args2);
    
    if (written < 0 || written != needed) {
        return LP_ERROR_INVALID_STATE;
    }
    
    builder->position += written;
    return LP_SUCCESS;
}

// Public API Implementation

LineProtocolBuilder* lp_builder_create(size_t initial_capacity) {
    if (initial_capacity < 64) initial_capacity = 64;
    if (initial_capacity > LP_MAX_CAPACITY) return NULL;
    
    LineProtocolBuilder* builder = calloc(1, sizeof(LineProtocolBuilder));
    if (!builder) return NULL;
    
    builder->buffer = malloc(initial_capacity);
    if (!builder->buffer) {
        free(builder);
        return NULL;
    }
    
    builder->capacity = initial_capacity;
    builder->position = 0;
    builder->buffer[0] = '\0';
    
    return builder;
}

LineProtocolBuilder* lp_builder_create_default(void) {
    return lp_builder_create(LP_DEFAULT_CAPACITY);
}

void lp_builder_destroy(LineProtocolBuilder* builder) {
    if (builder) {
        free(builder->buffer);
        free(builder);
    }
}

LineProtocolError lp_builder_reset(LineProtocolBuilder* builder) {
    if (!builder) return LP_ERROR_INVALID_PARAM;
    
    builder->position = 0;
    builder->measurement_end = 0;
    builder->tags_end = 0;
    builder->fields_start = 0;
    builder->has_measurement = false;
    builder->has_fields = false;
    builder->has_timestamp = false;
    builder->finalized = false;
    
    if (builder->buffer) {
        builder->buffer[0] = '\0';
    }
    
    return LP_SUCCESS;
}

LineProtocolError lp_set_measurement(LineProtocolBuilder* builder, const char* measurement) {
    if (!builder || !measurement) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!lp_is_valid_measurement_name(measurement)) return LP_ERROR_INVALID_MEASUREMENT;
    
    // Reset builder if measurement is being changed
    lp_builder_reset(builder);
    
    LineProtocolError err = append_string(builder, measurement);
    if (err != LP_SUCCESS) return err;
    
    builder->has_measurement = true;
    builder->measurement_end = builder->position;
    builder->tags_end = builder->position;
    
    return LP_SUCCESS;
}

LineProtocolError lp_add_tag(LineProtocolBuilder* builder, const char* key, const char* value) {
    if (!builder || !key || !value) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!builder->has_measurement) return LP_ERROR_INVALID_STATE;
    if (builder->has_fields) return LP_ERROR_INVALID_STATE; // Tags must come before fields
    if (!lp_is_valid_tag_key(key)) return LP_ERROR_INVALID_TAG_KEY;
    
    LineProtocolError err = append_formatted(builder, ",%s=%s", key, value);
    if (err != LP_SUCCESS) return err;
    
    builder->tags_end = builder->position;
    return LP_SUCCESS;
}

LineProtocolError lp_add_field_double(LineProtocolBuilder* builder, const char* key, double value) {
    if (!builder || !key) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!builder->has_measurement) return LP_ERROR_INVALID_STATE;
    if (!lp_is_valid_field_key(key)) return LP_ERROR_INVALID_FIELD_KEY;
    if (isnan(value) || isinf(value)) return LP_ERROR_INVALID_PARAM;
    
    const char* separator = builder->has_fields ? "," : " ";
    LineProtocolError err = append_formatted(builder, "%s%s=%.6f", separator, key, value);
    if (err != LP_SUCCESS) return err;
    
    if (!builder->has_fields) {
        builder->fields_start = builder->position - strlen(key) - 8; // Approximate
        builder->has_fields = true;
    }
    
    return LP_SUCCESS;
}

LineProtocolError lp_add_field_integer(LineProtocolBuilder* builder, const char* key, int64_t value) {
    if (!builder || !key) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!builder->has_measurement) return LP_ERROR_INVALID_STATE;
    if (!lp_is_valid_field_key(key)) return LP_ERROR_INVALID_FIELD_KEY;
    
    const char* separator = builder->has_fields ? "," : " ";
    LineProtocolError err = append_formatted(builder, "%s%s=%ldi", separator, key, (long long)value);
    if (err != LP_SUCCESS) return err;
    
    if (!builder->has_fields) {
        builder->has_fields = true;
    }
    
    return LP_SUCCESS;
}

LineProtocolError lp_add_field_string(LineProtocolBuilder* builder, const char* key, const char* value) {
    if (!builder || !key || !value) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!builder->has_measurement) return LP_ERROR_INVALID_STATE;
    if (!lp_is_valid_field_key(key)) return LP_ERROR_INVALID_FIELD_KEY;
    
    char* escaped_value = escape_string(value);
    if (!escaped_value) return LP_ERROR_MEMORY_ALLOC;
    
    const char* separator = builder->has_fields ? "," : " ";
    LineProtocolError err = append_formatted(builder, "%s%s=\"%s\"", separator, key, escaped_value);
    
    free(escaped_value);
    if (err != LP_SUCCESS) return err;
    
    if (!builder->has_fields) {
        builder->has_fields = true;
    }
    
    return LP_SUCCESS;
}

LineProtocolError lp_add_field_boolean(LineProtocolBuilder* builder, const char* key, bool value) {
    if (!builder || !key) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!builder->has_measurement) return LP_ERROR_INVALID_STATE;
    if (!lp_is_valid_field_key(key)) return LP_ERROR_INVALID_FIELD_KEY;
    
    const char* separator = builder->has_fields ? "," : " ";
    const char* bool_str = value ? "true" : "false";
    LineProtocolError err = append_formatted(builder, "%s%s=%s", separator, key, bool_str);
    if (err != LP_SUCCESS) return err;
    
    if (!builder->has_fields) {
        builder->has_fields = true;
    }
    
    return LP_SUCCESS;
}

LineProtocolError lp_add_field(LineProtocolBuilder* builder, const LineProtocolField* field) {
    if (!builder || !field || !field->key) return LP_ERROR_INVALID_PARAM;
    
    switch (field->type) {
        case LP_FIELD_TYPE_DOUBLE:
            return lp_add_field_double(builder, field->key, field->value.double_val);
        case LP_FIELD_TYPE_INTEGER:
            return lp_add_field_integer(builder, field->key, field->value.int_val);
        case LP_FIELD_TYPE_STRING:
            return lp_add_field_string(builder, field->key, field->value.string_val);
        case LP_FIELD_TYPE_BOOLEAN:
            return lp_add_field_boolean(builder, field->key, field->value.bool_val);
        default:
            return LP_ERROR_INVALID_PARAM;
    }
}

LineProtocolError lp_set_timestamp(LineProtocolBuilder* builder, int64_t timestamp) {
    if (!builder) return LP_ERROR_INVALID_PARAM;
    if (builder->finalized) return LP_ERROR_INVALID_STATE;
    if (!builder->has_measurement || !builder->has_fields) return LP_ERROR_INVALID_STATE;
    
    LineProtocolError err = append_formatted(builder, " %lld", (long long)timestamp);
    if (err != LP_SUCCESS) return err;
    
    builder->has_timestamp = true;
    return LP_SUCCESS;
}

LineProtocolError lp_set_timestamp_now(LineProtocolBuilder* builder) {
    return lp_set_timestamp(builder, lp_get_current_timestamp());
}

char* lp_copy(LineProtocolBuilder* builder) {
    if (!builder || !builder->has_measurement || !builder->has_fields) {
        return NULL;
    }
    
    // Add current timestamp if none provided
    if (!builder->has_timestamp) {
        if (lp_set_timestamp_now(builder) != LP_SUCCESS) {
            return NULL;
        }
    }
    
    builder->finalized = true;
    
    // Return a copy of the buffer
    char* result = malloc(builder->position + 1);
    if (!result) return NULL;
    
    memcpy(result, builder->buffer, builder->position + 1);
    return result;
}

const char* lp_view(const LineProtocolBuilder* builder) {
    if (!builder) return NULL;
    return builder->buffer;
}

size_t lp_get_length(const LineProtocolBuilder* builder) {
    if (!builder) return 0;
    return builder->position;
}

// Validation functions
bool lp_is_valid_measurement_name(const char* name) {
    if (!name || *name == '\0') return false;
    
    // First character cannot be underscore
    if (*name == '_') return false;
    
    // Check for invalid characters
    for (const char* p = name; *p; p++) {
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.') {
            return false;
        }
    }
    
    return true;
}

bool lp_is_valid_tag_key(const char* key) {
    if (!key || *key == '\0') return false;
    
    // Similar to measurement name but more restrictive
    for (const char* p = key; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            return false;
        }
    }
    
    return true;
}

bool lp_is_valid_field_key(const char* key) {
    return lp_is_valid_tag_key(key); // Same rules for now
}

LineProtocolError lp_validate(const LineProtocolBuilder* builder) {
    if (!builder) return LP_ERROR_INVALID_PARAM;
    if (!builder->has_measurement) return LP_ERROR_INVALID_MEASUREMENT;
    if (!builder->has_fields) return LP_ERROR_INVALID_STATE;
    
    return LP_SUCCESS;
}

// Utility functions
const char* lp_error_string(LineProtocolError error) {
    switch (error) {
        case LP_SUCCESS: return "Success";
        case LP_ERROR_INVALID_PARAM: return "Invalid parameter";
        case LP_ERROR_BUFFER_FULL: return "Buffer full";
        case LP_ERROR_INVALID_STATE: return "Invalid state";
        case LP_ERROR_MEMORY_ALLOC: return "Memory allocation failed";
        case LP_ERROR_INVALID_MEASUREMENT: return "Invalid measurement name";
        case LP_ERROR_INVALID_TAG_KEY: return "Invalid tag key";
        case LP_ERROR_INVALID_FIELD_KEY: return "Invalid field key";
        default: return "Unknown error";
    }
}

int64_t lp_get_current_timestamp(void) {
    return (int64_t)time(NULL);
}

// Convenience functions
LineProtocolError lp_add_gps_fields(LineProtocolBuilder* builder, 
                                   double latitude, double longitude, 
                                   double altitude, double speed) {
    if (!builder) return LP_ERROR_INVALID_PARAM;
    
    LineProtocolError err;
    
    if (!isnan(latitude)) {
        err = lp_add_field_double(builder, "latitude", latitude);
        if (err != LP_SUCCESS) return err;
    }
    
    if (!isnan(longitude)) {
        err = lp_add_field_double(builder, "longitude", longitude);
        if (err != LP_SUCCESS) return err;
    }
    
    if (!isnan(altitude)) {
        err = lp_add_field_double(builder, "altitude", altitude);
        if (err != LP_SUCCESS) return err;
    }
    
    if (!isnan(speed)) {
        err = lp_add_field_double(builder, "speed", speed);
        if (err != LP_SUCCESS) return err;
    }
    
    return LP_SUCCESS;
}