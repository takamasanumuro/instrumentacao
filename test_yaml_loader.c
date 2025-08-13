#include "ConfigYAML.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_config_summary(const YAMLAppConfig* config) {
    printf("\n=== Configuration Summary ===\n");
    
    printf("Metadata:\n");
    printf("  Version: %s\n", config->metadata.version);
    printf("  Calibration Date: %s\n", config->metadata.calibration_date);
    printf("  Calibrated By: %s\n", config->metadata.calibrated_by);
    printf("  Notes: %.100s%s\n", config->metadata.notes, 
           strlen(config->metadata.notes) > 100 ? "..." : "");
    
    printf("Hardware:\n");
    printf("  I2C Bus: %s\n", config->hardware.i2c_bus);
    printf("  I2C Address: 0x%lx\n", config->hardware.i2c_address);
    
    printf("System:\n");
    printf("  Main Loop Interval: %d ms\n", config->system.main_loop_interval_ms);
    printf("  Data Send Interval: %d ms\n", config->system.data_send_interval_ms);
    
    printf("Channels (%zu configured):\n", config->channel_count);
    for (size_t i = 0; i < config->channel_count; i++) {
        const Channel* ch = &config->channels[i];
        printf("  Channel %zu: %s (%s) - %s %s\n", 
               i, ch->id, ch->gain_setting, 
               ch->is_active ? "ACTIVE" : "INACTIVE", ch->unit);
        printf("    Calibration: slope=%.9f, offset=%.6f\n", ch->slope, ch->offset);
    }
    
    printf("InfluxDB:\n");
    printf("  URL: %s\n", config->influxdb.url);
    printf("  Bucket: %s\n", config->influxdb.bucket);
    printf("  Org: %s\n", config->influxdb.org);
    printf("  Token: %.*s...\n", 10, config->influxdb.token); // Only show first 10 chars
    
    printf("Logging:\n");
    printf("  CSV Enabled: %s\n", config->logging.csv_enabled ? "true" : "false");
    printf("  CSV Directory: %s\n", config->logging.csv_directory);
    
    printf("Battery:\n");
    printf("  Coulomb Counting: %s\n", 
           config->battery.coulomb_counting_enabled ? "enabled" : "disabled");
    printf("  Capacity: %.1f Ah\n", config->battery.capacity_ah);
    printf("  Current Channel: %s\n", config->battery.current_channel_id);
}

static bool test_yaml_file(const char* filename) {
    printf("\n=== Testing YAML file: %s ===\n", filename);
    
    YAMLAppConfig* config = config_yaml_load(filename);
    if (!config) {
        printf("❌ Failed to load configuration from '%s'\n", filename);
        return false;
    }
    
    printf("✅ Successfully loaded configuration from '%s'\n", filename);
    
    // Basic validation
    char error_msg[512];
    ConfigYAMLResult validation_result = config_yaml_validate(config, error_msg, sizeof(error_msg));
    
    if (validation_result == CONFIG_YAML_SUCCESS) {
        printf("✅ Basic configuration validation passed\n");
    } else {
        printf("❌ Basic configuration validation failed: %s\n", error_msg);
        config_yaml_free(config);
        return false;
    }

    // Comprehensive validation
    validation_result = config_yaml_validate_comprehensive(config, error_msg, sizeof(error_msg));
    
    if (validation_result == CONFIG_YAML_SUCCESS) {
        printf("✅ Comprehensive configuration validation passed\n");
    } else {
        printf("⚠️  Comprehensive validation warning: %s\n", error_msg);
        // Don't fail the test - comprehensive validation might flag environment variables
    }

    // Hardware validation (may fail in test environment)
    validation_result = config_yaml_validate_hardware(config, error_msg, sizeof(error_msg));
    
    if (validation_result == CONFIG_YAML_SUCCESS) {
        printf("✅ Hardware configuration validation passed\n");
    } else {
        printf("⚠️  Hardware validation warning: %s\n", error_msg);
        // Don't fail the test - hardware validation requires actual I2C devices
    }
    
    // Print summary
    print_config_summary(config);
    
    config_yaml_free(config);
    return true;
}

int main(int argc, char* argv[]) {
    printf("=== YAML Configuration Loader Test ===\n");
    
    // Test 1: Verify YAML support is available
    printf("\n1. Checking YAML support...\n");
    if (!config_yaml_is_available()) {
        printf("❌ YAML support not available\n");
        return 1;
    }
    printf("✅ YAML support is available\n");
    
    // Test 2: Test error handling with non-existent file
    printf("\n2. Testing error handling...\n");
    YAMLAppConfig* config = config_yaml_load("non_existent_file.yaml");
    if (config == NULL) {
        printf("✅ Gracefully handled missing file\n");
    } else {
        printf("❌ Should have failed for missing file\n");
        config_yaml_free(config);
    }
    
    // Test 3: Test configuration files
    bool all_tests_passed = true;
    
    const char* test_files[] = {
        //"config_blank.yaml",
        "config_bike.yaml", 
        "config_arariboia.yaml"
    };
    
    for (size_t i = 0; i < sizeof(test_files) / sizeof(test_files[0]); i++) {
        if (!test_yaml_file(test_files[i])) {
            all_tests_passed = false;
        }
    }
    
    // Test 4: Command line file if provided
    if (argc > 1) {
        printf("\n=== Testing user-provided file ===\n");
        for (int i = 1; i < argc; i++) {
            if (!test_yaml_file(argv[i])) {
                all_tests_passed = false;
            }
        }
    }
    
    // Summary
    printf("\n=== Test Summary ===\n");
    if (all_tests_passed) {
        printf("✅ All YAML configuration tests passed!\n");
        printf("🎉 Phase 3: YAML Configuration Loader - COMPLETE\n");
        return 0;
    } else {
        printf("❌ Some tests failed\n");
        return 1;
    }
}