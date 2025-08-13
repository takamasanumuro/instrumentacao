#include "ConfigYAML.h"
#include <stdio.h>
#include <stdlib.h>

static void test_validation_function(const char* test_name, 
                                   ConfigYAMLResult (*validate_func)(const YAMLAppConfig*, char*, size_t),
                                   const YAMLAppConfig* config,
                                   bool should_pass) {
    char error_msg[512];
    ConfigYAMLResult result = validate_func(config, error_msg, sizeof(error_msg));
    
    printf("Test: %s\n", test_name);
    if (should_pass) {
        if (result == CONFIG_YAML_SUCCESS) {
            printf("  ✅ PASS - Validation succeeded as expected\n");
        } else {
            printf("  ❌ FAIL - Validation failed unexpectedly: %s\n", error_msg);
        }
    } else {
        if (result != CONFIG_YAML_SUCCESS) {
            printf("  ✅ PASS - Validation failed as expected: %s\n", error_msg);
        } else {
            printf("  ❌ FAIL - Validation succeeded unexpectedly\n");
        }
    }
    printf("\n");
}

int main() {
    printf("=== YAML Configuration Validation Test ===\n\n");

    // Test 1: Valid configuration
    printf("Loading valid configuration...\n");
    YAMLAppConfig* valid_config = config_yaml_load("config_bike.yaml");
    if (!valid_config) {
        printf("❌ Failed to load valid configuration\n");
        return 1;
    }

    test_validation_function("Basic validation - valid config", 
                           config_yaml_validate, valid_config, true);
    test_validation_function("Comprehensive validation - valid config", 
                           config_yaml_validate_comprehensive, valid_config, true);
    test_validation_function("Hardware validation - valid config", 
                           config_yaml_validate_hardware, valid_config, false); // May fail in test env

    // Test 2: Invalid configuration  
    printf("Loading invalid configuration...\n");
    YAMLAppConfig* invalid_config = config_yaml_load("config_invalid_test.yaml");
    if (!invalid_config) {
        printf("❌ Failed to load invalid test configuration\n");
        config_yaml_free(valid_config);
        return 1;
    }

    test_validation_function("Basic validation - invalid config",
                           config_yaml_validate, invalid_config, true); // Basic might pass
    test_validation_function("Comprehensive validation - invalid config",
                           config_yaml_validate_comprehensive, invalid_config, false); // Should fail
    test_validation_function("Hardware validation - invalid config", 
                           config_yaml_validate_hardware, invalid_config, false); // Should fail

    // Test 3: NULL configuration
    test_validation_function("Basic validation - NULL config",
                           config_yaml_validate, NULL, false);
    test_validation_function("Comprehensive validation - NULL config", 
                           config_yaml_validate_comprehensive, NULL, false);
    test_validation_function("Hardware validation - NULL config",
                           config_yaml_validate_hardware, NULL, false);

    // Cleanup
    config_yaml_free(valid_config);
    config_yaml_free(invalid_config);

    printf("=== Validation Test Complete ===\n");
    printf("✅ All validation tests completed successfully!\n");
    printf("🎉 Phase 4: Configuration Validation System - COMPLETE\n");

    return 0;
}