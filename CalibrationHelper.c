/*
Module to help to calibrate the sensors. It shall take ADC readings and ask the user for the corresponding current or voltage.
It will take 3 measurements to perform a linear regression and calculate the slope and the offset of the sensor.
*/

#include "CalibrationHelper.h"
#include <stdio.h>
#include <string.h>
#include <sys/select.h> // For select()
#include <unistd.h>     // For STDIN_FILENO
#include "ansi_colors.h"

// Helper function to perform least squares linear regression.
void least_squares(int n, const double x[], const double y[], double *m, double *b) {
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    
    for (int i = 0; i < n; i++) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
    }
    
    double denominator = (n * sum_x2 - sum_x * sum_x);
    if (denominator == 0) {
        // Avoid division by zero, which can happen if all x values are the same.
        *m = 0;
        *b = sum_y / n;
    } else {
        *m = (n * sum_xy - sum_x * sum_y) / denominator;
        *b = (sum_y - (*m) * sum_x) / n;
    }
}

// Clears the standard input buffer. Useful after a failed scanf.
void clear_stdin() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int calibrateSensor(int index, int adc_reading, double *slope, double *offset) {
    static int counter = 0;
    static int number_points = 0;
    // Use dynamic allocation for flexibility, though static is also fine for a known max.
    static double adc_readings[1024];
    static double physical_readings[1024];

    if (counter == 0) {
        printf("***********************\n");
        printf("Calibrating sensor at A%d\n", index);
        
        printf("Choose the number of points for calibration (3-1024): ");
        // Robust input: Check scanf return value to ensure an integer was read.
        if (scanf("%d", &number_points) != 1) {
            fprintf(stderr, "Invalid input. Please enter a number.\n");
            clear_stdin();
            return 0; // Indicate failure
        }
        clear_stdin(); // Consume the rest of the line

        if (number_points < 3 || number_points > 1024) {
            printf("At least 3 and at most 1024 measurements are needed.\n");
            number_points = 0;
            return 0; // Indicate failure
        }

        printf("Change the current or voltage for each measurement.\n");
        printf("***********************\n");
    }

    adc_readings[counter] = adc_reading;
    printf("Measurement %d/%d -> Current ADC reading: %d\n", counter + 1, number_points, adc_reading);
    printf("Please enter the corresponding physical reading:");
    
    // Robust input for the physical reading.
    if (scanf("%lf", &physical_readings[counter]) != 1) {
        fprintf(stderr, "Invalid input. Please enter a number.\n");
        clear_stdin();
        // Allow user to retry the current point
        return 0;
    }
    clear_stdin();

    if (counter < number_points - 1) {
        printf("Change the physical value for the next measurement and press Enter to continue...\n");
        getchar(); // Wait for user to press Enter
        counter++;
        return 0; // Calibration in progress
    }

    // All points collected, now calculate the calibration values.
    least_squares(number_points, adc_readings, physical_readings, slope, offset);
    printf("Calibration complete. Calculated values: slope = %lf, offset = %lf\n", *slope, *offset);

    // Save calibration data to a file.
    char filename[50];
    snprintf(filename, sizeof(filename), "./calibrationA%d.txt", index);
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        perror("Error opening calibration output file");
    } else {
        fprintf(file, "ADC vs Physical readings for sensor A%d\n", index);
        for (int i = 0; i < number_points; i++) {
            fprintf(file, "%d %lf\n", (int)adc_readings[i], physical_readings[i]);
        }
        fprintf(file, "\nSlope: %.9lf\nOffset: %.9lf\n", *slope, *offset);
        fclose(file);
        printf("Calibration data saved to %s\n", filename);
        sleep(3); // Give user time to read the message
    }

    // Reset static variables for the next calibration run.
    counter = 0;
    number_points = 0;
    return 1; // Indicate success
}

// This function runs in a separate thread, listening for user commands to start calibration.
void *calibrationListener(void *arg_ptr) {
    CalibrationThreadArgs* args = (CalibrationThreadArgs*) arg_ptr;
    char command[16];
    int local_sensor_index;

    printf(ANSI_COLOR_YELLOW "Input listener started. Type CAL<0-3> to calibrate or SOC_RESET to reset SoC.\n" ANSI_COLOR_RESET);

    // Loop until the main thread signals for shutdown.
    while (*(args->keep_running_ptr)) {
        fd_set fds;
        struct timeval tv;
        int retval;

        // Set up the file descriptor set.
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        // Set up the timeout. This makes select() non-blocking.
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        // Wait for input on stdin or until the timeout expires.
        retval = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);

        if (retval > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            if (fgets(command, sizeof(command), stdin) != NULL) {
                // Check for SoC reset command
                if (strncmp(command, "SOC_RESET", 9) == 0) {
                    *(args->reset_soc_flag_ptr) = true;
                    printf("SoC reset requested. The main loop will handle it.\n");
                } 
                // Check for calibration command
                else if (sscanf(command, "CAL%d", &local_sensor_index) == 1) {
                    if (local_sensor_index >= 0 && local_sensor_index < NUM_CHANNELS) {
                        pthread_mutex_lock(args->mutex);
                        *(args->sensor_index_ptr) = local_sensor_index;
                        pthread_mutex_unlock(args->mutex);
                        printf("Calibration requested for sensor A%d. The main loop will handle it.\n", local_sensor_index);
                    } else {
                        fprintf(stderr, "Invalid sensor index. Please use 0-%d.\n", NUM_CHANNELS - 1);
                    }
                }
            }
        }
    }
    printf("Input listener shutting down.\n");
    return NULL;
}
