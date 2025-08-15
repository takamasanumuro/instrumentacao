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
    int i2c_handle;
    char i2c_bus_path[256];
    long i2c_address;
    
    // I2C retry configuration
    int i2c_max_retries;
    int i2c_base_delay_ms;
    
    // Channel management
    Channel channels[NUM_CHANNELS];
    int channel_count;
    bool channels_initialized;
};

HardwareManager* hardware_manager_init(const char* i2c_bus_path, long i2c_address) {                        
    if (!i2c_bus_path) {
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
    
    // Set default I2C retry parameters
    hw_manager->i2c_max_retries = 3;
    hw_manager->i2c_base_delay_ms = 1;

    // Initialize I2C
    hw_manager->i2c_handle = ads1115_init(i2c_bus_path, i2c_address);
    if (hw_manager->i2c_handle < 0) {
        fprintf(stderr, "Hardware: Failed to initialize I2C bus %s at address 0x%lx\n", 
                i2c_bus_path, i2c_address);
        return false;
    }
    printf("Hardware: I2C initialized successfully on %s at 0x%lx\n", 
           i2c_bus_path, i2c_address);

    // Initialize GPS
    if (gps_open("localhost", "2947", &hw_manager->gps_data) != 0) {
        fprintf(stderr, "Hardware: Could not connect to gpsd (continuing without GPS)\n");
        hw_manager->gps_connected = false;
        // Don't fail initialization - GPS is optional for some use cases
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
    if (!config) {
        return NULL;
    }

    // Initialize using YAML configuration
    return hardware_manager_init(config->hardware.i2c_bus, config->hardware.i2c_address);                                                   
}

void hardware_manager_cleanup(HardwareManager* hw_manager) {
    if (!hw_manager) return;

    printf("Hardware: Cleaning up resources...\n");

    // Cleanup I2C
    if (hw_manager->i2c_handle >= 0) {
        ads1115_close(hw_manager->i2c_handle);
        hw_manager->i2c_handle = -1;
        printf("Hardware: I2C closed\n");
    }

    // Cleanup GPS
    if (hw_manager->gps_connected) {
        gps_stream(&hw_manager->gps_data, WATCH_DISABLE, NULL);
        gps_close(&hw_manager->gps_data);
        hw_manager->gps_connected = false;
        printf("Hardware: GPS disconnected\n");
    }
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
    for (int i = 0; i < NUM_CHANNELS; i++) {
        channel_init(&hw_manager->channels[i]);
    }

    // Map YAML configuration to channels
    if (!config_yaml_map_to_channels(config, hw_manager->channels)) {
        fprintf(stderr, "Hardware: Failed to map YAML config to channels\n");
        return false;
    }

    hw_manager->channel_count = config->channel_count < NUM_CHANNELS ? 
                               config->channel_count : NUM_CHANNELS;
    hw_manager->channels_initialized = true;

    printf("Hardware: Initialized %d channels from YAML configuration\n", hw_manager->channel_count);
    return true;
}

bool hardware_manager_collect_measurements(HardwareManager* hw_manager) {
    if (!hw_manager || !hw_manager->channels_initialized || hw_manager->i2c_handle < 0) {
        return false;
    }

    bool all_success = true;

    for (int i = 0; i < hw_manager->channel_count; i++) {
        Channel* channel = &hw_manager->channels[i];
        
        if (!channel->is_active) {
            continue;
        }

        int16_t raw_value;
        int result = ads1115_read_with_retry(hw_manager->i2c_handle, channel->pin, 
                                           channel->gain_setting, &raw_value, 
                                           hw_manager->i2c_max_retries);
        
        if (result == 0) {
            channel_update_raw_value(channel, (int)raw_value);
            // Apply filtering using channel's alpha value from YAML
            channel_apply_filter(channel, channel->filter_alpha);
        } else {
            fprintf(stderr, "Hardware: Failed to read from channel %s (pin %d) after retries\n", 
                   channel->id, channel->pin);
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
        return false;
    }

    // Check for new GPS data
    if (gps_waiting(&hw_manager->gps_data, 1000000)) {  // 1 second timeout
        if (gps_read(&hw_manager->gps_data, NULL, 0) == -1) {
            fprintf(stderr, "Hardware: GPS read error\n");
            return false;
        }

        if (MODE_SET != (MODE_SET & hw_manager->gps_data.set)) {
            // Did not get GPS mode information yet
            return false;
        }

        // Uninitialized GPS double values are set to NAN or INF, so we need to check for valid doubles
        if (isfinite(hw_manager->gps_data.fix.latitude) &&
            isfinite(hw_manager->gps_data.fix.longitude) &&
            isfinite(hw_manager->gps_data.fix.altHAE) &&
            isfinite(hw_manager->gps_data.fix.speed)) {
                gps_data->latitude = hw_manager->gps_data.fix.latitude;
                gps_data->longitude = hw_manager->gps_data.fix.longitude;
                gps_data->altitude = hw_manager->gps_data.fix.altHAE;
                gps_data->speed = hw_manager->gps_data.fix.speed;
            return true;
        }
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