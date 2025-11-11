// LineProtocol.h - Pure protocol formatting module
#ifndef LINEPROTOCOL_H
#define LINEPROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Error codes for LineProtocol operations
typedef enum {
    LP_SUCCESS = 0,
    LP_ERROR_INVALID_PARAM,
    LP_ERROR_BUFFER_FULL,
    LP_ERROR_INVALID_STATE,
    LP_ERROR_MEMORY_ALLOC,
    LP_ERROR_INVALID_MEASUREMENT,
    LP_ERROR_INVALID_TAG_KEY,
    LP_ERROR_INVALID_FIELD_KEY
} LineProtocolError;


// Field types supported by InfluxDB Line Protocol
typedef enum {
    LP_FIELD_TYPE_DOUBLE,
    LP_FIELD_TYPE_INTEGER,
    LP_FIELD_TYPE_STRING,
    LP_FIELD_TYPE_BOOLEAN
} LineProtocolFieldType;

// Field value union
typedef union {
    double double_val;
    int64_t int_val;
    char* string_val;
    bool bool_val;
} LineProtocolValue;

// Field structure
typedef struct {
    char* key;
    LineProtocolFieldType type;
    LineProtocolValue value;
} LineProtocolField;

// Tag structure
typedef struct {
    char* key;
    char* value;
} LineProtocolTag;

// Opaque builder structure
typedef struct LineProtocolBuilder LineProtocolBuilder;

// Builder lifecycle
LineProtocolBuilder* lp_builder_create(size_t initial_capacity);
LineProtocolBuilder* lp_builder_create_default(void);
void lp_builder_destroy(LineProtocolBuilder* builder);
LineProtocolError lp_builder_reset(LineProtocolBuilder* builder);

// Core building operations
LineProtocolError lp_set_measurement(LineProtocolBuilder* builder, const char* measurement);
LineProtocolError lp_add_tag(LineProtocolBuilder* builder, const char* key, const char* value);
LineProtocolError lp_add_field_double(LineProtocolBuilder* builder, const char* key, double value);
LineProtocolError lp_add_field_integer(LineProtocolBuilder* builder, const char* key, int64_t value);
LineProtocolError lp_add_field_string(LineProtocolBuilder* builder, const char* key, const char* value);
LineProtocolError lp_add_field_boolean(LineProtocolBuilder* builder, const char* key, bool value);
LineProtocolError lp_add_field(LineProtocolBuilder* builder, const LineProtocolField* field);
LineProtocolError lp_set_timestamp(LineProtocolBuilder* builder, int64_t timestamp);
LineProtocolError lp_set_timestamp_now(LineProtocolBuilder* builder);

// Output operations
char* lp_copy(LineProtocolBuilder* builder);  // Caller owns returned string
const char* lp_view(const LineProtocolBuilder* builder);  // Read-only view
size_t lp_get_length(const LineProtocolBuilder* builder);

// Validation
bool lp_is_valid_measurement_name(const char* name);
bool lp_is_valid_tag_key(const char* key);
bool lp_is_valid_field_key(const char* key);
LineProtocolError lp_validate(const LineProtocolBuilder* builder);

// Utility functions
const char* lp_error_string(LineProtocolError error);
int64_t lp_get_current_timestamp(void);

#endif // LINEPROTOCOL_H