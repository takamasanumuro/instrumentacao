#include "Channel.h"
#include <string.h>
#include <stdio.h>

void channel_init(Channel* channel) {
    if (!channel) return;

    memset(channel, 0, sizeof(Channel));
    channel->slope = 1.0;
    channel->offset = 0.0;
    channel->filter_alpha = 0.1; // Default alpha value
    channel->has_calibrated_override = false;
    channel->calibrated_override_value = 0.0;
    channel->is_active = false;
    channel->board_address = 0; // Default board address
    channel->pin = -1; // Initialize to invalid pin
    strcpy(channel->id, "NC"); // Default to "Not Connected"
}

double channel_get_calibrated_value(const Channel* channel) {
    if (!channel) return 0.0;

    if (channel->has_calibrated_override) {
        return channel->calibrated_override_value;
    }

    // Use the filtered value if it has been calculated, otherwise use the raw value.
    double value_to_use = (channel->filtered_adc_value > 0) ? channel->filtered_adc_value : (double)channel->raw_adc_value;
    return value_to_use * channel->slope + channel->offset;
}

void channel_set_calibrated_override(Channel* channel, double calibrated_value) {
    if (!channel) return;

    channel->has_calibrated_override = true;
    channel->calibrated_override_value = calibrated_value;
}

void channel_clear_calibrated_override(Channel* channel) {
    if (!channel) return;

    channel->has_calibrated_override = false;
    channel->calibrated_override_value = 0.0;
}

bool channel_has_calibrated_override(const Channel* channel) {
    return channel && channel->has_calibrated_override;
}

void channel_update_raw_value(Channel* channel, int new_raw_value) {
    if (!channel) return;
    channel->raw_adc_value = new_raw_value;
}

void channel_apply_filter(Channel* channel, double alpha) {
    if (!channel) return;

    // If the filtered value is not initialized, start it with the raw value.
    if (channel->filtered_adc_value == 0) {
        channel->filtered_adc_value = (double)channel->raw_adc_value;
    } else {
        channel->filtered_adc_value = (channel->filtered_adc_value * (1.0 - alpha)) + ((double)channel->raw_adc_value * alpha);
    }
}