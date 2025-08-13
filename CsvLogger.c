#include "CsvLogger.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h> // For mkdir
#include <math.h>     // For isfinite

void csv_logger_init(CsvLogger* logger, const Channel* channels) {
    logger->file_handle = NULL;
    logger->is_active = false;

    const char* log_env = getenv("CSV_LOGGING_ENABLE");
    if (log_env && (strcmp(log_env, "1") == 0 || strcmp(log_env, "true") == 0)) {
        // Create logs directory if it doesn't exist
        mkdir("logs", 0755); 

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char filename[128];
        // Format: logs/log_YYYY-MM-DD_HH-MM-SS.csv
        strftime(filename, sizeof(filename), "logs/log_%Y-%m-%d_%H-%M-%S.csv", tm_info);

        logger->file_handle = fopen(filename, "w");
        if (logger->file_handle == NULL) {
            perror("Failed to open CSV log file");
            return;
        }

        logger->is_active = true;
        printf("CSV logging is ENABLED. Logging to file: %s\n", filename);

        // Write header
        fprintf(logger->file_handle, "timestamp_iso8601,epoch_seconds");
        for (int i = 0; i < NUM_CHANNELS; i++) {
            fprintf(logger->file_handle, ",%s_adc,%s_value", channels[i].id, channels[i].id);
        }
        fprintf(logger->file_handle, ",latitude,longitude,altitude,speed\n");
        fflush(logger->file_handle); // Ensure header is written immediately

    } else {
        printf("CSV logging is DISABLED. Set CSV_LOGGING_ENABLE=1 to enable.\n");
    }
}

void csv_logger_init_from_yaml(CsvLogger* logger, const Channel* channels, const YAMLAppConfig* config) {
    logger->file_handle = NULL;
    logger->is_active = false;

    if (!config) {
        printf("CSV logging is DISABLED. No YAML configuration provided.\n");
        return;
    }

    if (!config->logging.csv_enabled) {
        printf("CSV logging is DISABLED in YAML configuration.\n");
        return;
    }

    // Create the specified logs directory if it doesn't exist
    if (strlen(config->logging.csv_directory) > 0) {
        mkdir(config->logging.csv_directory, 0755);
    } else {
        printf("CSV logging is DISABLED. No CSV directory specified in YAML configuration.\n");
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[512];
    // Format: <csv_directory>/log_YYYY-MM-DD_HH-MM-SS.csv
    snprintf(filename, sizeof(filename), "%s/log_", config->logging.csv_directory);
    strftime(filename + strlen(filename), sizeof(filename) - strlen(filename), 
             "%Y-%m-%d_%H-%M-%S.csv", tm_info);

    logger->file_handle = fopen(filename, "w");
    if (logger->file_handle == NULL) {
        perror("Failed to open CSV log file");
        return;
    }

    logger->is_active = true;
    printf("CSV logging is ENABLED. Logging to file: %s\n", filename);

    // Write header
    fprintf(logger->file_handle, "timestamp_iso8601,epoch_seconds");
    for (int i = 0; i < NUM_CHANNELS; i++) {
        fprintf(logger->file_handle, ",%s_adc,%s_value", channels[i].id, channels[i].id);
    }
    fprintf(logger->file_handle, ",latitude,longitude,altitude,speed\n");
    fflush(logger->file_handle); // Ensure header is written immediately
}

void csv_logger_log(const CsvLogger* logger, const Channel* channels, const GPSData* gps_data) {
    if (!logger->is_active || logger->file_handle == NULL) {
        return;
    }

    time_t now = time(NULL);
    char time_buf[64];
    // ISO 8601 format
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S%z", localtime(&now));

    fprintf(logger->file_handle, "%s,%ld", time_buf, now);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        fprintf(logger->file_handle, ",%d,%.4f", channels[i].raw_adc_value, channel_get_calibrated_value(&channels[i]));
    }

    // Handle potentially unavailable GPS data
    if (gps_data && isfinite(gps_data->latitude)) {
        fprintf(logger->file_handle, ",%.6f", gps_data->latitude);
    } else {
        fprintf(logger->file_handle, ",");
    }
    if (gps_data && isfinite(gps_data->longitude)) {
        fprintf(logger->file_handle, ",%.6f", gps_data->longitude);
    } else {
        fprintf(logger->file_handle, ",");
    }
    if (gps_data && isfinite(gps_data->altitude)) {
        fprintf(logger->file_handle, ",%.2f", gps_data->altitude);
    } else {
        fprintf(logger->file_handle, ",");
    }
    if (gps_data && isfinite(gps_data->speed)) {
        fprintf(logger->file_handle, ",%.2f", gps_data->speed);
    } else {
        fprintf(logger->file_handle, ",");
    }

    fprintf(logger->file_handle, "\n");
    fflush(logger->file_handle); // Flush buffer to disk to prevent data loss on crash
}

void csv_logger_close(CsvLogger* logger) {
    if (logger->is_active && logger->file_handle != NULL) {
        fclose(logger->file_handle);
        logger->file_handle = NULL;
        logger->is_active = false;
        printf("CSV log file closed.\n");
    }
}
