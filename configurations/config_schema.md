# YAML Configuration Schema Documentation

## Overview

This document describes the YAML configuration schema for the Instrumentation System. The schema provides comprehensive configuration management with validation, documentation, and environment variable support.

## Schema Structure

### metadata
**Purpose**: Configuration traceability and documentation
- `version`: Configuration schema version
- `calibration_date`: When sensors were last calibrated (ISO 8601 format)
- `calibrated_by`: Technician or engineer who performed calibration
- `description`: Brief system description
- `notes`: Detailed calibration notes (supports multi-line YAML)

### hardware  
**Purpose**: Physical hardware configuration
- `i2c_bus`: I2C bus device path (e.g., "/dev/i2c-1")
- `i2c_address`: ADS1115 I2C address (hex format: 0x48)

### system
**Purpose**: Core system timing parameters
- `main_loop_interval_ms`: Main loop delay in milliseconds
- `data_send_interval_ms`: Data transmission interval in milliseconds

### channels[]
**Purpose**: Sensor channel configuration array
Each channel contains:

#### Basic Properties
- `pin`: ADC pin identifier ("A0", "A1", "A2", "A3")
- `id`: Unique channel identifier (use "NC" for disabled channels)
- `description`: Human-readable channel description
- `unit`: Measurement unit ("A", "V", etc.)

#### calibration
- `slope`: Linear calibration slope (real_value = slope * adc + offset)
- `offset`: Linear calibration offset
- `r_squared`: Calibration quality metric (0.0-1.0)
- `calibration_points`: Number of calibration measurements taken
- `range_min`: Minimum calibrated range
- `range_max`: Maximum calibrated range

#### adc
- `gain`: ADS1115 gain setting ("GAIN_6144MV", "GAIN_4096MV", etc.)
- `filter_alpha`: EMA filter coefficient (0.0-1.0, lower = more filtering)

#### validation
- `min_value`: Absolute minimum valid measurement
- `max_value`: Absolute maximum valid measurement  
- `timeout_threshold_s`: Alert timeout in seconds (optional)

### influxdb
**Purpose**: InfluxDB time-series database configuration
- `url`: InfluxDB server URL (supports ${ENV_VAR} expansion)
- `bucket`: Database bucket name
- `org`: Organization name
- `token`: API token (use environment variables for security)
- `measurement`: InfluxDB measurement name
- `tags`: Static tags applied to all measurements
- `batch_size`: Points per batch transmission
- `flush_interval_ms`: Forced flush interval
- `retry_attempts`: Network retry attempts
- `retry_delay_ms`: Delay between retries

### logging
**Purpose**: CSV logging configuration
- `csv_enabled`: Enable/disable CSV logging
- `csv_directory`: Log file directory
- `csv_filename_format`: strftime format for log filenames
- `max_file_size_mb`: File size rotation limit
- `max_files`: Maximum number of log files to keep
- `sync_interval_s`: Disk sync interval

### battery
**Purpose**: Battery monitoring and coulomb counting
- `coulomb_counting_enabled`: Enable state-of-charge calculation
- `capacity_ah`: Battery capacity in amp-hours
- `current_channel_id`: Channel ID for current measurement
- `initial_soc_percent`: Initial state of charge
- `soc_save_interval_s`: SoC persistence interval
- `soc_file_path`: SoC state file location
- `low_voltage_threshold`: Low battery warning voltage
- `critical_voltage_threshold`: Critical battery alarm voltage

### gps
**Purpose**: GPS integration via gpsd
- `enabled`: Enable/disable GPS functionality
- `gpsd_host`: gpsd daemon host (usually "localhost")
- `gpsd_port`: gpsd daemon port (default 2947)
- `timeout_s`: Connection timeout
- `reconnect_interval_s`: Reconnection attempt interval
- `min_satellites`: Minimum satellites for valid fix
- `position_precision`: Decimal places for coordinates
- `speed_units`: Speed unit format ("kph", "mph", "mps")

### network
**Purpose**: Network services configuration
- `socket_server_enabled`: Enable TCP socket server
- `socket_port`: TCP server port
- `max_clients`: Maximum concurrent connections
- `offline_queue_enabled`: Enable offline data queuing
- `offline_queue_max_size_mb`: Maximum offline queue size

## Environment Variable Expansion

The configuration system supports `${VARIABLE_NAME}` syntax for environment variable expansion. This is primarily used for sensitive data like API tokens:

```yaml
influxdb:
  token: "${INFLUXDB_TOKEN}"  # Expands to environment variable value
```

## Configuration Templates

### config_blank.yaml
Template for new installations with all channels disabled (NC).

### config_bike.yaml  
Bike battery monitoring configuration with precision calibration.

### config_arariboia.yaml
Marine vessel configuration with high-current sensors and marine-specific settings.

## Validation Rules

1. **Channel IDs must be unique** (except "NC")
2. **Channel count cannot exceed NUM_CHANNELS** (currently 4)
3. **All required environment variables must be available**
4. **Numeric ranges must be logical** (min < max)
5. **File paths must be accessible**
6. **Network ports must be valid** (1-65535)

## Migration from Legacy Format

Legacy space-separated configuration files can be automatically converted:
- Pin names are mapped to channel pins
- Calibration values are preserved
- Default values are applied for new fields
- Comments are generated from calibration data

## Best Practices

1. **Always document calibration procedures** in the `notes` field
2. **Use environment variables** for sensitive data
3. **Set appropriate validation ranges** based on sensor capabilities
4. **Choose filter_alpha carefully**: lower values = more filtering, higher latency
5. **Test configurations** in a safe environment before deployment
6. **Keep backup copies** of working configurations
7. **Version control** configuration files for traceability