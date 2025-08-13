# -*- coding: utf-8 -*-
"""
This script calculates the effective voltage resolution for a 15-bit ADC with an
external voltage divider on each of its four input channels.

The ADC has the following characteristics:
- Resolution: 15 bits
- Full-scale range: 4096mV

The script determines if a 1mV change at the input of the voltage divider
results in a change of at least 1 digital code from the ADC.
"""

# --- ADC Parameters ---
ADC_BITS = 15
FULL_SCALE_MV = 4096.0  # Full-scale range in millivolts

def calculate_adc_step_size(bits, full_scale):
    """Calculates the ADC's intrinsic step size in mV per digital code."""
    number_of_codes = 2**bits
    return full_scale / number_of_codes

def get_voltage_divider_ratio(r1, r2):
    """
    Calculates the voltage division ratio.
    Assumes a standard voltage divider circuit where the output voltage is
    taken across R2.
    Vin ---- R1 ----+---- Vout
                   |
                   R2
                   |
                  GND
    """
    if r1 + r2 == 0:
        return 0
    return r2 / (r1 + r2)

def main():
    """
    Main function to run the resolution calculation for each channel.
    """
    print("--- ADC Resolution Calculator ---")
    print(f"ADC: {ADC_BITS}-bit, Full-Scale Range: {FULL_SCALE_MV}mV\n")

    # Calculate the ADC's intrinsic step size (mV per code)
    adc_step_mv = calculate_adc_step_size(ADC_BITS, FULL_SCALE_MV)
    print(f"The ADC's intrinsic step size is: {adc_step_mv:.4f} mV per digital code.\n")

    # --- Channel Analysis ---
    num_channels = 4
    for i in range(num_channels):
        print(f"--- Channel {i+1} ---")
        try:
            # Get resistor values from the user
            r1 = float(input(f"Enter the value of R1 for Channel {i+1} (in Ohms): "))
            r2 = float(input(f"Enter the value of R2 for Channel {i+1} (in Ohms): "))

            if r1 < 0 or r2 < 0:
                print("Resistor values cannot be negative. Please try again.\n")
                continue

            # Calculate the voltage divider ratio
            ratio = get_voltage_divider_ratio(r1, r2)
            if ratio == 0:
                print("The voltage divider ratio is zero (R1+R2=0). Cannot calculate effective resolution.\n")
                continue
                
            print(f"Voltage divider ratio for Channel {i+1}: {ratio:.4f}")

            # Calculate the effective voltage step (mV per code) at the divider's input
            mv_per_code_at_input = adc_step_mv / ratio
            
            # Calculate the number of codes per mV of change at the input
            # This is the inverse of the effective voltage step
            codes_per_mv_at_input = 1.0 / mv_per_code_at_input
            
            print(f"Effective sensitivity for Channel {i+1}: {codes_per_mv_at_input:.4f} digital codes per 1mV.")

            # Check if the condition is met (at least 1 code per mV)
            if codes_per_mv_at_input >= 1.0:
                print("✅ Condition MET: You get at least 1 digital code change per 1mV.\n")
            else:
                print("❌ Condition NOT MET: You get less than 1 digital code change per 1mV.\n")

        except ValueError:
            print("Invalid input. Please enter a numerical value for the resistors.\n")
        except Exception as e:
            print(f"An error occurred: {e}\n")

if __name__ == "__main__":
    main()
