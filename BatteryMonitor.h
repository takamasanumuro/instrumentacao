#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>
#include <time.h>
#include "Measurement.h"
#include "ConfigYAML.h"

// Structure to hold the state of the battery
typedef struct {
    bool enabled;
    double state_of_charge_percent; // SoC from 0.0 to 100.0
    double capacity_Ah;             // Total capacity in Ampere-hours
    int current_measurement_index;  // Which measurement index corresponds to battery current
    struct timespec last_update_time; // Last time the state was updated
    struct timespec last_save_time;   // Time of the last persistent save
} BatteryState;

// Initializes the battery monitor using environment variables. Returns true if enabled.
bool battery_monitor_init(BatteryState* state, const Channel* channels);

// Initializes the battery monitor using YAML configuration. Returns true if enabled.
bool battery_monitor_init_from_yaml(BatteryState* state, const Channel* channels, const YAMLAppConfig* config);

// Updates the State of Charge based on the current measurement and time delta.
void battery_monitor_update(BatteryState* state, const Channel* channels);

// Saves the current SoC to a file for persistence.
void battery_monitor_save_state(const BatteryState* state);

// Resets the SoC to 100% and saves it.
void battery_monitor_reset_soc(BatteryState* state);

#endif // BATTERY_MONITOR_H


