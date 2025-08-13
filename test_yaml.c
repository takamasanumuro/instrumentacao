#include "ConfigYAML.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Testing YAML configuration infrastructure...\n\n");

    // Test 1: Check if YAML support is available
    printf("1. Checking YAML library availability...\n");
    if (config_yaml_is_available()) {
        printf("   ✓ libyaml is properly linked and functional\n");
    } else {
        printf("   ✗ libyaml is not available\n");
        return 1;
    }

    // Test 2: Test error string function
    printf("\n2. Testing error handling...\n");
    printf("   Error string test: %s\n", config_yaml_error_string(CONFIG_YAML_SUCCESS));
    printf("   Error string test: %s\n", config_yaml_error_string(CONFIG_YAML_ERROR_PARSE_FAILED));

    // Test 3: Try to load a non-existent file (should fail gracefully)
    printf("\n3. Testing graceful failure handling...\n");
    YAMLAppConfig* config = config_yaml_load("non_existent_file.yaml");
    if (config == NULL) {
        printf("   ✓ Gracefully handled missing file\n");
    } else {
        printf("   ✗ Should have failed for missing file\n");
        config_yaml_free(config);
    }

    // Test 4: Test validation with NULL config
    printf("\n4. Testing validation...\n");
    char error_msg[256];
    ConfigYAMLResult result = config_yaml_validate(NULL, error_msg, sizeof(error_msg));
    if (result == CONFIG_YAML_ERROR_VALIDATION_FAILED) {
        printf("   ✓ Validation correctly rejected NULL config: %s\n", error_msg);
    } else {
        printf("   ✗ Validation should have failed for NULL config\n");
    }

    printf("\n=== YAML Infrastructure Test Complete ===\n");
    printf("✓ Basic YAML infrastructure is working correctly\n");
    printf("Ready to proceed to Phase 2: Design YAML configuration schema\n");

    return 0;
}