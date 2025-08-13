#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>

// Function to initialize the I2C bus and connect to the ADS1115.
// It takes the I2C bus device string (e.g., "/dev/i2c-1") and the
// 7-bit I2C address of the device.
// Returns a file descriptor on success, or -1 on error.
int ads1115_init(const char* i2c_bus_str, long i2c_address);

// Function to read a single conversion from a specified channel (0-3).
// It requires the file handle from ads1115_init(), the channel number,
// the gain setting as a string (e.g., "GAIN_4096MV"), and a pointer
// to store the 16-bit conversion result.
// Returns 0 on success, or a negative value on error.
int ads1115_read(int i2c_handle, uint8_t channel, const char* gain_str, int16_t *conversionResult);

// Function to close the I2C device handle.
void ads1115_close(int i2c_handle);

#endif
