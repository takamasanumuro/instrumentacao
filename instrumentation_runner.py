import subprocess
import sys
from dataclasses import dataclass
from typing import Optional

@dataclass
class Measurement:
    """A simple data class to hold parsed measurement data."""
    channel: int
    adc: int
    value: float
    unit: str
    field_name: str

def parse_measurement_line(line: str) -> Optional[Measurement]:
    """
    Parses a single line of output from the C application.
    Expected format: "A{d}: {id} | ADC: {d} | Value: {f} {unit}"
    Example: "A0: corrente-bateria-bombordo | ADC:  7581 | Value:    -0.29 A"
    """
    try:
        # Split the line into major parts based on the '|' delimiter
        parts = [p.strip() for p in line.split('|')]
        if len(parts) != 3:
            return None

        # Part 1: Channel and ID
        channel_part, id_part = parts[0].split(':')
        channel = int(channel_part.strip()[1:]) # Get digit from "A0"
        field_name = id_part.strip()

        # Part 2: ADC value
        adc_val_str = parts[1].split(':')[1].strip()
        adc = int(adc_val_str)

        # Part 3: Physical value and unit
        value_part = parts[2].split(':')[1].strip()
        value_str, unit = value_part.split()
        value = float(value_str)

        return Measurement(channel, adc, value, unit, field_name)
    except (ValueError, IndexError) as e:
        # This will catch lines that don't match the expected format
        # print(f"Could not parse line: '{line.strip()}' due to {e}", file=sys.stderr)
        return None


def run_instrumentation(i2c_bus: str, i2c_addr: str, config_file: str):
    """
    Runs the C instrumentation app as a subprocess and prints parsed data.
    """
    command = ["./build/instrumentation-app", i2c_bus, i2c_addr, config_file]
    print(f"Starting subprocess with command: {' '.join(command)}")

    try:
        # Using Popen to get real-time output from the subprocess.
        # Merging stderr with stdout to simplify reading output.
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, # Redirect stderr to stdout
            text=True,
            encoding='utf-8',
            bufsize=1  # Line-buffered
        )

        # Read and process output line by line in real-time.
        for line in process.stdout:
            # The C app now has more structured output, so we only parse measurement lines.
            measurement = parse_measurement_line(line)
            if measurement:
                print(measurement)
            else:
                # Print non-measurement lines (like status messages) directly.
                print(f"[C-APP] {line.strip()}")

        process.wait()
        if process.returncode != 0:
            print(f"Subprocess exited with error code: {process.returncode}", file=sys.stderr)

    except FileNotFoundError:
        print(f"Error: The application '{command[0]}' was not found.", file=sys.stderr)
        print("Please ensure you have built the project with 'cmake .. && make'", file=sys.stderr)
    except Exception as e:
        print(f"An unexpected error occurred: {e}", file=sys.stderr)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: python3 {sys.argv[0]} <i2c-bus> <i2c-address-hex> <config-file>")
        print(f"Example: python3 {sys.argv[0]} /dev/i2c-1 0x48 configA")
        sys.exit(1)

    i2c_bus_arg = sys.argv[1]
    i2c_addr_arg = sys.argv[2]
    config_file_arg = sys.argv[3]
    
    run_instrumentation(i2c_bus_arg, i2c_addr_arg, config_file_arg)
