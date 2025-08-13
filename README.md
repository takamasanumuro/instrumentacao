This project provides a flexible data acquisition application for reading sensor data from an ADS1115 ADC, performing linear calibration, and sending the results to an InfluxDB time-series database.

## Dependencies

Before building, ensure you have the following installed on your system (e.g., Debian/Ubuntu):

- `cmake`: For building the project.
- `build-essential`: For the GCC compiler and make.
- `libcurl4-openssl-dev`: For sending data over HTTP to InfluxDB.
- `git`: For cloning the repository.

You can install them with:
sudo apt-get update
sudo apt-get install cmake build-essential libcurl4-openssl-dev git


## Building the Application

1.  Create a build directory and navigate into it:
    ```bash
    mkdir build
    cd build
    ```

2.  Run CMake to configure the project and then Make to compile it:
    ```bash
    cmake ..
    make
    ```
    This will create the `instrumentation-app` executable inside the `build` directory.

## Running the Application

The application requires root privileges to access the I2C bus.

### **Security Note**
For security, the InfluxDB API token is **not** hardcoded. You must provide it via an environment variable.

### **Execution**

1.  **Export the InfluxDB Token:**
    ```bash
    export INFLUXDB_TOKEN="your-very-long-and-secret-influxdb-api-token"
    ```

2.  **Run the application directly (as root):**
    The program takes the I2C bus, I2C device address (in hex), and the configuration file as arguments.
    ```bash
    # Example for board 'A' on I2C bus 1 with device at 0x48
    sudo ./build/instrumentation-app /dev/i2c-1 0x48 configA
    ```

3.  **Run using the Python wrapper (Recommended for easier parsing):**
    The Python script runs the C application and parses its output into a more structured format.
    ```bash
    # You still need to export the token first!
    python3 instrumentation_runner.py /dev/i2c-1 0x48 configA
    ```

## On-the-fly Calibration

While the application is running, you can trigger a recalibration for any sensor without restarting the program.

1.  In the terminal where the app is running, type `CAL` followed by the channel number (0-3).
    -   To calibrate the sensor on channel A0, type: `CAL0` and press Enter.
    -   To calibrate the sensor on channel A2, type: `CAL2` and press Enter.

2.  Follow the on-screen prompts to provide the physical measurements corresponding to the ADC readings.

3.  The new calibration values (slope and offset) will be applied immediately and saved to a `calibrationA<index>.txt` file.

## Troubleshooting

1.  **"Error opening I2C bus: Permission denied"**:
    You must run the application with `sudo`.

2.  **"Error setting I2C slave address"**:
    -   Verify the I2C device is connected correctly.
    -   Run `sudo i2cdetect -y <bus_number>` (e.g., `sudo i2cdetect -y 1`) to see if the device is detected at the address you provided.
    -   On some boards (like Orange Pi), you may need to enable the I2C overlays in `/boot/orangepiEnv.txt` or a similar configuration file.

3.  **"INFLUXDB_TOKEN environment variable not set"**:
    You forgot to `export` the token before running the application.

4.  **Measurements are stuck or nonsensical**:
    -   Check the physical sensor connections.
    -   For current sensors, ensure the current flows in the direction of the arrow on the sensor.
    -   The sensor's output voltage might be saturating the ADC's input range. Check that the gain setting in the config file (e.g., `GAIN_4096MV`) is appropriate for the expected voltage level.
