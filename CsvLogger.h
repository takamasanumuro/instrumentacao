#ifndef CSVLOGGER_H
#define CSVLOGGER_H

#include <stdio.h>
#include <stdbool.h>
#include "Channel.h"
#include "DataPublisher.h"
#include "ConfigYAML.h"

// A structure to hold the state of the CSV logger
typedef struct {
    FILE* file_handle;
    bool is_active;
} CsvLogger;

/**
 * @brief Initializes the CSV logger using environment variables.
 * * Checks for the 'CSV_LOGGING_ENABLE' environment variable. If it's set to '1' or 'true',
 * this function creates a new CSV file with a timestamped name in the 'logs' directory
 * and writes the header row.
 * * @param logger A pointer to the CsvLogger instance to initialize.
 * @param channels A pointer to the array of Channel to get the column names for the header.
 */
void csv_logger_init(CsvLogger* logger, const Channel* channels);

/**
 * @brief Initializes the CSV logger using YAML configuration.
 * * Uses the YAML configuration to determine if CSV logging is enabled and which directory to use.
 * Creates a new CSV file with a timestamped name and writes the header row.
 * * @param logger A pointer to the CsvLogger instance to initialize.
 * @param channels A pointer to the array of Channel to get the column names for the header.
 * @param config A pointer to the YAML configuration.
 */
void csv_logger_init_from_yaml(CsvLogger* logger, const Channel* channels, const YAMLAppConfig* config);

/**
 * @brief Logs a row of data to the CSV file.
 * * If the logger is active, this function writes the current timestamp, sensor measurements,
 * and GPS data as a new row in the CSV file.
 * * @param logger A pointer to the CsvLogger instance.
 * @param measurements A pointer to the array of current measurements.
 * @param gps_data A pointer to the current GPS data.
 */
void csv_logger_log(const CsvLogger* logger, const Channel* channels, const GPSData* gps_data);

/**
 * @brief Closes the CSV logger file.
 * * If the logger is active, this function will close the file handle.
 * * @param logger A pointer to the CsvLogger instance.
 */
void csv_logger_close(CsvLogger* logger);

#endif // CSVLOGGER_H
