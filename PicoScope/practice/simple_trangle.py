import ctypes
import numpy as np
import matplotlib.pyplot as plt
from picosdk.ps4000a import ps4000a as ps
from picosdk.functions import adc2mV, assert_pico_ok
from picosdk.constants import PICO_STATUS


# ------------------- 1. CONNECTION (UPDATED FOR 4824) -------------------
chandle = ctypes.c_int16()
status = {}


print("Connecting to PicoScope 4824/4000A...")


# Try to open the unit
status["openunit"] = ps.ps4000aOpenUnit(ctypes.byref(chandle), None)


# Check for the specific "Non-USB 3.0 Port" warning (Status Code 286)
if status["openunit"] == PICO_STATUS["PICO_USB3_0_DEVICE_NON_USB3_0_PORT"]:
   print("USB 2.0 / Non-powered connection detected. Changing power source...")


   # We must confirm the power source to proceed.
   # The second argument corresponds to the power state we are confirming.
   status["changePowerSource"] = ps.ps4000aChangePowerSource(
       chandle, status["openunit"]
   )


   # Check if the power change was successful
   assert_pico_ok(status["changePowerSource"])
   print("Power source changed successfully.")


# If it wasn't 286, check if it was a standard error
elif status["openunit"] != 0:
   # If it failed for another reason (like device not found), raise the error
   assert_pico_ok(status["openunit"])


print("Device connected!")


# ------------------- 2. SETUP CHANNEL A -------------------
# Handle, Channel(0=A), Enabled(1), Coupling(1=DC), Range(6=1V), Offset(0)
# Range Index 6 (+/- 1V) is perfect for a 1Vpp signal (+/- 0.5V)
channel = ps.PS4000A_CHANNEL["PS4000A_CHANNEL_A"]
coupling = ps.PS4000A_COUPLING["PS4000A_DC"]
chARange = ps.PICO_CONNECT_PROBE_RANGE["PICO_X1_PROBE_1V"]
chAOffset = 0.0


status["setChA"] = ps.ps4000aSetChannel(
   chandle,
   channel,
   1,
   coupling,
   chARange,
   chAOffset,
)
assert_pico_ok(status["setChA"])


# ------------------- 3. SETUP SIGNAL GENERATOR -------------------
# We want 1kHz, 1Vpp Triangle wave
# 1Vpp = 1,000,000 uV
# WaveType: 0=Sine, 1=Square, 2=Triangle


offset_voltage_uv = 0
pk_to_pk_uv = 1000000  # 1V
wave_type = 2  # Triangle
frequency = 1000.0  # 1 kHz


waveType = ps.PS4000A_WAVE_TYPE["PS4000A_TRIANGLE"]
sweepType = ps.PS4000A_SWEEP_TYPE["PS4000A_UP"]
extraOperations = ps.PS4000A_EXTRA_OPERATIONS["PS4000A_ES_OFF"]
sigGenTrigType = ps.PS4000A_SIGGEN_TRIG_TYPE["PS4000A_SIGGEN_RISING"]
sigGenTrigSource = ps.PS4000A_SIGGEN_TRIG_SOURCE["PS4000A_SIGGEN_NONE"]


status["setSigGen"] = ps.ps4000aSetSigGenBuiltIn(
   chandle,
   offset_voltage_uv,
   pk_to_pk_uv,
   waveType,
   frequency,  # Start Freq
   frequency,  # Stop Freq (Same as start for standard wave)
   0,  # Increment
   0,  # Dwell time
   sweepType,  # Sweep type (0=Up, not used here)
   extraOperations,  # Operation (0=Off)
   0,  # Shots
   0,  # Sweeps
   sigGenTrigType,  # Trigger Type
   sigGenTrigSource,  # Trigger Source
   0,  # Ext Threshold
)
assert_pico_ok(status["setSigGen"])
print(f"Signal Generator set to {frequency}Hz Triangle Wave, {pk_to_pk_uv/1e6}Vpp")


# ------------------- 4. SETUP TIMEBASE (ACQUISITION) -------------------
# We want to capture 3 cycles. 1kHz = 1ms period.
# 3ms total window.
# 2500 samples usually gives good resolution.
# Timebase calculation for PS4000A: (timebase - 4) / 80,000,000 = interval (s)
# We will use the API to find a valid timebase roughly fitting our needs.


preTriggerSamples = 0
postTriggerSamples = 2500
maxSamples = preTriggerSamples + postTriggerSamples


# Use timebase index 402 (1us per sample -> 2.5ms total)
timebase = 79
timeIntervalns = ctypes.c_int32()
returnedMaxSamples = ctypes.c_int32()


status["getTimebase"] = ps.ps4000aGetTimebase(
   chandle,
   timebase,
   maxSamples,
   ctypes.byref(timeIntervalns),
   ctypes.byref(returnedMaxSamples),
   0,
)
assert_pico_ok(status["getTimebase"])


# ------------------- 5. CAPTURE DATA -------------------
print("Starting capture...")
status["runBlock"] = ps.ps4000aRunBlock(
   chandle,
   preTriggerSamples,
   postTriggerSamples,
   timebase,
   None,  # timeIndisposedMs
   0,  # segmentIndex
   None,  # lpReady
   None,  # pParameter
)
assert_pico_ok(status["runBlock"])


# Wait for readiness
ready = ctypes.c_int16(0)
check = ctypes.c_int16(0)
while ready.value == 0:
   status["isReady"] = ps.ps4000aIsReady(chandle, ctypes.byref(ready))


# ------------------- 6. RETRIEVE DATA -------------------
buffer = (ctypes.c_int16 * maxSamples)()
status["setDataBuffer"] = ps.ps4000aSetDataBuffer(
   chandle, 0, ctypes.byref(buffer), maxSamples, 0, 0
)
assert_pico_ok(status["setDataBuffer"])


overflow = ctypes.c_int16()
cmaxSamples = ctypes.c_int32(maxSamples)


status["getValues"] = ps.ps4000aGetValues(
   chandle,
   0,  # startIndex
   ctypes.byref(cmaxSamples),
   0,  # downSampleRatio
   0,  # downSampleRatioMode
   0,  # segmentIndex
   ctypes.byref(overflow),
)
assert_pico_ok(status["getValues"])


# ------------------- 7. PLOT -------------------
# Convert ADC values to mV
adc_data = np.ctypeslib.as_array(buffer)
# Max ADC value for PS4000A is usually 32767
maxADC = ctypes.c_int16(32767)


# Convert to mV using the range we set (Range 6 = 1000mV)
data_mV = adc2mV(adc_data, chARange, maxADC)


# Create time axis
time_ns = np.linspace(0, cmaxSamples.value * timeIntervalns.value, cmaxSamples.value)
time_ms = time_ns / 1e6


plt.plot(time_ms, data_mV)
plt.xlabel("Time (ms)")
plt.ylabel("Voltage (mV)")
plt.title("PicoScope 4000A Capture (1kHz Triangle)")
plt.grid(True)
plt.show()


# ------------------- 8. CLEANUP -------------------
status["stop"] = ps.ps4000aStop(chandle)
status["close"] = ps.ps4000aCloseUnit(chandle)
print("Device closed.")
