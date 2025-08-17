#ifndef CONFIG_YAML_H
#define CONFIG_YAML_H

#include "Channel.h"
#include <stdbool.h>
#include <stddef.h>

// Configuration metadata for traceability and documentation
typedef struct {
    char version[32];
    char calibration_date[32];
    char calibrated_by[64];
    char notes[256];
} ConfigMetadata;

// Board configuration for multi-board support
typedef struct {
    int address;        // I2C address (0x48-0x4B)
    char description[64]; // Optional description
} BoardConfig;

// Hardware configuration
typedef struct {
    char i2c_bus[64];
    int i2c_max_retries;     // Maximum I2C read retry attempts
    int i2c_retry_delay_ms;  // Base retry delay in milliseconds
    
    // Multi-board support
    BoardConfig* boards;     // Array of board configurations
    int board_count;         // Number of configured boards
} HardwareConfig;

// System timing configuration  
typedef struct {
    int main_loop_interval_ms;
    int data_send_interval_ms;
} SystemConfig;

// InfluxDB configuration with environment variable support
typedef struct {
    char url[256];
    char bucket[128];
    char org[128];
    char token[256];
} InfluxDBConfig;

// Logging configuration
typedef struct {
    bool csv_enabled;
    char csv_directory[256];
} LoggingConfig;

// Battery monitoring configuration
typedef struct {
    bool coulomb_counting_enabled;
    double capacity_ah;
    char current_channel_id[MEASUREMENT_ID_SIZE];
} BatteryConfig;

// Network configuration
typedef struct {
    bool socket_server_enabled;
    int socket_port;
    int update_interval_ms;
} NetworkConfig;

// Main YAML configuration structure
typedef struct {
    ConfigMetadata metadata;
    HardwareConfig hardware;
    SystemConfig system;
    Channel* channels;
    size_t channel_count;
    InfluxDBConfig influxdb;
    LoggingConfig logging;
    BatteryConfig battery;
    NetworkConfig network;
} YAMLAppConfig;

// Error codes for YAML configuration operations
typedef enum {
    CONFIG_YAML_SUCCESS = 0,
    CONFIG_YAML_ERROR_FILE_NOT_FOUND,
    CONFIG_YAML_ERROR_PARSE_FAILED,
    CONFIG_YAML_ERROR_INVALID_STRUCTURE,
    CONFIG_YAML_ERROR_VALIDATION_FAILED,
    CONFIG_YAML_ERROR_MEMORY_ALLOCATION,
    CONFIG_YAML_ERROR_ENVIRONMENT_VARIABLE
} ConfigYAMLResult;

// --- Public API ---

/**
 * @brief Loads and parses a YAML configuration file.
 * @param filename Path to the YAML configuration file
 * @return Pointer to YAMLAppConfig structure or NULL on failure
 */
YAMLAppConfig* config_yaml_load(const char* filename);

/**
 * @brief Validates a YAML configuration for correctness and consistency.
 * @param config The configuration to validate
 * @param error_message Buffer to store detailed error message (can be NULL)
 * @param error_size Size of the error message buffer
 * @return CONFIG_YAML_SUCCESS if valid, error code otherwise
 */
ConfigYAMLResult config_yaml_validate(const YAMLAppConfig* config, 
                                     char* error_message, 
                                     size_t error_size);

/**
 * @brief Performs comprehensive validation with detailed reporting.
 * @param config The configuration to validate
 * @param error_message Buffer to store detailed error message (can be NULL)  
 * @param error_size Size of the error message buffer
 * @return CONFIG_YAML_SUCCESS if valid, error code otherwise
 */
ConfigYAMLResult config_yaml_validate_comprehensive(const YAMLAppConfig* config,
                                                   char* error_message,
                                                   size_t error_size);

/**
 * @brief Validates hardware configuration and accessibility.
 * @param config The configuration to validate
 * @param error_message Buffer to store detailed error message (can be NULL)
 * @param error_size Size of the error message buffer
 * @return CONFIG_YAML_SUCCESS if valid, error code otherwise
 */
ConfigYAMLResult config_yaml_validate_hardware(const YAMLAppConfig* config,
                                              char* error_message,
                                              size_t error_size);

/**
 * @brief Frees memory allocated for a YAML configuration.
 * @param config The configuration to free
 */
void config_yaml_free(YAMLAppConfig* config);

/**
 * @brief Converts a ConfigYAMLResult to a human-readable string.
 * @param result The result code to convert
 * @return String description of the error
 */
const char* config_yaml_error_string(ConfigYAMLResult result);

/**
 * @brief Checks if YAML configuration support is available.
 * @return true if libyaml is properly linked and functional
 */
bool config_yaml_is_available(void);

/**
 * @brief Maps YAML configuration to legacy Channel array format.
 * @param config The YAML configuration to map from
 * @param channels Array of Channel structures to populate (must have NUM_CHANNELS elements)
 * @return true on successful mapping, false on failure
 */
bool config_yaml_map_to_channels(const YAMLAppConfig* config, Channel* channels);

#endif // CONFIG_YAML_H