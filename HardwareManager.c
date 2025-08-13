#include "HardwareManager.h"
#include "ADS1115.h"
#include <stdio.h>
#include <string.h>

bool hardware_manager_init(HardwareManager* hw_manager, 
                          const char* i2c_bus_path, 
                          long i2c_address) {
    if (!hw_manager || !i2c_bus_path) {
        return false;
    }

    // Initialize structure
    memset(hw_manager, 0, sizeof(HardwareManager));
    hw_manager->i2c_handle = -1;
    hw_manager->gps_connected = false;
    hw_manager->i2c_address = i2c_address;
    strncpy(hw_manager->i2c_bus_path, i2c_bus_path, sizeof(hw_manager->i2c_bus_path) - 1);

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

    return true;
}

bool hardware_manager_init_from_yaml(HardwareManager* hw_manager, 
                                    const YAMLAppConfig* config) {
    if (!hw_manager || !config) {
        return false;
    }

    // Initialize using YAML configuration
    return hardware_manager_init(hw_manager, 
                                config->hardware.i2c_bus, 
                                config->hardware.i2c_address);
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

int hardware_manager_get_i2c_handle(const HardwareManager* hw_manager) {
    return hw_manager ? hw_manager->i2c_handle : -1;
}

struct gps_data_t* hardware_manager_get_gps_data(HardwareManager* hw_manager) {
    return (hw_manager && hw_manager->gps_connected) ? &hw_manager->gps_data : NULL;
}

bool hardware_manager_is_gps_connected(const HardwareManager* hw_manager) {
    return hw_manager ? hw_manager->gps_connected : false;
}