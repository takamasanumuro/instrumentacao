// example gpsd client
// compile this way:
// gcc example1.c -o example1 -lgps -lm
#include <gps.h>      // for gps_*()
#include <math.h>     // for isfinite()
#include <stdio.h>    // for printf(), puts()
#include <unistd.h>

#define MODE_STR_NUM 4
static const char *mode_str[MODE_STR_NUM] = {
    "n/a",
    "None",
    "2D",
    "3D"
};

int main(int argc, char *argv[])
{
    struct gps_data_t gps_data;

    // Connect to the gpsd daemon
    if (gps_open("localhost", "2947", &gps_data) != 0) {
        printf("Open error. Bye, bye\n");
        return 1;
    }

    (void)gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

    while (true) {
        if (gps_waiting(&gps_data, 1000)) {
            if (gps_read(&gps_data, NULL, 0) == -1) {
                printf("Read error. Bye, bye\n");
                break;
            }

            // Check if mode is set
            if (!(gps_data.set & MODE_SET)) {
                // did not even get mode, nothing to see here
                continue;
            }
            
            // Sanitize mode value
            if (gps_data.fix.mode < 0 ||
                gps_data.fix.mode >= MODE_STR_NUM) {
                gps_data.fix.mode = 0;
            }
            
            printf("Fix mode: %s (%d) Time: ",
                mode_str[gps_data.fix.mode],
                gps_data.fix.mode);

            // Check if time is set
            if ((gps_data.set & TIME_SET) != 0) {
                // not 32-bit safe
                printf("%ld.%09ld ", gps_data.fix.time.tv_sec,
                    gps_data.fix.time.tv_nsec);
            } else {
                puts("n/a ");
            }
            
            // Check for valid lat/lon
            if (isfinite(gps_data.fix.latitude) &&
                isfinite(gps_data.fix.longitude)) {
                // Display data from the GPS receiver if valid.
                printf("Lat %.6f Lon %.6f\n",
                    gps_data.fix.latitude, gps_data.fix.longitude);
            } else {
                printf("Lat n/a Lon n/a\n");
            }
        }
    }

    // When you are done...
    (void)gps_stream(&gps_data, WATCH_DISABLE, NULL);
    (void)gps_close(&gps_data);
    return 0;
}