#ifndef APPLICATION_MANAGER_H
#define APPLICATION_MANAGER_H

#include <stdbool.h>
#include "ConfigYAML.h"

// Application constants
#define APP_CONFIG_FILE_PATH_MAX 256

// Error codes for more detailed error reporting
typedef enum {
    APP_SUCCESS = 0,
    APP_ERROR_NULL_POINTER,
    APP_ERROR_MEMORY_ALLOCATION,
    APP_ERROR_INVALID_PARAMETER,
    APP_ERROR_HARDWARE_INIT_FAILED,
    APP_ERROR_CONFIG_LOAD_FAILED,
    APP_ERROR_SENDER_INIT_FAILED,
    APP_ERROR_COORDINATOR_INIT_FAILED,
    APP_ERROR_PUBLISHER_INIT_FAILED,
    APP_ERROR_MUTEX_INIT_FAILED
} AppManagerError;

// Opaque pointer to the application manager
typedef struct ApplicationManager ApplicationManager;

// YAML Configuration integration
typedef struct {
    YAMLAppConfig* yaml_config;
    char config_path[APP_CONFIG_FILE_PATH_MAX];
} AppConfigManager;

/**
 * @brief Creates a new ApplicationManager instance using YAML configuration.
 *
 * @param config_file The path to the YAML configuration file.
 * @return A pointer to the new ApplicationManager, or NULL on failure.
 */
ApplicationManager* app_manager_create(const char* config_file);

/**
 * @brief Initializes all components and subsystems of the application.
 *
 * @param app A pointer to the ApplicationManager instance.
 * @return APP_SUCCESS on successful initialization, or specific error code.
 */
AppManagerError app_manager_init(ApplicationManager* app);

/**
 * @brief Starts and runs the main application event loop.
 *
 * This function will block until the application is signaled to shut down.
 * @param app A pointer to the ApplicationManager instance.
 */
void app_manager_run(ApplicationManager* app);

/**
 * @brief Cleans up and destroys the ApplicationManager and all its resources.
 *
 * @param app A pointer to the ApplicationManager instance.
 */
void app_manager_destroy(ApplicationManager* app);

/**
 * @brief Handles termination signals (SIGINT, SIGTERM).
 *
 * This function is designed to be called from a signal handler in main.c
 * to gracefully shut down the application.
 * @param app A pointer to the ApplicationManager instance.
 */
void app_manager_signal_shutdown(ApplicationManager* app);

/**
 * @brief Converts an error code to a human-readable string.
 *
 * @param error The error code to convert.
 * @return A string description of the error.
 */
const char* app_manager_error_string(AppManagerError error);

#endif // APPLICATION_MANAGER_H
