#ifndef MEASUREMENT_H
#define MEASUREMENT_H
#include <stdbool.h>

#define MEASUREMENT_ID_SIZE 32
#define GAIN_SETTING_SIZE 16
#define UNIT_SIZE 16
#define NUM_CHANNELS 4

// This struct will hold ALL information about a single sensor channel.
typedef struct {
    // Configuration
    char id[MEASUREMENT_ID_SIZE];
    char unit[UNIT_SIZE];
    char gain_setting[GAIN_SETTING_SIZE];
    int pin;  // ADC pin number (A0, A1, A2, A3)

    // Calibration
    double slope;
    double offset;

    // Filtering
    double filter_alpha;  // EMA filter alpha value from YAML

    // Live Data
    int raw_adc_value;
    double filtered_adc_value;
    bool is_active;
} Channel;

// --- Public API ---

// Initializes a channel with default values
void channel_init(Channel* channel);

// Calculates the final calibrated value
double channel_get_calibrated_value(const Channel* channel);

// Updates the raw ADC value
void channel_update_raw_value(Channel* channel, int new_raw_value);

// Applies the EMA filter to the raw value
void channel_apply_filter(Channel* channel, double alpha);

#endif // MEASUREMENT_H