#pragma once
#include "Measurement.h"
#include <stdbool.h>

// Load configuration from file and populate Channel array
// Returns true on success, false on failure
bool loadConfigurationFile(const char *filename, Channel *channels);
