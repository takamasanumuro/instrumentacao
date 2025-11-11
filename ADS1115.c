#include "ADS1115.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include "ansi_colors.h"
#include <time.h>

// --- Internal Constants ---

// RATE in SPS (samples per second)
#define RATE_8   0
#define RATE_16  1
#define RATE_32  2
#define RATE_64  3
#define RATE_128 4
#define RATE_250 5
#define RATE_475 6
#define RATE_860 7

// GAIN in mV, max expected voltage as input
#define GAIN_6144MV 0
#define GAIN_4096MV 1
#define GAIN_2048MV 2
#define GAIN_1024MV 3
#define GAIN_512MV  4
#define GAIN_256MV  5

// Multiplexer settings for single-ended inputs
#define AIN0 4
#define AIN1 5
#define AIN2 6
#define AIN3 7

// Register addresses
#define REG_CONV 0
#define REG_CONFIG 1

// --- Internal Helper Functions ---

// Converts a gain setting string to its corresponding integer code for the ADC.
// This function is 'static' because it's only used within this file.
static int gain_to_int(const char* gain_str) {
    if (strcmp(gain_str, "GAIN_6144MV") == 0) return GAIN_6144MV;
    if (strcmp(gain_str, "GAIN_4096MV") == 0) return GAIN_4096MV;
    if (strcmp(gain_str, "GAIN_2048MV") == 0) return GAIN_2048MV;
    if (strcmp(gain_str, "GAIN_1024MV") == 0) return GAIN_1024MV;
    if (strcmp(gain_str, "GAIN_512MV") == 0) return GAIN_512MV;
    if (strcmp(gain_str, "GAIN_256MV") == 0) return GAIN_256MV;
    return -1; // Invalid gain string
}

// Maps a channel number (0-3) to the ADS1115's internal multiplexer setting.
static uint8_t channel_to_mux(uint8_t channel) {
    switch (channel) {
        case 0: return AIN0;
        case 1: return AIN1;
        case 2: return AIN2;
        case 3: return AIN3;
        default: return AIN0; // Default to AIN0 for safety
    }
}

// --- Public API Functions ---

int ads1115_init(const char* i2c_bus_str, long i2c_address) {
    int i2c_handle = open(i2c_bus_str, O_RDWR);
    if (i2c_handle < 0) {
        perror("ADS1115: Error opening I2C bus");
        return -1;
    }

    if (ioctl(i2c_handle, I2C_SLAVE, i2c_address) < 0) {
        perror("ADS1115: Error setting I2C slave address");
        close(i2c_handle);
        return -1;
    }

    // Test if the device is actually present by attempting to read the config register
    uint8_t reg_addr = REG_CONFIG;
    if (write(i2c_handle, &reg_addr, 1) != 1) {
        fprintf(stderr, "ADS1115: Failed to write config register address - device not responding at 0x%lX\n", i2c_address);
        close(i2c_handle);
        return -1;
    }

    uint8_t config_data[2];
    if (read(i2c_handle, config_data, 2) != 2) {
        fprintf(stderr, "ADS1115: Failed to read config register - device not present at 0x%lX\n", i2c_address);
        close(i2c_handle);
        return -1;
    }

    // Verify we got a reasonable config register value (ADS1115 default is 0x8583)
    uint16_t config_reg = (config_data[0] << 8) | config_data[1];
    printf("ADS1115: Device detected at " ANSI_COLOR_YELLOW "0x%lX" ANSI_COLOR_RESET " on " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " (config: 0x%04X)\n", 
           i2c_address, i2c_bus_str, config_reg);
    
    return i2c_handle;
}

int ads1115_read(int i2c_handle, uint8_t channel, const char* gain_str, int16_t *conversion_result) {
    
    // --- ADS111x Register and Bitfield Definitions ---
    // Register Pointer Addresses
    #define ADS_REG_CONV_RESULT 0x00 // Conversion Register (Read-Only)
    #define ADS_REG_CONFIG      0x01 // Config Register (Read/Write)
    
    // Configuration Register MSB Settings (Config[15:8])
    #define ADS_OS_START_SINGLE_CONV 0x80 // Bit 15: Begin a single conversion (Write 1) 
    #define ADS_MODE_SINGLE_SHOT     0x01 // Bit 8: Power-down single-shot mode (Write 1) 
    #define ADS_OS_CONV_READY_MASK   0x80 // Bit 15: Device is not currently performing a conversion (Read 1)

    // Configuration Register LSB Settings (Config[7:0])
    #define ADS_COMP_DISABLE         0x03 // Bits [1:0]: Disable comparator

    int gain = gain_to_int(gain_str);
    if (gain == -1) {
        fprintf(stderr, "ADS1115: Invalid gain setting '%s' for channel %d\n", gain_str, channel);
        return -1;
    }

    uint8_t multiplexer = channel_to_mux(channel);

    // Calculated Config Register Values
    uint8_t config_msb = ADS_OS_START_SINGLE_CONV | (multiplexer << 4) | (gain << 1) | ADS_MODE_SINGLE_SHOT;
    uint8_t config_lsb = (RATE_860 << 5) | ADS_COMP_DISABLE;
    
    // --- Step 1: Write to the Config Register to start the conversion (Write Word Format) ---
    // Structure: [Pointer: Config_Reg] [Data_MSB: Config] [Data_LSB: Config]
    unsigned char write_config_cmd[3] = {
        ADS_REG_CONFIG, 
        config_msb, 
        config_lsb
    };

    if (write(i2c_handle, write_config_cmd, 3) != 3) {
        // Return -2: Error during I2C Master Write for Configuration
        perror("ADS1115: Config write error");
        return -2;
    }

    // --- Step 2: Wait for Conversion Ready by Polling the OS bit ---
    // First set register pointer to configuration register, then read MSB until OS bit is set
    unsigned char pointer_config_cmd[1] = {ADS_REG_CONFIG};
    if (write(i2c_handle, pointer_config_cmd, 1) != 1) {
        // Error setting Pointer to Config Register for polling
        return -2;
    }

    time_t start_time = time(NULL);
    unsigned char config_read_msb = 0;

    // Loop until OS bit (Bit 15, or MSB) is set, indicating conversion complete
    while (1) {
        // Safety timeout to prevent infinite loop
        if ((time(NULL) - start_time) > 3) { // Timeout after 3 seconds
            printf("ADS1115: Problem reading I2C. Check board address and connections!\n");
            // Timeout or problem reading I2C
            return -3; 
        }

        // Read the MSB of the Config register to check the OS bit
        if (read(i2c_handle, &config_read_msb, 1) != 1) {
            // Error reading Config MSB
            return -4;
        }

        if (config_read_msb & ADS_OS_CONV_READY_MASK) // Check if Bit 15 (0x80) is 1
        {
            break; // Conversion is complete!
        }
    }

    // --- Step 3: Read the Conversion Result Register
    // First write the register pointer to the Conversion Result Register, then read 2 bytes
    unsigned char pointer_conv_cmd[1] = {ADS_REG_CONV_RESULT};
    if (write(i2c_handle, pointer_conv_cmd, 1) != 1) {
        // Error setting Pointer to Conversion Register
        return -4; 
    }

    unsigned char conversion_result_MSB[2] = {0U, 0U};
    
    // Read 2 bytes (16 bits) in big-endian format (MSB first)
    if (read(i2c_handle, conversion_result_MSB, 2) != 2) {
        // Error reading Conversion result
        return -5; 
    }

    // Combine bytes into a 16-bit signed integer (Twos Complement)
    *conversion_result = (int16_t) (conversion_result_MSB[0] << 8) | conversion_result_MSB[1];
    
    return 0; // Success

}

int ads1115_read_with_retry(int i2c_handle, uint8_t channel, const char* gain_str, 
                           int16_t *conversion_result, int max_retries) {
    if (max_retries <= 0) {
        max_retries = 1; // At least one attempt
    }
    
    int last_error = 0;
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        int result = ads1115_read(i2c_handle, channel, gain_str, conversion_result);
        
        if (result == 0) {
            // Success - log retry count if this wasn't the first attempt
            if (attempt > 0) {
                printf("ADS1115: Channel %d read succeeded on attempt %d/%d\n", 
                       channel, attempt + 1, max_retries);
            }
            return 0;
        }
        
        last_error = result;
        
        // Log the retry attempt (except for the last one which will be logged as final failure)
        if (attempt < max_retries - 1) {
            fprintf(stderr, "ADS1115: Channel %d read failed (attempt %d/%d, error %d), retrying...\n", 
                   channel, attempt + 1, max_retries, result);
            
            // Exponential backoff: 1ms, 2ms, 4ms, 8ms, etc. (capped at 100ms)
            int backoff_ms = 1 << attempt;
            if (backoff_ms > 100) {
                backoff_ms = 100;
            }
            usleep(backoff_ms * 1000);
        }
    }
    
    // All retries failed
    fprintf(stderr, "ADS1115: Channel %d read failed after %d attempts (final error: %d)\n", 
           channel, max_retries, last_error);
    
    return last_error;
}

void ads1115_close(int i2c_handle) {
    if (i2c_handle >= 0) {
        close(i2c_handle);
    }
}
