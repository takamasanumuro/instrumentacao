#include "ApplicationManager.h"
#include "ConfigYAML.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    printf("=== YAML Configuration Integration Test ===\n\n");
    
    // Test 1: Verify YAML configuration loads correctly
    printf("1. Testing YAML configuration loading...\n");
    YAMLAppConfig* config = config_yaml_load("config_bike.yaml");
    if (!config) {
        printf("❌ Failed to load YAML configuration\n");
        return 1;
    }
    printf("✅ YAML configuration loaded successfully\n\n");
    
    // Test 2: Validate configuration
    printf("2. Testing configuration validation...\n");
    char error_msg[512];
    ConfigYAMLResult result = config_yaml_validate_comprehensive(config, error_msg, sizeof(error_msg));
    if (result != CONFIG_YAML_SUCCESS) {
        printf("❌ Configuration validation failed: %s\n", error_msg);
        config_yaml_free(config);
        return 1;
    }
    printf("✅ Configuration validation passed\n\n");
    
    // Test 3: Test ApplicationManager creation and initialization
    printf("3. Testing ApplicationManager integration...\n");
    ApplicationManager* app = app_manager_create(
        config->hardware.i2c_bus,
        config->hardware.i2c_address,
        "config_bike.yaml"
    );
    
    if (!app) {
        printf("❌ Failed to create ApplicationManager\n");
        config_yaml_free(config);
        return 1;
    }
    printf("✅ ApplicationManager created successfully\n");
    
    // Test 4: Initialize with YAML configuration
    printf("4. Testing ApplicationManager initialization...\n");
    AppManagerError init_result = app_manager_init(app);
    if (init_result != APP_SUCCESS) {
        printf("❌ ApplicationManager initialization failed: %s\n", 
               app_manager_error_string(init_result));
        app_manager_destroy(app);
        config_yaml_free(config);
        return 1;
    }
    printf("✅ ApplicationManager initialized successfully\n\n");
    
    printf("=== Integration Test Results ===\n");
    printf("✅ All integration tests passed!\n");
    printf("🎉 YAML configuration system is fully integrated\n\n");
    
    printf("Configuration Summary:\n");
    printf("  - Hardware: %s at 0x%02lx\n", config->hardware.i2c_bus, config->hardware.i2c_address);
    printf("  - Channels: %zu configured\n", config->channel_count);
    printf("  - InfluxDB: %s\n", config->influxdb.url);
    printf("  - CSV Logging: %s in %s\n", 
           config->logging.csv_enabled ? "enabled" : "disabled",
           config->logging.csv_directory);
    printf("  - Battery Monitoring: %s (%.2f Ah)\n",
           config->battery.coulomb_counting_enabled ? "enabled" : "disabled",
           config->battery.capacity_ah);
    
    // Clean up
    app_manager_destroy(app);
    config_yaml_free(config);
    
    printf("\n✅ Integration test completed successfully!\n");
    return 0;
}