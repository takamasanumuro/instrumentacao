#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ConfigurationLoader.h"

// Take measurements with multimeters and compare ADC vs Real Current to get a regression slope and offset for each sensor

bool loadConfigurationFile(const char *filename, Channel *channels) {
    if (!filename || !channels) {
        fprintf(stderr, "Error: Invalid parameters to loadConfigurationFile\n");
        return false;
    }

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening sensor configuration file: %s\n", filename);
        return false;
    }

    char line[256];
    int line_num = 0;
    int settings_count = 0;

    // Read the file line by line until EOF or until we have loaded settings for all channels.
    while (fgets(line, sizeof(line), file) && settings_count < NUM_CHANNELS) {
        line_num++;
        // Ignore lines that are comments, empty, or headers.
        if (line[0] == '#' || line[0] == '\n' || line[0] == 'P' || line[0] == 'G') {
            continue;
        }

        char pin_name[16]; // To consume the "A0", "A1", etc., part of the line.

        // Use sscanf to parse the line, which is safer than a single fscanf for the whole file.
        // This provides better error isolation for malformed lines.
        int items_scanned = sscanf(line, "%s %lf %lf %15s %31s %15s",
                                   pin_name,
                                   &channels[settings_count].slope,
                                   &channels[settings_count].offset,
                                   channels[settings_count].gain_setting,
                                   channels[settings_count].id,
                                   channels[settings_count].unit);

        if (items_scanned == 6) {
            // Initialize other Channel fields
            channels[settings_count].raw_adc_value = 0;
            channels[settings_count].filtered_adc_value = 0.0;
            channels[settings_count].is_active = false; // Will be set later based on ID
            
            // If all 6 items were parsed successfully, move to the next setting.
            settings_count++;
        } else {
            // Warn the user if a line in the config file is malformed.
            fprintf(stderr, "Warning: Could not parse line %d in config file '%s'. Scanned %d items.\n", line_num, filename, items_scanned);
        }
    }

    fclose(file);

    if (settings_count == 0) {
        fprintf(stderr, "Error: No valid configuration found in file '%s'\n", filename);
        return false;
    }

    if (settings_count < NUM_CHANNELS) {
        fprintf(stderr, "Warning: Config file '%s' contains settings for only %d out of %d channels.\n", filename, settings_count, NUM_CHANNELS);
    }

    return true;
}
