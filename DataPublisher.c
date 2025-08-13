#include "DataPublisher.h"
#include "LineProtocol.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

struct DataPublisher {
    LineProtocolBuilder* lp_builder;
    SenderContext* sender_ctx;
};

DataPublisher* data_publisher_create(SenderContext* sender_ctx) {
    if (!sender_ctx) return NULL;
    
    DataPublisher* publisher = malloc(sizeof(DataPublisher));
    if (!publisher) return NULL;
    
    publisher->lp_builder = lp_builder_create_default();
    if (!publisher->lp_builder) {
        free(publisher);
        return NULL;
    }
    
    publisher->sender_ctx = sender_ctx;
    return publisher;
}

void data_publisher_destroy(DataPublisher* publisher) {
    if (!publisher) return;
    
    if (publisher->lp_builder) {
        lp_builder_destroy(publisher->lp_builder);
    }
    free(publisher);
}

static bool add_channel_fields(LineProtocolBuilder* builder, const Channel channels[]) {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (!channels[i].is_active) continue; 
        LineProtocolError error = lp_add_field_double(builder, 
            channels[i].id, 
            channel_get_calibrated_value(&channels[i]));
        if (error != LP_SUCCESS) {
            fprintf(stderr, "Error adding field for channel %d: %d\n", 
                    channels[i].id, error);
            return false;
        }
    }
    return true;
}

static void add_gps_fields(LineProtocolBuilder* builder, const GPSData* gps_data) {
    if (isfinite(gps_data->latitude)) {
        lp_add_field_double(builder, "latitude", gps_data->latitude);
    }
    if (isfinite(gps_data->longitude)) {
        lp_add_field_double(builder, "longitude", gps_data->longitude);
    }
    if (isfinite(gps_data->altitude)) {
        lp_add_field_double(builder, "altitude", gps_data->altitude);
    }
    if (isfinite(gps_data->speed)) {
        lp_add_field_double(builder, "speed", gps_data->speed);
    }
}

bool data_publisher_publish(DataPublisher* publisher, 
                           const Channel channels[], 
                           const GPSData* gps_data) {
    if (!publisher || !channels || !gps_data) return false;
    
    lp_builder_reset(publisher->lp_builder);
    
    // Set measurement and tags
    if (lp_set_measurement(publisher->lp_builder, "measurements") != LP_SUCCESS ||
        lp_add_tag(publisher->lp_builder, "source", "instrumentacao") != LP_SUCCESS) {
        return false;
    }
    
    // Add fields
    if (!add_channel_fields(publisher->lp_builder, channels)) {
        return false;
    }
    
    add_gps_fields(publisher->lp_builder, gps_data);
    
    // Set timestamp and send
    lp_set_timestamp_now(publisher->lp_builder);
    
    const char* lp_string = lp_view(publisher->lp_builder);
    if (!lp_string) return false;
    
    sender_submit(publisher->sender_ctx, lp_string);
    return true;
}