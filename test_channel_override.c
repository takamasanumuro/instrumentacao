#include "Channel.h"
#include "CsvLogger.h"
#include <stdio.h>
#include <string.h>

static int fail(const char* message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void) {
    Channel channel;
    channel_init(&channel);

    channel_update_raw_value(&channel, 1234);
    channel_apply_filter(&channel, 0.5);
    if (channel_has_calibrated_override(&channel)) {
        return fail("override should be disabled after init");
    }

    double baseline = channel_get_calibrated_value(&channel);
    if (baseline <= 0.0) {
        return fail("baseline calibrated value should be positive");
    }

    channel_set_calibrated_override(&channel, 42.5);
    if (!channel_has_calibrated_override(&channel)) {
        return fail("override should be enabled");
    }

    if (channel_get_calibrated_value(&channel) != 42.5) {
        return fail("override value was not returned");
    }

    CsvLogger logger = {0};
    logger.file_handle = tmpfile();
    if (!logger.file_handle) {
        return fail("tmpfile failed");
    }
    logger.is_active = true;

    Channel channels[NUM_CHANNELS];
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channel_init(&channels[i]);
        channels[i].is_active = true;
        snprintf(channels[i].id, sizeof(channels[i].id), "CH%d", i);
    }
    channels[0] = channel;

    GPSData gps = {0};
    csv_logger_log(&logger, channels, &gps);

    rewind(logger.file_handle);
    char buffer[512];
    if (!fgets(buffer, sizeof(buffer), logger.file_handle)) {
        return fail("failed to read csv output");
    }

    if (strstr(buffer, ",1234,") != NULL) {
        return fail("synthetic channel should not print raw adc value");
    }

    if (strstr(buffer, ",,42.5000") == NULL) {
        return fail("synthetic channel should print empty raw field and overridden value");
    }

    fclose(logger.file_handle);
    return 0;
}