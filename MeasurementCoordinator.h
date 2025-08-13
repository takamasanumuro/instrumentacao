#ifndef MEASUREMENT_COORDINATOR_H
#define MEASUREMENT_COORDINATOR_H

#include "Measurement.h"
#include "DataPublisher.h"
#include <gps.h>

typedef struct {
    int i2c_handle;
    struct gps_data_t* gps_data;
    Channel* channels;
    GPSData* gps_measurements;
    bool filter_enabled;
    double filter_alpha;
} MeasurementCoordinator;

// Initialize coordinator with system handles
bool measurement_coordinator_init(MeasurementCoordinator* coordinator,
                                 int i2c_handle,
                                 struct gps_data_t* gps_data,
                                 Channel* channels,
                                 GPSData* gps_measurements);

// Collect all measurements (ADC + GPS)
void measurement_coordinator_collect(MeasurementCoordinator* coordinator);

// Configure filtering
void measurement_coordinator_set_filter(MeasurementCoordinator* coordinator, 
                                       bool enabled, double alpha);

#endif // MEASUREMENT_COORDINATOR_H