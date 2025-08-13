#include "ApplicationManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>

// All required headers from the original main.c
#include "ADS1115.h"
#include "ansi_colors.h"
#include "BatteryMonitor.h"
#include "CalibrationHelper.h"
#include "ConfigYAML.h"
#include "CsvLogger.h"
#include "Measurement.h"
#include "Sender.h"
#include "SocketServer.h"
#include "util.h"
#include "DataPublisher.h"
#include "MeasurementCoordinator.h"
#include "TimingUtils.h"
#include "HardwareManager.h"

// The internal structure of the ApplicationManager, formerly AppContext
struct ApplicationManager {
    volatile sig_atomic_t keep_running;
    char config_file_path[APP_CONFIG_FILE_PATH_MAX];
    YAMLAppConfig* yaml_config;

    Channel channels[NUM_CHANNELS];
    GPSData gps_measurements;
    BatteryState battery_state;
    SenderContext* sender_ctx;
    CsvLogger csv_logger;
    
    pthread_mutex_t cal_mutex;
    int cal_sensor_index;
    
    HardwareManager hardware_manager;
    MeasurementCoordinator measurement_coordinator;
    DataPublisher* data_publisher;
    IntervalTimer send_timer;
};

// --- Private Function Prototypes ---
static void print_current_measurements(const Channel channels[], const GPSData* gps_data);

// --- Public API Implementation ---

ApplicationManager* app_manager_create(const char* config_file) {
    if (!config_file) {
        fprintf(stderr, "Invalid config file parameter to app_manager_create\n");
        return NULL;
    }
    
    if (strlen(config_file) >= APP_CONFIG_FILE_PATH_MAX) {
        fprintf(stderr, "Config file path too long: %s\n", config_file);
        return NULL;
    }

    ApplicationManager* app = calloc(1, sizeof(ApplicationManager));
    if (!app) {
        perror("Failed to allocate memory for ApplicationManager");
        return NULL;
    }

    app->keep_running = true;
    app->yaml_config = NULL;
    
    // Safe string copying with guaranteed null termination
    strncpy(app->config_file_path, config_file, sizeof(app->config_file_path) - 1);
    app->config_file_path[sizeof(app->config_file_path) - 1] = '\0';

    return app;
}

AppManagerError app_manager_init(ApplicationManager* app) {
    if (!app) {
        return APP_ERROR_NULL_POINTER;
    }

    // Load YAML configuration first to get hardware settings
    app->yaml_config = config_yaml_load(app->config_file_path);
    if (!app->yaml_config) {
        fprintf(stderr, "YAML configuration file load failed: %s\n", app->config_file_path);
        return APP_ERROR_CONFIG_LOAD_FAILED;
    }
    
    // 1. Initialize hardware using YAML configuration
    if (!hardware_manager_init_from_yaml(&app->hardware_manager, app->yaml_config)) {
        fprintf(stderr, "Hardware manager initialization failed\n");
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        return APP_ERROR_HARDWARE_INIT_FAILED;
    }

    // 2. Initialize mutex with error checking
    int mutex_result = pthread_mutex_init(&app->cal_mutex, NULL);
    if (mutex_result != 0) {
        fprintf(stderr, "Failed to initialize mutex: %d\n", mutex_result);
        hardware_manager_cleanup(&app->hardware_manager);
        return APP_ERROR_MUTEX_INIT_FAILED;
    }
    
    // Validate configuration (YAML already loaded above)
    char validation_error[512];
    ConfigYAMLResult validation_result = config_yaml_validate_comprehensive(app->yaml_config, validation_error, sizeof(validation_error));
    if (validation_result != CONFIG_YAML_SUCCESS) {
        fprintf(stderr, "YAML configuration validation failed: %s\n", validation_error);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(&app->hardware_manager);
        return APP_ERROR_CONFIG_LOAD_FAILED;
    }
    
    // Map YAML configuration to Channel structures
    if (!config_yaml_map_to_channels(app->yaml_config, app->channels)) {
        fprintf(stderr, "Failed to map YAML configuration to channels\n");
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(&app->hardware_manager);
        return APP_ERROR_CONFIG_LOAD_FAILED;
    }
    
    // Initialize sender with YAML configuration
    app->sender_ctx = sender_create_from_yaml(app->yaml_config);
    if (!app->sender_ctx) {
        fprintf(stderr, "Sender initialization failed\n");
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(&app->hardware_manager);
        return APP_ERROR_SENDER_INIT_FAILED;
    }
    
    // Initialize high-level coordinators
    if (!measurement_coordinator_init(&app->measurement_coordinator,
                                     hardware_manager_get_i2c_handle(&app->hardware_manager),
                                     hardware_manager_get_gps_data(&app->hardware_manager),
                                     app->channels,
                                     &app->gps_measurements)) {
        fprintf(stderr, "Failed to initialize Measurement Coordinator.\n");
        sender_destroy(app->sender_ctx);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(&app->hardware_manager);
        return APP_ERROR_COORDINATOR_INIT_FAILED;
    }
    
    // Component to publish data to the network
    app->data_publisher = data_publisher_create(app->sender_ctx);
    if (!app->data_publisher) {
        fprintf(stderr, "Failed to create Data Publisher.\n");
        sender_destroy(app->sender_ctx);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(&app->hardware_manager);
        return APP_ERROR_PUBLISHER_INIT_FAILED;
    }
    
    // Transmission interval for sending networked data
    double send_interval_s = app->yaml_config->system.data_send_interval_ms / 1000.0;
    interval_timer_init(&app->send_timer, send_interval_s);
    csv_logger_init_from_yaml(&app->csv_logger, app->channels, app->yaml_config);
    
    // Initialize battery monitor with YAML configuration
    battery_monitor_init_from_yaml(&app->battery_state, app->channels, app->yaml_config);

    printf("Application Manager initialized successfully with YAML config: %s\n", app->config_file_path);
    printf("  - Hardware I2C: %s at 0x%02lx\n", app->yaml_config->hardware.i2c_bus, app->yaml_config->hardware.i2c_address);
    printf("  - Channels configured: %zu\n", app->yaml_config->channel_count);
    printf("  - Main loop interval: %d ms\n", app->yaml_config->system.main_loop_interval_ms);
    printf("  - Data send interval: %d ms\n", app->yaml_config->system.data_send_interval_ms);
    return APP_SUCCESS;
}

void app_manager_run(ApplicationManager* app) {
    if (!app) return;

    while (app->keep_running) {
        measurement_coordinator_collect(&app->measurement_coordinator);
        
        if (interval_timer_should_trigger(&app->send_timer)) {
            data_publisher_publish(app->data_publisher, app->channels, &app->gps_measurements);
            interval_timer_mark_triggered(&app->send_timer);
        }
        
        csv_logger_log(&app->csv_logger, app->channels, &app->gps_measurements);
        print_current_measurements(app->channels, &app->gps_measurements);
        
        usleep(app->yaml_config->system.main_loop_interval_ms * 1000);
    }
}

void app_manager_destroy(ApplicationManager* app) {
    if (!app) return;

    printf("\nCleaning up resources...\n");
    
    data_publisher_destroy(app->data_publisher);
    hardware_manager_cleanup(&app->hardware_manager);
    sender_destroy(app->sender_ctx);
    csv_logger_close(&app->csv_logger);
    pthread_mutex_destroy(&app->cal_mutex);
    
    // Clean up YAML configuration
    if (app->yaml_config) {
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
    }
    
    free(app);
}

void app_manager_signal_shutdown(ApplicationManager* app) {
    if (!app) return;
    const char msg[] = "\nTermination signal received. Shutting down...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    app->keep_running = false;
}

const char* app_manager_error_string(AppManagerError error) {
    switch (error) {
        case APP_SUCCESS:
            return "Success";
        case APP_ERROR_NULL_POINTER:
            return "Null pointer provided";
        case APP_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case APP_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        case APP_ERROR_HARDWARE_INIT_FAILED:
            return "Hardware initialization failed";
        case APP_ERROR_CONFIG_LOAD_FAILED:
            return "Configuration file load failed";
        case APP_ERROR_SENDER_INIT_FAILED:
            return "Sender initialization failed";
        case APP_ERROR_COORDINATOR_INIT_FAILED:
            return "Measurement coordinator initialization failed";
        case APP_ERROR_PUBLISHER_INIT_FAILED:
            return "Data publisher initialization failed";
        case APP_ERROR_MUTEX_INIT_FAILED:
            return "Mutex initialization failed";
        default:
            return "Unknown error";
    }
}

// --- Private Helper Functions ---

static void print_current_measurements(const Channel channels[], const GPSData* gps_data) {
    // Simple placeholder. A more advanced implementation would format this nicely.
    printf("--- Current Measurements ---\n");
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (channels[i].is_active) {
            printf("  Channel %d (%s): ADC=%d, Value=%.4f %s\n",
                   i,
                   channels[i].id,
                   channels[i].raw_adc_value,
                   channel_get_calibrated_value(&channels[i]),
                   channels[i].unit);
        }
    }
    if (!isnan(gps_data->latitude) && !isnan(gps_data->longitude)) {
        printf("  GPS: Lat=%.6f, Lon=%.6f, Speed=%.2f kph\n",
               gps_data->latitude,
               gps_data->longitude,
               gps_data->speed);
    } else {
        printf("  GPS: No valid data\n");
    }
    printf("--------------------------\n");
}
