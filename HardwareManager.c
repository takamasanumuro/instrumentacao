#include "HardwareManager.h"
#include "ADS1115.h"
#include "ConfigYAML.h"
#include <stdio.h>
#include <string.h>
#include <gps.h>
#include <stdint.h>
#include "math.h"
#include <stdlib.h>

// The internal structure of HardwareManager
struct HardwareManager {
    // Hardware interfaces
    struct gps_data_t gps_data;
    bool gps_connected;
    char i2c_bus_path[256];
    
    // GPS state management
    GPSData last_valid_gps;     // Last known valid GPS data
    bool has_valid_gps;         // Whether we ever received valid GPS data
    
    // Multi-board I2C management
    int board_handles[MAX_BOARDS];     // I2C handles for each board
    int board_addresses[MAX_BOARDS];   // I2C addresses for each board
    int active_board_count;
    
    // I2C retry configuration
    int i2c_max_retries;
    int i2c_base_delay_ms;
    
    // Channel management (up to 16 channels across 4 boards)
    Channel channels[MAX_TOTAL_CHANNELS];
    int channel_count;
    bool channels_initialized;
};

HardwareManager* hardware_manager_init(const char* i2c_bus_path, int* board_addresses, int board_count) {                        
    if (!i2c_bus_path || !board_addresses || board_count <= 0 || board_count > MAX_BOARDS) {
        return NULL;
    }

    // Initialize structure
    HardwareManager* hw_manager = calloc(1, sizeof(HardwareManager));
    if (!hw_manager) {
        fprintf(stderr, "Hardware: Failed to allocate memory for HardwareManager\n");
        return NULL;
    }

    strncpy(hw_manager->i2c_bus_path, i2c_bus_path, sizeof(hw_manager->i2c_bus_path) - 1);
    hw_manager->i2c_bus_path[sizeof(hw_manager->i2c_bus_path) - 1] = '\0';
    
    // Initialize GPS state
    hw_manager->has_valid_gps = false;
    hw_manager->last_valid_gps.latitude = NAN;
    hw_manager->last_valid_gps.longitude = NAN;
    hw_manager->last_valid_gps.altitude = NAN;
    hw_manager->last_valid_gps.speed = NAN;
    
    // Set default I2C retry parameters
    hw_manager->i2c_max_retries = 3;
    hw_manager->i2c_base_delay_ms = 1;

    // Initialize all board handles to invalid
    for (int i = 0; i < MAX_BOARDS; i++) {
        hw_manager->board_handles[i] = -1;
        hw_manager->board_addresses[i] = -1;
    }

    // Initialize I2C for each board
    hw_manager->active_board_count = 0;
    for (int i = 0; i < board_count; i++) {
        int board_handle = ads1115_init(i2c_bus_path, board_addresses[i]);
        if (board_handle < 0) {
            fprintf(stderr, "Hardware: Failed to initialize board at address 0x%02x\n", 
                    board_addresses[i]);
            continue; // Skip failed boards but continue with others
        }
        
        hw_manager->board_handles[hw_manager->active_board_count] = board_handle;
        hw_manager->board_addresses[hw_manager->active_board_count] = board_addresses[i];
        hw_manager->active_board_count++;
        
        printf("Hardware: Board %d initialized at address 0x%02x\n", 
               hw_manager->active_board_count, board_addresses[i]);
    }

    if (hw_manager->active_board_count == 0) {
        fprintf(stderr, "Hardware: No boards successfully initialized\n");
        free(hw_manager);
        return NULL;
    }

    printf("Hardware: %d/%d boards initialized successfully on %s\n", 
           hw_manager->active_board_count, board_count, i2c_bus_path);

    // Initialize GPS
    if (gps_open("localhost", "2947", &hw_manager->gps_data) != 0) {
        fprintf(stderr, "Hardware: Could not connect to gpsd (continuing without GPS)\n");
        hw_manager->gps_connected = false;
    } else {
        if (gps_stream(&hw_manager->gps_data, WATCH_ENABLE | WATCH_JSON, NULL) < 0) {
            fprintf(stderr, "Hardware: Failed to start GPS streaming\n");
            gps_close(&hw_manager->gps_data);
            hw_manager->gps_connected = false;
        } else {
            hw_manager->gps_connected = true;
            printf("Hardware: GPS connected successfully\n");
        }
    }

    return hw_manager;
}

HardwareManager* hardware_manager_init_from_yaml(const YAMLAppConfig* config) {                                 
    if (!config) return NULL;
    if (config->hardware.board_count <= 0) return NULL;

    // Extract board addresses from YAML configuration
    int board_addresses[MAX_BOARDS];
    int count = config->hardware.board_count < MAX_BOARDS ? 
                config->hardware.board_count : MAX_BOARDS;
                
    for (int i = 0; i < count; i++) {
        board_addresses[i] = config->hardware.boards[i].address;
    }
    
    return hardware_manager_init(config->hardware.i2c_bus, board_addresses, count);                                                   
}

void hardware_manager_cleanup(HardwareManager* hw_manager) {
    if (!hw_manager) return;

    printf("Hardware: Cleaning up resources...\n");

    // Close all board I2C handles
    for (int i = 0; i < hw_manager->active_board_count; i++) {
        if (hw_manager->board_handles[i] < 0) continue;
        
        ads1115_close(hw_manager->board_handles[i]);
        printf("Hardware: Board at 0x%02x closed\n", hw_manager->board_addresses[i]);
        hw_manager->board_handles[i] = -1;
    }
    hw_manager->active_board_count = 0;
    printf("Hardware: All I2C boards closed\n");

    // Cleanup GPS
    if (hw_manager->gps_connected) {
        gps_stream(&hw_manager->gps_data, WATCH_DISABLE, NULL);
        gps_close(&hw_manager->gps_data);
        hw_manager->gps_connected = false;
        printf("Hardware: GPS disconnected\n");
    }
    
    free(hw_manager);
}

bool hardware_manager_init_channels(HardwareManager* hw_manager, const YAMLAppConfig* config) {
    if (!hw_manager || !config) {
        return false;
    }

    if (hw_manager->channels_initialized) {
        printf("Hardware: Channels already initialized\n");
        return true;
    }

    // Initialize all channels with defaults first
    for (int i = 0; i < MAX_TOTAL_CHANNELS; i++) {
        channel_init(&hw_manager->channels[i]);
    }

    // Map YAML configuration to channels
    if (!config_yaml_map_to_channels(config, hw_manager->channels)) {
        fprintf(stderr, "Hardware: Failed to map YAML config to channels\n");
        return false;
    }

    hw_manager->channel_count = config->channel_count < MAX_TOTAL_CHANNELS ? 
                               config->channel_count : MAX_TOTAL_CHANNELS;
    hw_manager->channels_initialized = true;

    printf("Hardware: Initialized %d channels from YAML configuration\n", hw_manager->channel_count);
    return true;
}

static int find_board_handle(HardwareManager* hw_manager, int board_address) {
    for (int i = 0; i < hw_manager->active_board_count; i++) {
        if (hw_manager->board_addresses[i] == board_address) {
            return hw_manager->board_handles[i];
        }
    }
    return -1; // Board not found
}

bool hardware_manager_collect_measurements(HardwareManager* hw_manager) {
    if (!hw_manager) return false;
    if (!hw_manager->channels_initialized) return false;
    if (hw_manager->active_board_count == 0) return false;

    bool all_success = true;

    for (int i = 0; i < hw_manager->channel_count; i++) {
        Channel* channel = &hw_manager->channels[i];
        
        if (!channel->is_active) continue;

        // Find the I2C handle for this channel's board
        int board_handle = find_board_handle(hw_manager, channel->board_address);
        if (board_handle < 0) {
            fprintf(stderr, "Hardware: Board 0x%02x not found for channel %s\n", 
                   channel->board_address, channel->id);
            all_success = false;
            continue;
        }

        int16_t raw_value;
        int result = ads1115_read_with_retry(board_handle, channel->pin, 
                                           channel->gain_setting, &raw_value, 
                                           hw_manager->i2c_max_retries);
        
        if (result == 0) {
            channel_update_raw_value(channel, (int)raw_value);
            // Apply filtering using channel's alpha value from YAML
            channel_apply_filter(channel, channel->filter_alpha);
        } else {
            fprintf(stderr, "Hardware: Failed to read channel %s (board 0x%02x, pin %d) after retries\n", 
                   channel->id, channel->board_address, channel->pin);
            all_success = false;
        }
    }

    return all_success;
}

const Channel* hardware_manager_get_channels(const HardwareManager* hw_manager) {
    if (!hw_manager || !hw_manager->channels_initialized) {
        return NULL;
    }
    return hw_manager->channels;
}

const Channel* hardware_manager_get_channel(const HardwareManager* hw_manager, int index) {
    if (!hw_manager || !hw_manager->channels_initialized || 
        index < 0 || index >= hw_manager->channel_count) {
        return NULL;
    }
    return &hw_manager->channels[index];
}

int hardware_manager_get_channel_count(const HardwareManager* hw_manager) {
    if (!hw_manager || !hw_manager->channels_initialized) {
        return 0;
    }
    return hw_manager->channel_count;
}

bool hardware_manager_update_channel_calibration(HardwareManager* hw_manager, 
                                                int index, double slope, double offset) {
    if (!hw_manager || !hw_manager->channels_initialized || 
        index < 0 || index >= hw_manager->channel_count) {
        return false;
    }

    hw_manager->channels[index].slope = slope;
    hw_manager->channels[index].offset = offset;
    
    printf("Hardware: Updated calibration for channel %s: slope=%.6f, offset=%.6f\n",
           hw_manager->channels[index].id, slope, offset);
    return true;
}

bool hardware_manager_get_current_gps(HardwareManager* hw_manager, GPSData* gps_data) {
    if (!hw_manager || !gps_data) {
        return false;
    }

    if (!hw_manager->gps_connected) {
        // Return last valid GPS data if available, even when not connected
        if (hw_manager->has_valid_gps) {
            *gps_data = hw_manager->last_valid_gps;
            return true;
        }
        return false;
    }

    // Check for new GPS data
    if (gps_waiting(&hw_manager->gps_data, 1000)) {  // 1 millisecond timeout
        if (gps_read(&hw_manager->gps_data, NULL, 0) == -1) {
            fprintf(stderr, "Hardware: GPS read error\n");
            // Return last valid GPS data on read error
            if (hw_manager->has_valid_gps) {
                *gps_data = hw_manager->last_valid_gps;
                return true;
            }
            return false;
        }

        if (MODE_SET != (MODE_SET & hw_manager->gps_data.set)) {
            // Did not get GPS mode information yet - return last valid data
            if (hw_manager->has_valid_gps) {
                *gps_data = hw_manager->last_valid_gps;
                return true;
            }
            return false;
        }

        // Uninitialized GPS double values are set to NAN or INF, so we need to check for valid doubles
        if (isfinite(hw_manager->gps_data.fix.latitude) &&
            isfinite(hw_manager->gps_data.fix.longitude) &&
            isfinite(hw_manager->gps_data.fix.altHAE) &&
            isfinite(hw_manager->gps_data.fix.speed)) {
            
            // Store new valid GPS data
            hw_manager->last_valid_gps.latitude = hw_manager->gps_data.fix.latitude;
            hw_manager->last_valid_gps.longitude = hw_manager->gps_data.fix.longitude;
            hw_manager->last_valid_gps.altitude = hw_manager->gps_data.fix.altHAE;
            hw_manager->last_valid_gps.speed = hw_manager->gps_data.fix.speed;
            hw_manager->has_valid_gps = true;
            
            // Return the new valid data
            *gps_data = hw_manager->last_valid_gps;
            return true;
        }
    }

    // No new data available - return last valid GPS data if we have it
    if (hw_manager->has_valid_gps) {
        *gps_data = hw_manager->last_valid_gps;
        return true;
    }

    return false;
}

void hardware_manager_set_i2c_retry_params(HardwareManager* hw_manager, int max_retries, int base_delay_ms) {
    if (!hw_manager) return;
    
    hw_manager->i2c_max_retries = (max_retries > 0) ? max_retries : 3;
    hw_manager->i2c_base_delay_ms = (base_delay_ms > 0) ? base_delay_ms : 1;
    
    printf("Hardware: I2C retry configured - max_retries=%d, base_delay=%dms\n", 
           hw_manager->i2c_max_retries, hw_manager->i2c_base_delay_ms);
}

bool hardware_manager_is_gps_available(const HardwareManager* hw_manager) {
    return hw_manager && hw_manager->gps_connected;
}