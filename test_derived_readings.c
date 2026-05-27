#include "Channel.h"
#include <stdio.h>
#include <string.h>

static int fail(const char* message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void) {
    Channel source;
    Channel derived;

    channel_init(&source);
    channel_init(&derived);

    strncpy(source.id, "Pressao_Cilindro_Hidrogenio", sizeof(source.id) - 1);
    source.id[sizeof(source.id) - 1] = '\0';
    strncpy(source.unit, "bar", sizeof(source.unit) - 1);
    source.unit[sizeof(source.unit) - 1] = '\0';
    source.is_active = true;

    channel_update_raw_value(&source, 1800);
    channel_apply_filter(&source, 0.25);
    channel_set_calibrated_override(&source, 123.4);

    double source_value_before = channel_get_calibrated_value(&source);

    strncpy(derived.id, "Nivel_Cilindro_Hidrogenio", sizeof(derived.id) - 1);
    derived.id[sizeof(derived.id) - 1] = '\0';
    strncpy(derived.unit, "%", sizeof(derived.unit) - 1);
    derived.unit[sizeof(derived.unit) - 1] = '\0';
    strncpy(derived.derived_source_id, source.id, sizeof(derived.derived_source_id) - 1);
    derived.derived_source_id[sizeof(derived.derived_source_id) - 1] = '\0';
    derived.is_active = true;
    derived.is_derived = true;

    channel_set_calibrated_override(&derived, 61.5);

    if (!channel_has_calibrated_override(&source)) {
        return fail("source override should remain enabled");
    }

    if (!channel_has_calibrated_override(&derived)) {
        return fail("derived override should be enabled");
    }

    if (channel_get_calibrated_value(&source) != source_value_before) {
        return fail("source calibrated value changed after derived setup");
    }

    if (channel_get_calibrated_value(&derived) != 61.5) {
        return fail("derived calibrated value was not preserved");
    }

    if (strcmp(derived.derived_source_id, source.id) != 0) {
        return fail("derived source id was not stored correctly");
    }

    return 0;
}