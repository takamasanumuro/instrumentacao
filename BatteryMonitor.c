// --- File: BatteryMonitor.c ---

#include "BatteryMonitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ansi_colors.h"

#define SOC_STATE_FILE "logs/soc_state.dat"

// --- Private function to load the SoC from a file ---
static double load_soc_from_file() {
    FILE* file = fopen(SOC_STATE_FILE, "r");
    if (!file) {
        // File doesn't exist, so create it with the default value of 100%
        printf("SoC state file not found. Creating a new one with default 100%% SoC.\n");
        file = fopen(SOC_STATE_FILE, "w");
        if (file) {
            fprintf(file, "100.0\n");
            fclose(file);
        } else {
            // If we can't even create the file, there's a bigger problem (e.g., permissions)
            perror("Could not create SoC state file");
        }
        return 100.0; // Return the default value
    }

    // If the file exists, read from it
    double soc = 100.0;
    if (fscanf(file, "%lf", &soc) != 1) {
        soc = 100.0; // Default if file is corrupt or empty
    }
    fclose(file);

    // Clamp the loaded value just in case
    if (soc < 0.0) soc = 0.0;
    if (soc > 100.0) soc = 100.0;

    return soc;
}
bool battery_monitor_init(BatteryState* state, const Channel* channels) {
    const char* enable_env = getenv("COULOMB_COUNTING_ENABLE");
    if (!enable_env || (strcmp(enable_env, "1") != 0 && strcmp(enable_env, "true") != 0)) {
        state->enabled = false;
        printf("Coulomb counting is " ANSI_COLOR_YELLOW "DISABLED" ANSI_COLOR_RESET ". Set COULOMB_COUNTING_ENABLE=1 to enable.\n");
        return false;
    }
    
    const char* capacity_str = getenv("BATTERY_CAPACITY_AH");
    const char* current_id_str = getenv("BATTERY_CURRENT_ID");

    if (!capacity_str || !current_id_str) {
        fprintf(stderr, ANSI_COLOR_RED "Error: BATTERY_CAPACITY_AH and BATTERY_CURRENT_ID must be set for Coulomb counting.\n" ANSI_COLOR_RESET);
        state->enabled = false;
        return false;
    }

    state->enabled = true;
    state->capacity_Ah = atof(capacity_str);
    state->state_of_charge_percent = load_soc_from_file(); // Load persistent SoC
    state->current_measurement_index = -1;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (strcmp(channels[i].id, current_id_str) == 0) {
            state->current_measurement_index = i;
            break;
        }
    }

    if (state->current_measurement_index == -1) {
        fprintf(stderr, ANSI_COLOR_RED "Error: Battery current ID '%s' not found in configuration.\n" ANSI_COLOR_RESET, current_id_str);
        state->enabled = false;
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &state->last_update_time);
    clock_gettime(CLOCK_MONOTONIC, &state->last_save_time); // Initialize save timer
    printf("Coulomb counting is " ANSI_COLOR_GREEN "ENABLED" ANSI_COLOR_RESET " for '%s' with capacity %.2f Ah. Initial SoC: %.2f%%\n", 
           current_id_str, state->capacity_Ah, state->state_of_charge_percent);
    return true;
}

bool battery_monitor_init_from_yaml(BatteryState* state, const Channel* channels, const YAMLAppConfig* config) {
    if (!config) {
        fprintf(stderr, "NULL YAML configuration provided to battery_monitor_init_from_yaml\n");
        state->enabled = false;
        return false;
    }
    
    if (!config->battery.coulomb_counting_enabled) {
        state->enabled = false;
        printf("Coulomb counting is " ANSI_COLOR_YELLOW "DISABLED" ANSI_COLOR_RESET " in YAML configuration.\n");
        return false;
    }
    
    if (config->battery.capacity_ah <= 0.0) {
        fprintf(stderr, ANSI_COLOR_RED "Error: Invalid battery capacity in YAML configuration: %.2f\n" ANSI_COLOR_RESET, 
                config->battery.capacity_ah);
        state->enabled = false;
        return false;
    }
    
    if (strlen(config->battery.current_channel_id) == 0) {
        fprintf(stderr, ANSI_COLOR_RED "Error: Battery current channel ID not specified in YAML configuration.\n" ANSI_COLOR_RESET);
        state->enabled = false;
        return false;
    }

    state->enabled = true;
    state->capacity_Ah = config->battery.capacity_ah;
    state->state_of_charge_percent = load_soc_from_file(); // Load persistent SoC
    state->current_measurement_index = -1;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (strcmp(channels[i].id, config->battery.current_channel_id) == 0) {
            state->current_measurement_index = i;
            break;
        }
    }

    if (state->current_measurement_index == -1) {
        fprintf(stderr, ANSI_COLOR_RED "Error: Battery current ID '%s' not found in configuration.\n" ANSI_COLOR_RESET, 
                config->battery.current_channel_id);
        state->enabled = false;
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &state->last_update_time);
    clock_gettime(CLOCK_MONOTONIC, &state->last_save_time); // Initialize save timer
    printf("Coulomb counting is " ANSI_COLOR_GREEN "ENABLED" ANSI_COLOR_RESET " for '%s' with capacity %.2f Ah. Initial SoC: %.2f%%\n", 
           config->battery.current_channel_id, state->capacity_Ah, state->state_of_charge_percent);
    return true;
}

void battery_monitor_update(BatteryState* state, const Channel* channels) {
    if (!state->enabled) {
        return;
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    double time_diff_s = (current_time.tv_sec - state->last_update_time.tv_sec)
                       + (current_time.tv_nsec - state->last_update_time.tv_nsec) / 1e9;

    double current_A = channel_get_calibrated_value(&channels[state->current_measurement_index]);
    double charge_moved_Ah = (current_A * time_diff_s) / 3600.0;
    double soc_change_percent = (charge_moved_Ah / state->capacity_Ah) * 100.0;

    state->state_of_charge_percent -= soc_change_percent;

    if (state->state_of_charge_percent > 100.0) {
        state->state_of_charge_percent = 100.0;
    } else if (state->state_of_charge_percent < 0.0) {
        state->state_of_charge_percent = 0.0;
    }

    state->last_update_time = current_time;

    // Check if one second has passed since the last save
    double save_time_diff_s = (current_time.tv_sec - state->last_save_time.tv_sec)
                            + (current_time.tv_nsec - state->last_save_time.tv_nsec) / 1e9;

    if (save_time_diff_s >= 1.0) {
        battery_monitor_save_state(state);
        state->last_save_time = current_time;
    }
}

void battery_monitor_save_state(const BatteryState* state) {
    if (!state->enabled) {
        return;
    }
    FILE* file = fopen(SOC_STATE_FILE, "w");
    if (file) {
        fprintf(file, "%.4f\n", state->state_of_charge_percent);
        fclose(file);
        printf("Saved SoC: %.2f%%\n", state->state_of_charge_percent);
    } else {
        perror("Failed to save SoC state file");
    }
}

void battery_monitor_reset_soc(BatteryState* state) {
    if (!state->enabled) {
        return;
    }
    printf("Resetting SoC to 100%%.\n");
    state->state_of_charge_percent = 100.0;
    battery_monitor_save_state(state); // Persist the reset immediately
}
