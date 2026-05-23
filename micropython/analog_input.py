import machine
import time

# Initialize Pin GPIO33=D33
analog_pin = machine.Pin(33)
adc = machine.ADC(analog_pin)

# Configure full range: 11dB attenuation scales the 0V - 3.3V range
# Note: In older MicroPython firmware versions, use: adc.atten(machine.ADC.ATTN_11DB)
adc.width(machine.ADC.WIDTH_12BIT)
adc.atten(machine.ADC.ATTN_11DB)

while True:

    # read_u16() reads the value as a 16-bit integer (0 to 65535)
    #raw_value = adc.read_u16()
    # Convert the 16-bit range directly to a voltage representation
    #voltage = raw_value * (3.3 / 65535.0)
    #print("Raw 16-bit Value: {} | Calculated Voltage: {:.2f}V".format(raw_value, voltage))
    
    # Native 12-bit read (Returns 0 to 4095)
    raw_12bit = adc.read()
    voltage = raw_12bit * (3.3 / 4095.0)
    print("Raw 12-bit Value: {} | Calculated Voltage: {:.2f}V".format(raw_12bit, voltage))
    
    time.sleep(0.5)

