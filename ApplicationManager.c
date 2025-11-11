#include "ApplicationManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <time.h>

// All required headers from the original main.c
#include "ADS1115.h"
#include "ansi_colors.h"
#include "BatteryMonitor.h"
#include "CalibrationHelper.h"
#include "ConfigYAML.h"
#include "CsvLogger.h"
#include "Channel.h"
#include "DisplayManager.h"
#include "Sender.h"
#include "SocketServer.h"
#include "util.h"
#include "DataPublisher.h"
#include "TimingUtils.h"
#include "HardwareManager.h"

// The internal structure of the ApplicationManager
struct ApplicationManager {
    volatile sig_atomic_t keep_running;
    char config_file_path[APP_CONFIG_FILE_PATH_MAX];
    YAMLAppConfig* yaml_config;

    BatteryState battery_state;
    SenderContext* sender_ctx;
    CsvLogger csv_logger;
    
    pthread_mutex_t cal_mutex;
    int cal_sensor_index;
    
    HardwareManager* hardware_manager;
    DataPublisher* data_publisher;
    DisplayManager* display_manager;
    IntervalTimer send_timer;
    time_t start_time;
};

// --- Private Function Prototypes ---
// print_measurements function removed - now using DisplayManager

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

    // Validate loaded YAML configuration
    char validation_error[512];
    ConfigYAMLResult validation_result = config_yaml_validate_comprehensive(app->yaml_config, validation_error, sizeof(validation_error));
    if (validation_result != CONFIG_YAML_SUCCESS) {
        fprintf(stderr, "YAML configuration validation failed: %s\n", validation_error);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        return APP_ERROR_CONFIG_LOAD_FAILED;
    }
    
    // Initialize display manager first
    app->display_manager = display_manager_init();
    if (!app->display_manager) {
        fprintf(stderr, "Display manager initialization failed\n");
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        return APP_ERROR_HARDWARE_INIT_FAILED;
    }
    
    // Set configuration name for display
    const char* config_filename = strrchr(app->config_file_path, '/');
    if (!config_filename) config_filename = strrchr(app->config_file_path, '\\');
    if (!config_filename) config_filename = app->config_file_path;
    else config_filename++; // Skip the slash
    display_manager_set_config_name(app->display_manager, config_filename);
    
    // Record start time
    app->start_time = time(NULL);
    
    // Initialize hardware using YAML configuration
    app->hardware_manager = hardware_manager_init_from_yaml(app->yaml_config);
    if (!app->hardware_manager) {
        display_manager_add_message(app->display_manager, MSG_ERROR, "Hardware manager initialization failed");
        display_manager_cleanup(app->display_manager);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        return APP_ERROR_HARDWARE_INIT_FAILED;
    }

    // Initialize channels in HardwareManager
    if (!hardware_manager_init_channels(app->hardware_manager, app->yaml_config)) {
        display_manager_add_message(app->display_manager, MSG_ERROR, "Failed to initialize channels in hardware manager");
        display_manager_cleanup(app->display_manager);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        hardware_manager_cleanup(app->hardware_manager);
        return APP_ERROR_CONFIG_LOAD_FAILED;
    }
    
    // Configure I2C retry parameters from YAML
    hardware_manager_set_i2c_retry_params(app->hardware_manager, 
                                         app->yaml_config->hardware.i2c_max_retries,
                                         app->yaml_config->hardware.i2c_retry_delay_ms);

    // Initialize mutex with error checking
    int mutex_result = pthread_mutex_init(&app->cal_mutex, NULL);
    if (mutex_result != 0) {
        fprintf(stderr, "Failed to initialize mutex: %d\n", mutex_result);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        hardware_manager_cleanup(app->hardware_manager);
        return APP_ERROR_MUTEX_INIT_FAILED;
    }
    
    // Initialize sender with YAML configuration
    app->sender_ctx = sender_create_from_yaml(app->yaml_config);
    if (!app->sender_ctx) {
        fprintf(stderr, "Sender initialization failed\n");
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(app->hardware_manager);
        return APP_ERROR_SENDER_INIT_FAILED;
    }
    
    
    // Component to publish data to the network
    app->data_publisher = data_publisher_create(app->sender_ctx);
    if (!app->data_publisher) {
        fprintf(stderr, "Failed to create Data Publisher.\n");
        sender_destroy(app->sender_ctx);
        config_yaml_free(app->yaml_config);
        app->yaml_config = NULL;
        pthread_mutex_destroy(&app->cal_mutex);
        hardware_manager_cleanup(app->hardware_manager);
        return APP_ERROR_PUBLISHER_INIT_FAILED;
    }

    // Initialize socket server
    SocketServerContext* socket_server = socket_server_create(app->hardware_manager, app->yaml_config);
    socket_server_start(socket_server);
    
    // Transmission interval for sending networked data
    double send_interval_s = app->yaml_config->system.data_send_interval_ms / 1000.0;
    interval_timer_init(&app->send_timer, send_interval_s);
    csv_logger_init_from_yaml(&app->csv_logger, hardware_manager_get_channels(app->hardware_manager), app->yaml_config);
    
    // Initialize battery monitor with YAML configuration
    battery_monitor_init_from_yaml(&app->battery_state, hardware_manager_get_channels(app->hardware_manager), app->yaml_config);

    display_manager_add_message(app->display_manager, MSG_INFO, "Application Manager initialized successfully with config: %s", config_filename);
    display_manager_add_message(app->display_manager, MSG_INFO, "Channels configured: %zu", app->yaml_config->channel_count);
    display_manager_add_message(app->display_manager, MSG_INFO, "Main loop interval: %d ms", app->yaml_config->system.main_loop_interval_ms);
    display_manager_add_message(app->display_manager, MSG_INFO, "Data send interval: %d ms", app->yaml_config->system.data_send_interval_ms);
    return APP_SUCCESS;
}

void app_manager_run(ApplicationManager* app) {
    if (!app) return;

    while (app->keep_running) {
        // Collect measurements via HardwareManager
        hardware_manager_collect_measurements(app->hardware_manager);
        
        if (interval_timer_should_trigger(&app->send_timer)) {
            // Get current data from hardware manager
            const Channel* channels = hardware_manager_get_channels(app->hardware_manager);
            GPSData gps_data;
            hardware_manager_get_current_gps(app->hardware_manager, &gps_data);
            
            data_publisher_publish(app->data_publisher, channels, &gps_data);
            interval_timer_mark_triggered(&app->send_timer);
        }
        
        // Log and display using hardware manager data
        const Channel* channels = hardware_manager_get_channels(app->hardware_manager);
        GPSData gps_data;
        hardware_manager_get_current_gps(app->hardware_manager, &gps_data);
        
        csv_logger_log(&app->csv_logger, channels, &gps_data);
        
        // Update display with measurements
        int channel_count = hardware_manager_get_channel_count(app->hardware_manager);
        display_manager_update_measurements(app->display_manager, channels, channel_count, &gps_data);
        
        // Update system status
        SystemStatus status = {
            .active_boards = app->yaml_config->hardware.board_count,
            .total_boards = app->yaml_config->hardware.board_count,
            .loop_frequency_hz = 1000.0 / app->yaml_config->system.main_loop_interval_ms,
            .send_frequency_hz = 1000.0 / app->yaml_config->system.data_send_interval_ms,
            .uptime_seconds = (int)(time(NULL) - app->start_time),
            .gps_connected = hardware_manager_is_gps_available(app->hardware_manager),
            .influxdb_connected = true  // Assume connected for now
        };
        display_manager_update_status(app->display_manager, &status);
        display_manager_refresh(app->display_manager);
        
        // usleep(app->yaml_config->system.main_loop_interval_ms * 1000);
    }
}

void app_manager_destroy(ApplicationManager* app) {
    if (!app) return;

    if (app->display_manager) {
        display_manager_add_message(app->display_manager, MSG_INFO, "Cleaning up resources...");
        display_manager_refresh(app->display_manager);
    }
    
    data_publisher_destroy(app->data_publisher);
    hardware_manager_cleanup(app->hardware_manager);
    sender_destroy(app->sender_ctx);
    csv_logger_close(&app->csv_logger);
    pthread_mutex_destroy(&app->cal_mutex);
    
    // Cleanup display manager last
    if (app->display_manager) {
        display_manager_cleanup(app->display_manager);
        app->display_manager = NULL;
    }
    
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
        case APP_ERROR_PUBLISHER_INIT_FAILED:
            return "Data publisher initialization failed";
        case APP_ERROR_MUTEX_INIT_FAILED:
            return "Mutex initialization failed";
        default:
            return "Unknown error";
    }
}

// --- Private Helper Functions ---

// print_measurements function removed - now using DisplayManager