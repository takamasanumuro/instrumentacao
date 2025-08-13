#ifndef DATA_PUBLISHER_H
#define DATA_PUBLISHER_H

#include "Channel.h"
#include "Sender.h"

typedef struct {
    double latitude;
    double longitude;
    double altitude;
    double speed;
} GPSData;

typedef struct DataPublisher DataPublisher;

// Create/destroy publisher
DataPublisher* data_publisher_create(SenderContext* sender_ctx);
void data_publisher_destroy(DataPublisher* publisher);

// Publish measurements to InfluxDB
bool data_publisher_publish(DataPublisher* publisher, 
                           const Channel channels[], 
                           const GPSData* gps_data);

#endif // DATA_PUBLISHER_H