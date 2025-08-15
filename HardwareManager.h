#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <stdbool.h>
#include "ConfigYAML.h"
#include "Channel.h"

// Simpler GPS data structure for application use
// Must be checked with isfinite() before each use
typedef struct {
    double latitude;
    double longitude;
    double altitude;
    double speed;
} GPSData;

// Opaque HardwareManager structure
typedef struct HardwareManager HardwareManager;

// Initialize hardware subsystems using parameters
HardwareManager* hardware_manager_init(const char* i2c_bus_path, long i2c_address);
                          
// Initialize hardware subsystems using YAML configuration
HardwareManager* hardware_manager_init_from_yaml(const YAMLAppConfig* config);                        

// Initialize channels from YAML configuration
bool hardware_manager_init_channels(HardwareManager* hw_manager, const YAMLAppConfig* config);

// Set I2C retry parameters from configuration
void hardware_manager_set_i2c_retry_params(HardwareManager* hw_manager, int max_retries, int base_delay_ms);

// Cleanup hardware resources
void hardware_manager_cleanup(HardwareManager* hw_manager);

// === Channel Management Interface ===
// Collect measurements from all active channels
bool hardware_manager_collect_measurements(HardwareManager* hw_manager);

// Get channel data (read-only access)
const Channel* hardware_manager_get_channels(const HardwareManager* hw_manager);
const Channel* hardware_manager_get_channel(const HardwareManager* hw_manager, int index);
int hardware_manager_get_channel_count(const HardwareManager* hw_manager);

// Update channel calibration
bool hardware_manager_update_channel_calibration(HardwareManager* hw_manager, int index, double slope, double offset);

// === GPS Data Interface ===
// Get current GPS data (on-demand)
bool hardware_manager_get_current_gps(HardwareManager* hw_manager, GPSData* gps_data);
bool hardware_manager_is_gps_available(const HardwareManager* hw_manager);

#endif // HARDWARE_MANAGER_H