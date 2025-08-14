#include "ConfigYAML.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

// Internal parsing state structure
typedef struct {
    YAMLAppConfig* config;
    yaml_parser_t parser;
    yaml_event_t event;
    bool error_occurred;
    char error_message[512];
    int channels_allocated; // Track allocated channels
} YAMLParseContext;

// --- Private Function Prototypes ---
static bool parse_yaml_document(YAMLParseContext* ctx);
static bool parse_mapping(YAMLParseContext* ctx);
static bool parse_metadata_section(YAMLParseContext* ctx);
static bool parse_hardware_section(YAMLParseContext* ctx);
static bool parse_system_section(YAMLParseContext* ctx);
static bool parse_channels_section(YAMLParseContext* ctx);
static bool parse_single_channel(YAMLParseContext* ctx, Channel* channel);
static bool parse_calibration_section(YAMLParseContext* ctx, Channel* channel);
static bool parse_adc_section(YAMLParseContext* ctx, Channel* channel);
static bool parse_validation_section(YAMLParseContext* ctx, Channel* channel);
static bool parse_influxdb_section(YAMLParseContext* ctx);
static bool parse_logging_section(YAMLParseContext* ctx);
static bool parse_battery_section(YAMLParseContext* ctx);
static bool parse_gps_section(YAMLParseContext* ctx);
static bool parse_network_section(YAMLParseContext* ctx);
static bool expect_event_type(YAMLParseContext* ctx, yaml_event_type_t expected);
static bool get_scalar_value(YAMLParseContext* ctx, char* buffer, size_t buffer_size);
static bool get_scalar_double(YAMLParseContext* ctx, double* value);
static bool get_scalar_int(YAMLParseContext* ctx, int* value);
static bool get_scalar_long(YAMLParseContext* ctx, long* value);
static bool get_scalar_bool(YAMLParseContext* ctx, bool* value);
static bool get_current_scalar_key(YAMLParseContext* ctx, char* buffer, size_t buffer_size);
static bool skip_mapping(YAMLParseContext* ctx);
static bool skip_sequence(YAMLParseContext* ctx);
static bool expand_environment_variables(char* value, size_t max_size);
static void set_parse_error(YAMLParseContext* ctx, const char* message);
static void trim_whitespace(char* str);

// --- Public API Implementation ---

bool config_yaml_is_available(void) {
    yaml_parser_t parser;
    if (yaml_parser_initialize(&parser)) {
        yaml_parser_delete(&parser);
        return true;
    }
    return false;
}

YAMLAppConfig* config_yaml_load(const char* filename) {
    if (!filename) {
        fprintf(stderr, "ConfigYAML: Filename cannot be NULL\n");
        return NULL;
    }

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ConfigYAML: Failed to open file '%s': %s\n", 
                filename, strerror(errno));
        return NULL;
    }

    YAMLParseContext ctx = {0};
    ctx.config = calloc(1, sizeof(YAMLAppConfig));
    if (!ctx.config) {
        fprintf(stderr, "ConfigYAML: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }
    
    // Initialize YAML parser
    if (!yaml_parser_initialize(&ctx.parser)) {
        fprintf(stderr, "ConfigYAML: Failed to initialize YAML parser\n");
        free(ctx.config);
        fclose(file);
        return NULL;
    }

    yaml_parser_set_input_file(&ctx.parser, file);

    // Parse the YAML document
    bool success = parse_yaml_document(&ctx);

    // Cleanup
    yaml_parser_delete(&ctx.parser);
    fclose(file);

    if (!success) {
        fprintf(stderr, "ConfigYAML: Parse failed: %s\n", ctx.error_message);
        config_yaml_free(ctx.config);
        return NULL;
    }

    // Expand environment variables in InfluxDB configuration
    expand_environment_variables(ctx.config->influxdb.url, sizeof(ctx.config->influxdb.url));
    expand_environment_variables(ctx.config->influxdb.bucket, sizeof(ctx.config->influxdb.bucket));
    expand_environment_variables(ctx.config->influxdb.org, sizeof(ctx.config->influxdb.org));
    expand_environment_variables(ctx.config->influxdb.token, sizeof(ctx.config->influxdb.token));

    return ctx.config;
}

ConfigYAMLResult config_yaml_validate(const YAMLAppConfig* config, 
                                     char* error_message, 
                                     size_t error_size) {
    if (!config) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size, "Configuration is NULL");
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    if (config->channel_count == 0) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size, "No channels configured");
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    if (config->channel_count > NUM_CHANNELS) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size, 
                    "Too many channels configured (%zu), maximum is %d", 
                    config->channel_count, NUM_CHANNELS);
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    return CONFIG_YAML_SUCCESS;
}

ConfigYAMLResult config_yaml_validate_comprehensive(const YAMLAppConfig* config,
                                                   char* error_message,
                                                   size_t error_size) {
    if (!config) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size, "Configuration is NULL");
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    // Basic validation first
    ConfigYAMLResult basic_result = config_yaml_validate(config, error_message, error_size);
    if (basic_result != CONFIG_YAML_SUCCESS) {
        return basic_result;
    }

    // Validate system timing parameters
    if (config->system.main_loop_interval_ms <= 0 || config->system.main_loop_interval_ms > 10000) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size, 
                    "Invalid main_loop_interval_ms: %d (must be 1-10000)", 
                    config->system.main_loop_interval_ms);
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    if (config->system.data_send_interval_ms <= 0 || config->system.data_send_interval_ms > 60000) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size,
                    "Invalid data_send_interval_ms: %d (must be 1-60000)",
                    config->system.data_send_interval_ms);
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    // Validate channel configurations
    for (size_t i = 0; i < config->channel_count; i++) {
        const Channel* ch = &config->channels[i];
        
        // Skip inactive channels
        if (!ch->is_active) continue;

        // Check for duplicate channel IDs
        for (size_t j = i + 1; j < config->channel_count; j++) {
            if (config->channels[j].is_active && 
                strcmp(ch->id, config->channels[j].id) == 0) {
                if (error_message && error_size > 0) {
                    snprintf(error_message, error_size,
                            "Duplicate channel ID: '%s' (channels %zu and %zu)",
                            ch->id, i, j);
                }
                return CONFIG_YAML_ERROR_VALIDATION_FAILED;
            }
        }

        // Validate calibration values
        if (ch->slope == 0.0) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Channel '%s': slope cannot be zero", ch->id);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }

        // Check for reasonable calibration ranges
        if (fabs(ch->slope) > 1000.0 || fabs(ch->slope) < 1e-9) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Channel '%s': suspicious slope value %.2e (check calibration)",
                        ch->id, ch->slope);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }

        if (fabs(ch->offset) > 100000.0) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Channel '%s': suspicious offset value %.2f (check calibration)",
                        ch->id, ch->offset);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }
    }

    // Validate battery configuration
    if (config->battery.coulomb_counting_enabled) {
        if (config->battery.capacity_ah <= 0.0 || config->battery.capacity_ah > 10000.0) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Invalid battery capacity: %.1f Ah (must be 0.1-10000)",
                        config->battery.capacity_ah);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }

        // Check that battery current channel exists
        bool current_channel_found = false;
        for (size_t i = 0; i < config->channel_count; i++) {
            if (config->channels[i].is_active &&
                strcmp(config->channels[i].id, config->battery.current_channel_id) == 0) {
                current_channel_found = true;
                break;
            }
        }

        if (!current_channel_found) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Battery current channel '%s' not found in active channels",
                        config->battery.current_channel_id);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }
    }

    // Validate network configuration
    if (config->network.socket_server_enabled) {
        if (config->network.socket_port <= 1024 || config->network.socket_port > 65535) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Invalid socket port: %d (must be 1025-65535)",
                        config->network.socket_port);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }
        
        if (config->network.update_interval_ms < 100 || config->network.update_interval_ms > 10000) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "Invalid update interval: %d ms (must be 100-10000)",
                        config->network.update_interval_ms);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }
    }

    // Validate environment variables are available
    const char* required_env_vars[] = {
        config->influxdb.url,
        config->influxdb.bucket, 
        config->influxdb.org,
        config->influxdb.token
    };

    for (size_t i = 0; i < sizeof(required_env_vars)/sizeof(required_env_vars[0]); i++) {
        const char* var = required_env_vars[i];
        if (var && var[0] == '$' && var[1] == '{') {
            // Extract variable name for validation
            char* end = strchr(var + 2, '}');
            if (end) {
                char var_name[256];
                size_t var_name_len = end - var - 2;
                if (var_name_len < sizeof(var_name)) {
                    strncpy(var_name, var + 2, var_name_len);
                    var_name[var_name_len] = '\0';
                    
                    if (!getenv(var_name)) {
                        if (error_message && error_size > 0) {
                            snprintf(error_message, error_size,
                                    "Required environment variable '%s' not set", var_name);
                        }
                        return CONFIG_YAML_ERROR_ENVIRONMENT_VARIABLE;
                    }
                }
            }
        }
    }

    return CONFIG_YAML_SUCCESS;
}

ConfigYAMLResult config_yaml_validate_hardware(const YAMLAppConfig* config,
                                              char* error_message,
                                              size_t error_size) {
    if (!config) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size, "Configuration is NULL");
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    // Validate I2C address range
    // 
    // I2C uses 7-bit addressing in an 8-bit field:
    // Bit:  7   6   5   4   3   2   1   0
    //      [A6][A5][A4][A3][A2][A1][A0][R/W]
    //
    // Reserved address ranges per I2C specification:
    // 0x00-0x02: Special purposes
    //   0x00 = General call address (broadcast)
    //   0x01 = CBUS address compatibility
    //   0x02 = Reserved for different bus format
    // 0x78-0x7F: Reserved for future use and 10-bit addressing
    //   0x78-0x7B = 10-bit addressing escape sequences
    //   0x7C-0x7F = Reserved for future use
    //
    // Valid device addresses: 0x03-0x77 (117 total usable addresses)
    if (config->hardware.i2c_address < 0x03 || config->hardware.i2c_address > 0x77) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size,
                    "Invalid I2C address: 0x%02lx (must be 0x03-0x77). "
                    "Addresses 0x00-0x02 are reserved for I2C protocol functions, "
                    "and 0x78-0x7F are reserved for 10-bit addressing and future use.",
                    config->hardware.i2c_address);
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    // Check I2C bus path accessibility (basic check)
    if (access(config->hardware.i2c_bus, F_OK) != 0) {
        if (error_message && error_size > 0) {
            snprintf(error_message, error_size,
                    "I2C bus path not accessible: %s", config->hardware.i2c_bus);
        }
        return CONFIG_YAML_ERROR_VALIDATION_FAILED;
    }

    // Validate CSV directory path  
    if (config->logging.csv_enabled) {
        struct stat st;
        if (stat(config->logging.csv_directory, &st) != 0) {
            // Try to create the directory
            if (mkdir(config->logging.csv_directory, 0755) != 0) {
                if (error_message && error_size > 0) {
                    snprintf(error_message, error_size,
                            "Cannot access or create CSV directory: %s", 
                            config->logging.csv_directory);
                }
                return CONFIG_YAML_ERROR_VALIDATION_FAILED;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            if (error_message && error_size > 0) {
                snprintf(error_message, error_size,
                        "CSV path exists but is not a directory: %s",
                        config->logging.csv_directory);
            }
            return CONFIG_YAML_ERROR_VALIDATION_FAILED;
        }
    }

    return CONFIG_YAML_SUCCESS;
}

void config_yaml_free(YAMLAppConfig* config) {
    if (!config) return;

    if (config->channels) {
        free(config->channels);
    }
    free(config);
}

const char* config_yaml_error_string(ConfigYAMLResult result) {
    switch (result) {
        case CONFIG_YAML_SUCCESS:
            return "Success";
        case CONFIG_YAML_ERROR_FILE_NOT_FOUND:
            return "Configuration file not found";
        case CONFIG_YAML_ERROR_PARSE_FAILED:
            return "YAML parsing failed";
        case CONFIG_YAML_ERROR_INVALID_STRUCTURE:
            return "Invalid YAML structure";
        case CONFIG_YAML_ERROR_VALIDATION_FAILED:
            return "Configuration validation failed";
        case CONFIG_YAML_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case CONFIG_YAML_ERROR_ENVIRONMENT_VARIABLE:
            return "Environment variable expansion failed";
        default:
            return "Unknown error";
    }
}

// --- Private Function Implementations ---

static bool parse_yaml_document(YAMLParseContext* ctx) {
    bool in_document = false;
    
    while (ctx->event.type != YAML_STREAM_END_EVENT) {
        yaml_event_delete(&ctx->event);
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
            set_parse_error(ctx, "YAML parser error");
            return false;
        }

        switch (ctx->event.type) {
            case YAML_STREAM_START_EVENT:
                break;
                
            case YAML_DOCUMENT_START_EVENT:
                in_document = true;
                break;
                
            case YAML_MAPPING_START_EVENT:
                if (in_document) {
                    bool result = parse_mapping(ctx);
                    yaml_event_delete(&ctx->event);
                    if (!result) return false;
                    continue; // Skip the event deletion at the end
                }
                break;
                
            case YAML_DOCUMENT_END_EVENT:
                in_document = false;
                break;
                
            case YAML_STREAM_END_EVENT:
                break;
                
            default:
                break;
        }

        if (ctx->error_occurred) {
            yaml_event_delete(&ctx->event);
            return false;
        }
    }

    return true;
}

static bool parse_mapping(YAMLParseContext* ctx) {
    char key[256];
    
    while (true) {
        // Get the next event
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
            set_parse_error(ctx, "Failed to parse YAML event");
            return false;
        }
        
        // Check for end of mapping
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        // Expect key (scalar)
        if (ctx->event.type != YAML_SCALAR_EVENT) {
            set_parse_error(ctx, "Expected scalar key in mapping");
            yaml_event_delete(&ctx->event);
            return false;
        }
        
        // Copy the key
        strncpy(key, (char*)ctx->event.data.scalar.value, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        yaml_event_delete(&ctx->event);
        
#ifdef DEBUG
        fprintf(stderr, "DEBUG: Parsing section: '%s'\n", key);
#endif
        
        // Parse the value based on the key
        if (strcmp(key, "metadata") == 0) {
            if (!parse_metadata_section(ctx)) return false;
        } else if (strcmp(key, "hardware") == 0) {
            if (!parse_hardware_section(ctx)) return false;
        } else if (strcmp(key, "system") == 0) {
            if (!parse_system_section(ctx)) return false;
        } else if (strcmp(key, "channels") == 0) {
            if (!parse_channels_section(ctx)) return false;
        } else if (strcmp(key, "influxdb") == 0) {
            if (!parse_influxdb_section(ctx)) return false;
        } else if (strcmp(key, "logging") == 0) {
            if (!parse_logging_section(ctx)) return false;
        } else if (strcmp(key, "battery") == 0) {
            if (!parse_battery_section(ctx)) return false;
        } else if (strcmp(key, "gps") == 0) {
            if (!parse_gps_section(ctx)) return false;
        } else if (strcmp(key, "network") == 0) {
            if (!parse_network_section(ctx)) return false;
        } else {
            // Skip unknown sections
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
                set_parse_error(ctx, "Failed to parse unknown section");
                return false;
            }
            
            if (ctx->event.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&ctx->event);
                if (!skip_mapping(ctx)) return false;
            } else if (ctx->event.type == YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&ctx->event);
                if (!skip_sequence(ctx)) return false;
            } else {
                yaml_event_delete(&ctx->event);
            }
        }
        
        if (ctx->error_occurred) return false;
    }
    
    return true;
}

static bool parse_metadata_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        // Extract key from current event
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "version") == 0) {
            if (!get_scalar_value(ctx, ctx->config->metadata.version, 
                                sizeof(ctx->config->metadata.version))) return false;
        } else if (strcmp(key, "calibration_date") == 0) {
            if (!get_scalar_value(ctx, ctx->config->metadata.calibration_date,
                                sizeof(ctx->config->metadata.calibration_date))) return false;
        } else if (strcmp(key, "calibrated_by") == 0) {
            if (!get_scalar_value(ctx, ctx->config->metadata.calibrated_by,
                                sizeof(ctx->config->metadata.calibrated_by))) return false;
        } else if (strcmp(key, "description") == 0 || strcmp(key, "notes") == 0) {
            if (!get_scalar_value(ctx, ctx->config->metadata.notes,
                                sizeof(ctx->config->metadata.notes))) return false;
        } else {
            // Skip unknown metadata fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_hardware_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        // Extract key from current event
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "i2c_bus") == 0) {
            if (!get_scalar_value(ctx, ctx->config->hardware.i2c_bus,
                                sizeof(ctx->config->hardware.i2c_bus))) return false;
        } else if (strcmp(key, "i2c_address") == 0) {
            if (!get_scalar_long(ctx, &ctx->config->hardware.i2c_address)) return false;
        } else {
            // Skip unknown hardware fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_system_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);

        if (strcmp(key, "main_loop_interval_ms") == 0) {
            if (!get_scalar_int(ctx, &ctx->config->system.main_loop_interval_ms)) return false;
        } else if (strcmp(key, "data_send_interval_ms") == 0) {
            if (!get_scalar_int(ctx, &ctx->config->system.data_send_interval_ms)) return false;
        } else {
            // Skip unknown system fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_channels_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_SEQUENCE_START_EVENT)) return false;
    
    // Allocate initial channels array
    ctx->channels_allocated = 8; // Start with 8, grow as needed
    ctx->config->channels = calloc(ctx->channels_allocated, sizeof(Channel));
    if (!ctx->config->channels) {
        set_parse_error(ctx, "Failed to allocate channels array");
        return false;
    }
    
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        // Grow channels array if needed
        if (ctx->config->channel_count >= ctx->channels_allocated) {
            ctx->channels_allocated *= 2;
            Channel* new_channels = realloc(ctx->config->channels, 
                                          ctx->channels_allocated * sizeof(Channel));
            if (!new_channels) {
                set_parse_error(ctx, "Failed to grow channels array");
                yaml_event_delete(&ctx->event);
                return false;
            }
            ctx->config->channels = new_channels;
        }
        
        if (ctx->event.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ctx->event);
            if (!parse_single_channel(ctx, &ctx->config->channels[ctx->config->channel_count])) {
                return false;
            }
            ctx->config->channel_count++;
        } else {
            set_parse_error(ctx, "Expected mapping in channels sequence");
            yaml_event_delete(&ctx->event);
            return false;
        }
    }
    
    return true;
}

static bool parse_single_channel(YAMLParseContext* ctx, Channel* channel) {
    // Initialize channel with defaults
    channel_init(channel);
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "pin") == 0) {
            char pin_str[16];
            if (!get_scalar_value(ctx, pin_str, sizeof(pin_str))) return false;
            
            // Parse pin string to pin number
            if (strcmp(pin_str, "A0") == 0) {
                channel->pin = 0;
            } else if (strcmp(pin_str, "A1") == 0) {
                channel->pin = 1;
            } else if (strcmp(pin_str, "A2") == 0) {
                channel->pin = 2;
            } else if (strcmp(pin_str, "A3") == 0) {
                channel->pin = 3;
            } else {
                // Try parsing as integer
                char* endptr;
                long pin_num = strtol(pin_str, &endptr, 10);
                if (*endptr == '\0' && pin_num >= 0 && pin_num <= 3) {
                    channel->pin = (int)pin_num;
                } else {
                    channel->pin = -1; // Invalid pin
                }
            }
        } else if (strcmp(key, "id") == 0) {
            if (!get_scalar_value(ctx, channel->id, sizeof(channel->id))) return false;
        } else if (strcmp(key, "description") == 0) {
            char description[256]; // Read but don't store (not in Channel struct)
            if (!get_scalar_value(ctx, description, sizeof(description))) return false;
        } else if (strcmp(key, "unit") == 0) {
            if (!get_scalar_value(ctx, channel->unit, sizeof(channel->unit))) return false;
        } else if (strcmp(key, "calibration") == 0) {
            if (!parse_calibration_section(ctx, channel)) return false;
        } else if (strcmp(key, "adc") == 0) {
            if (!parse_adc_section(ctx, channel)) return false;
        } else if (strcmp(key, "validation") == 0) {
            if (!parse_validation_section(ctx, channel)) return false;
        } else {
            // Skip unknown channel fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            if (ctx->event.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&ctx->event);
                if (!skip_mapping(ctx)) return false;
            } else {
                yaml_event_delete(&ctx->event);
            }
        }
    }
    
    // Set channel as active if it has a valid ID
    if (strcmp(channel->id, "NC") != 0 && strlen(channel->id) > 0) {
        channel->is_active = true;
    }
    
    return true;
}

static bool parse_calibration_section(YAMLParseContext* ctx, Channel* channel) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "slope") == 0) {
            if (!get_scalar_double(ctx, &channel->slope)) return false;
        } else if (strcmp(key, "offset") == 0) {
            if (!get_scalar_double(ctx, &channel->offset)) return false;
        } else {
            // Skip other calibration fields (r_squared, calibration_points, etc.)
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_adc_section(YAMLParseContext* ctx, Channel* channel) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "gain") == 0) {
            if (!get_scalar_value(ctx, channel->gain_setting, 
                                sizeof(channel->gain_setting))) return false;
        } else if (strcmp(key, "filter_alpha") == 0) {
            if (!get_scalar_double(ctx, &channel->filter_alpha)) return false;
        } else {
            // Skip other ADC fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_validation_section(YAMLParseContext* ctx, Channel* channel) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        // Validation fields not currently stored in Channel struct
        // Skip all validation fields for now
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        yaml_event_delete(&ctx->event);
    }
    
    return true;
}

static bool parse_influxdb_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "url") == 0) {
            if (!get_scalar_value(ctx, ctx->config->influxdb.url,
                                sizeof(ctx->config->influxdb.url))) return false;
        } else if (strcmp(key, "bucket") == 0) {
            if (!get_scalar_value(ctx, ctx->config->influxdb.bucket,
                                sizeof(ctx->config->influxdb.bucket))) return false;
        } else if (strcmp(key, "org") == 0) {
            if (!get_scalar_value(ctx, ctx->config->influxdb.org,
                                sizeof(ctx->config->influxdb.org))) return false;
        } else if (strcmp(key, "token") == 0) {
            if (!get_scalar_value(ctx, ctx->config->influxdb.token,
                                sizeof(ctx->config->influxdb.token))) return false;
        } else {
            // Skip other InfluxDB fields (measurement, tags, etc.)
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            if (ctx->event.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&ctx->event);
                if (!skip_mapping(ctx)) return false;
            } else {
                yaml_event_delete(&ctx->event);
            }
        }
    }
    
    return true;
}

static bool parse_logging_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "csv_enabled") == 0) {
            if (!get_scalar_bool(ctx, &ctx->config->logging.csv_enabled)) return false;
        } else if (strcmp(key, "csv_directory") == 0) {
            if (!get_scalar_value(ctx, ctx->config->logging.csv_directory,
                                sizeof(ctx->config->logging.csv_directory))) return false;
        } else {
            // Skip other logging fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_battery_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "coulomb_counting_enabled") == 0) {
            if (!get_scalar_bool(ctx, &ctx->config->battery.coulomb_counting_enabled)) return false;
        } else if (strcmp(key, "capacity_ah") == 0) {
            if (!get_scalar_double(ctx, &ctx->config->battery.capacity_ah)) return false;
        } else if (strcmp(key, "current_channel_id") == 0) {
            if (!get_scalar_value(ctx, ctx->config->battery.current_channel_id,
                                sizeof(ctx->config->battery.current_channel_id))) return false;
        } else {
            // Skip other battery fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

static bool parse_gps_section(YAMLParseContext* ctx) {
    // GPS section - just skip for now as it's not in our config struct
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    return skip_mapping(ctx);
}

static bool parse_network_section(YAMLParseContext* ctx) {
    if (!expect_event_type(ctx, YAML_MAPPING_START_EVENT)) return false;
    
    char key[256];
    while (true) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
        
        if (ctx->event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ctx->event);
            break;
        }
        
        if (!get_current_scalar_key(ctx, key, sizeof(key))) {
            yaml_event_delete(&ctx->event);
            return false;
        }
        yaml_event_delete(&ctx->event);
        
        if (strcmp(key, "socket_server_enabled") == 0) {
            if (!get_scalar_bool(ctx, &ctx->config->network.socket_server_enabled)) return false;
        } else if (strcmp(key, "socket_port") == 0) {
            if (!get_scalar_int(ctx, &ctx->config->network.socket_port)) return false;
        } else if (strcmp(key, "update_interval_ms") == 0) {
            if (!get_scalar_int(ctx, &ctx->config->network.update_interval_ms)) return false;
        } else {
            // Skip unknown network fields
            if (!yaml_parser_parse(&ctx->parser, &ctx->event)) return false;
            yaml_event_delete(&ctx->event);
        }
    }
    
    return true;
}

// --- Helper Functions ---

static bool expect_event_type(YAMLParseContext* ctx, yaml_event_type_t expected) {
    if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
        set_parse_error(ctx, "Failed to parse expected event");
        return false;
    }
    
    if (ctx->event.type != expected) {
        set_parse_error(ctx, "Unexpected event type");
        yaml_event_delete(&ctx->event);
        return false;
    }
    
    yaml_event_delete(&ctx->event);
    return true;
}

static bool get_scalar_value(YAMLParseContext* ctx, char* buffer, size_t buffer_size) {
    if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
        set_parse_error(ctx, "Failed to parse scalar value");
        return false;
    }
    
#ifdef DEBUG
    const char* event_names[] = {
        "NONE", "STREAM_START", "STREAM_END", "DOCUMENT_START", "DOCUMENT_END",
        "ALIAS", "SCALAR", "SEQUENCE_START", "SEQUENCE_END", "MAPPING_START", "MAPPING_END"
    };
    if (ctx->event.type < sizeof(event_names)/sizeof(event_names[0])) {
        fprintf(stderr, "DEBUG: get_scalar_value got event: %s\n", event_names[ctx->event.type]);
    }
    if (ctx->event.type == YAML_SCALAR_EVENT) {
        fprintf(stderr, "DEBUG: scalar value: '%s'\n", (char*)ctx->event.data.scalar.value);
    }
#endif
    
    if (ctx->event.type != YAML_SCALAR_EVENT) {
        set_parse_error(ctx, "Expected scalar value");
        yaml_event_delete(&ctx->event);
        return false;
    }
    
    strncpy(buffer, (char*)ctx->event.data.scalar.value, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    trim_whitespace(buffer);
    
    yaml_event_delete(&ctx->event);
    return true;
}

static bool get_scalar_double(YAMLParseContext* ctx, double* value) {
    char buffer[64];
    if (!get_scalar_value(ctx, buffer, sizeof(buffer))) return false;
    
    char* endptr;
    *value = strtod(buffer, &endptr);
    if (*endptr != '\0') {
        set_parse_error(ctx, "Invalid double value");
        return false;
    }
    
    return true;
}

static bool get_scalar_int(YAMLParseContext* ctx, int* value) {
    char buffer[32];
    if (!get_scalar_value(ctx, buffer, sizeof(buffer))) return false;
    
    char* endptr;
    long long_val = strtol(buffer, &endptr, 10);
    if (*endptr != '\0') {
        set_parse_error(ctx, "Invalid integer value");
        return false;
    }
    
    *value = (int)long_val;
    return true;
}

static bool get_scalar_long(YAMLParseContext* ctx, long* value) {
    char buffer[32];
    if (!get_scalar_value(ctx, buffer, sizeof(buffer))) return false;
    
    char* endptr;
    // Support hex values (0x48)
    int base = (strncmp(buffer, "0x", 2) == 0 || strncmp(buffer, "0X", 2) == 0) ? 16 : 10;
    *value = strtol(buffer, &endptr, base);
    if (*endptr != '\0') {
        set_parse_error(ctx, "Invalid long value");
        return false;
    }
    
    return true;
}

static bool get_scalar_bool(YAMLParseContext* ctx, bool* value) {
    char buffer[16];
    if (!get_scalar_value(ctx, buffer, sizeof(buffer))) return false;
    
    // Convert to lowercase for comparison
    for (char* p = buffer; *p; p++) {
        *p = tolower(*p);
    }
    
    if (strcmp(buffer, "true") == 0 || strcmp(buffer, "yes") == 0 || strcmp(buffer, "1") == 0) {
        *value = true;
    } else if (strcmp(buffer, "false") == 0 || strcmp(buffer, "no") == 0 || strcmp(buffer, "0") == 0) {
        *value = false;
    } else {
        set_parse_error(ctx, "Invalid boolean value");
        return false;
    }
    
    return true;
}

static bool skip_mapping(YAMLParseContext* ctx) {
    int depth = 1;
    
    while (depth > 0) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
            set_parse_error(ctx, "Failed to skip mapping");
            return false;
        }
        
        switch (ctx->event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                break;
            case YAML_SEQUENCE_START_EVENT:
                depth++; // Treat sequences like mappings for skipping
                break;
            case YAML_SEQUENCE_END_EVENT:
                depth--;
                break;
            default:
                break;
        }
        
        yaml_event_delete(&ctx->event);
    }
    
    return true;
}

static bool skip_sequence(YAMLParseContext* ctx) {
    int depth = 1;
    
    while (depth > 0) {
        if (!yaml_parser_parse(&ctx->parser, &ctx->event)) {
            set_parse_error(ctx, "Failed to skip sequence");
            return false;
        }
        
        switch (ctx->event.type) {
            case YAML_SEQUENCE_START_EVENT:
                depth++;
                break;
            case YAML_SEQUENCE_END_EVENT:
                depth--;
                break;
            case YAML_MAPPING_START_EVENT:
                depth++; // Treat mappings like sequences for skipping
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                break;
            default:
                break;
        }
        
        yaml_event_delete(&ctx->event);
    }
    
    return true;
}

static bool expand_environment_variables(char* value, size_t max_size) {
    if (!value || strlen(value) == 0) return true;
    
    // Handle ${VAR} syntax
    if (value[0] == '$' && value[1] == '{') {
        char* end = strchr(value + 2, '}');
        if (!end) return false;
        
        *end = '\0';  // Temporarily null-terminate
        const char* env_value = getenv(value + 2); // System managed memory. Must not be freed.
        *end = '}';   // Restore
        
        if (!env_value) {
            fprintf(stderr, "ConfigYAML: Environment variable '%.*s' not found\n", 
                    (int)(end - value - 2), value + 2);
            return false;
        }
        
        // Replace with environment variable value
        strncpy(value, env_value, max_size - 1);
        value[max_size - 1] = '\0';
    }
    
    return true;
}

static void set_parse_error(YAMLParseContext* ctx, const char* message) {
    ctx->error_occurred = true;
    
    // Get parser position information if available
    yaml_mark_t mark = ctx->parser.problem_mark;
    if (mark.line > 0 || mark.column > 0) {
        snprintf(ctx->error_message, sizeof(ctx->error_message), 
                "%s at line %zu, column %zu", message, mark.line + 1, mark.column + 1);
    } else {
        snprintf(ctx->error_message, sizeof(ctx->error_message), "%s", message);
    }
    
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Parse error - %s\n", ctx->error_message);
#endif
}

static bool get_current_scalar_key(YAMLParseContext* ctx, char* buffer, size_t buffer_size) {
    // Extract key from current event (must be YAML_SCALAR_EVENT)
    if (ctx->event.type != YAML_SCALAR_EVENT) {
        set_parse_error(ctx, "Expected scalar key");
        return false;
    }
    
    strncpy(buffer, (char*)ctx->event.data.scalar.value, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    trim_whitespace(buffer);
    return true;
}

static void trim_whitespace(char* str) {
    char* end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    // All spaces?
    if (*str == 0) return;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    *(end + 1) = '\0';
}

// --- Public API for Application Integration ---

bool config_yaml_map_to_channels(const YAMLAppConfig* config, Channel* channels) {
    if (!config || !channels) {
        fprintf(stderr, "ConfigYAML: Invalid parameters to config_yaml_map_to_channels\n");
        return false;
    }
    
    if (!config->channels) {
        fprintf(stderr, "ConfigYAML: No channels in YAML configuration\n");
        return false;
    }
    
    // Initialize all channels to inactive first
    for (int i = 0; i < NUM_CHANNELS; i++) {
        channel_init(&channels[i]);
        channels[i].is_active = false;
    }
    
    // Map YAML channels to Channel array
    size_t channels_to_map = (config->channel_count < NUM_CHANNELS) ? config->channel_count : NUM_CHANNELS;
    
    for (size_t i = 0; i < channels_to_map; i++) {
        const Channel* yaml_channel = &config->channels[i];
        Channel* target_channel = &channels[i];
        
        // Copy configuration data
        strncpy(target_channel->id, yaml_channel->id, MEASUREMENT_ID_SIZE - 1);
        target_channel->id[MEASUREMENT_ID_SIZE - 1] = '\0';
        
        strncpy(target_channel->unit, yaml_channel->unit, UNIT_SIZE - 1);
        target_channel->unit[UNIT_SIZE - 1] = '\0';
        
        strncpy(target_channel->gain_setting, yaml_channel->gain_setting, GAIN_SETTING_SIZE - 1);
        target_channel->gain_setting[GAIN_SETTING_SIZE - 1] = '\0';
        
        // Copy calibration data
        target_channel->slope = yaml_channel->slope;
        target_channel->offset = yaml_channel->offset;
        
        // Copy pin number and filter alpha
        target_channel->pin = yaml_channel->pin;
        target_channel->filter_alpha = yaml_channel->filter_alpha;
        
        // Set as active if it has a valid ID (not "NC" and not empty)
        if (strlen(target_channel->id) > 0 && 
            strncmp(target_channel->id, "NC", MEASUREMENT_ID_SIZE) != 0) {
            target_channel->is_active = true;
        } else {
            target_channel->is_active = false;
        }
        
        // Initialize runtime values
        target_channel->raw_adc_value = 0;
        target_channel->filtered_adc_value = 0.0;
    }
    
#ifdef DEBUG
    printf("ConfigYAML: Mapped %zu channels from YAML to Channel array\n", channels_to_map);
    for (size_t i = 0; i < channels_to_map; i++) {
        printf("  Channel %zu: ID='%s', Active=%s, Slope=%.6f, Offset=%.6f\n",
               i, channels[i].id, channels[i].is_active ? "true" : "false",
               channels[i].slope, channels[i].offset);
    }
#endif
    
    return true;
}