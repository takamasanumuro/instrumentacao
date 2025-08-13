#include "MeasurementCoordinator.h"
#include "ADS1115.h"
#include <math.h>

bool measurement_coordinator_init(MeasurementCoordinator* coordinator,
                                 int i2c_handle,
                                 struct gps_data_t* gps_data,
                                 Channel* channels,
                                 GPSData* gps_measurements) {
    if (!coordinator || !channels || !gps_measurements) return false;
    
    coordinator->i2c_handle = i2c_handle;
    coordinator->gps_data = gps_data;
    coordinator->channels = channels;
    coordinator->gps_measurements = gps_measurements;
    coordinator->filter_enabled = false;
    coordinator->filter_alpha = 0.1;
    
    return true;
}

static void collect_adc_measurements(MeasurementCoordinator* coordinator) {
    int16_t raw_val;
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (!coordinator->channels[i].is_active) continue;
        
        if (ads1115_read(coordinator->i2c_handle, i, 
                        coordinator->channels[i].gain_setting, &raw_val) == 0) {
            channel_update_raw_value(&coordinator->channels[i], raw_val);
            
            if (coordinator->filter_enabled) {
                channel_apply_filter(&coordinator->channels[i], 
                                   coordinator->filter_alpha);
            }
        }
    }
}

static void collect_gps_measurements(MeasurementCoordinator* coordinator) {
    // Reset to invalid state
    coordinator->gps_measurements->latitude = NAN;
    coordinator->gps_measurements->longitude = NAN;
    coordinator->gps_measurements->altitude = NAN;
    coordinator->gps_measurements->speed = NAN;

    if (gps_waiting(coordinator->gps_data, 500000)) { // 500ms timeout
        if (gps_read(coordinator->gps_data, NULL, 0) != -1) {
            if (isfinite(coordinator->gps_data->fix.latitude)) {
                coordinator->gps_measurements->latitude = coordinator->gps_data->fix.latitude;
            }
            if (isfinite(coordinator->gps_data->fix.longitude)) {
                coordinator->gps_measurements->longitude = coordinator->gps_data->fix.longitude;
            }
            if (isfinite(coordinator->gps_data->fix.altitude)) {
                coordinator->gps_measurements->altitude = coordinator->gps_data->fix.altitude;
            }
            if (isfinite(coordinator->gps_data->fix.speed)) {
                coordinator->gps_measurements->speed = coordinator->gps_data->fix.speed;
            }
        }
    }
}

void measurement_coordinator_collect(MeasurementCoordinator* coordinator) {
    if (!coordinator) return;
    
    collect_adc_measurements(coordinator);
    collect_gps_measurements(coordinator);
}

void measurement_coordinator_set_filter(MeasurementCoordinator* coordinator, 
                                       bool enabled, double alpha) {
    if (!coordinator) return;
    
    coordinator->filter_enabled = enabled;
    coordinator->filter_alpha = alpha;
}