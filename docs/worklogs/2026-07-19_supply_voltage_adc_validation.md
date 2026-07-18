# 2026-07-19 Supply-voltage ADC validation

## Objective

Add and validate a dedicated 4S battery-voltage ADC path without moving either
513X motor. This stage validates the MCU ADC, scheduling, filtering, telemetry,
and safety state. Absolute voltage calibration requires the physical divider
and a simultaneous multimeter reference.

## Electrical contract

- MCU input: `PB17 / ADC1_CH4`
- Divider high side: 100 kohm from battery positive to PB17
- Divider low side: 22 kohm from PB17 to GND
- Input filter: 100 nF from PB17 to GND
- Battery ground and controller ground: common
- Conversion: `raw / 4095 * 3.3 * 122 / 22`
- Expected PB17 at 16.8 V: approximately 3.03 V

The 4S positive terminal must never be connected directly to PB17. Before
connecting PB17 to the MCU, measure the divider midpoint and confirm it is at or
below 3.3 V.

## Firmware implementation

- Dedicated ADC1 instance; the reflectance ADC remains on ADC0.
- 40 us configured sample window.
- 10 ms sampling period, approximately 100 Hz.
- First-order IIR filter with 1/8 update weight.
- UART telemetry type 9 every 100 ms, approximately 10 Hz.
- Telemetry includes sequence, raw and filtered raw values, ADC input mV,
  battery mV, sample count, and conversion timeout count.

## Build and flash

- App full rebuild: 0 errors, 0 warnings.
- Program size: Code 69,328 B; RO-data 3,184 B; RW-data 188 B; ZI-data 18,188 B.
- HEX SHA-256:
  `6BC7F10A6E4A4C1EC118E01DF2B7931DBE409B945A5F398E0238AAC5B5DB2E2C`.
- Adapter: Horco CMSIS-DAP v2, serial `2b5d6f2a`, SWD 500 kHz.
- OpenOCD target CRC verification timed out; the flash script completed its
  byte-for-byte readback fallback successfully.
- Flash readback image SHA-256:
  `88A6F64F3E049037DC3D2C9837CD44CCAD9CEB47BFCD62F5739C266DF5136556`.

## Static board capture

Source: COM9, 230400 8N1, 10 seconds after a 2-second flush.

| Metric | Result |
| --- | --- |
| Supply frames | 102 |
| Supply telemetry rate | 10 Hz |
| ADC sample rate | 100 Hz |
| ADC conversion timeouts | 0 |
| Raw range | 657-803 |
| Raw average | 712.559 |
| Reported battery range | 3173-3392 mV |
| Reported battery average | 3222 mV |
| Latest filtered ADC input | 583 mV |
| Latest battery estimate | 3235 mV |
| Deadline misses | 0 |
| Device publish/transport/DMA drops | 0 |
| Health active/sticky issues | 0/0 |
| Actuator output permitted | 0 |

All observed motor targets, measurements, and controller outputs remained zero.
No motor command was sent.

COM9 recorded 29 CRC/sequence-gap events in mixed telemetry. The device-side
Health counters and timing stayed clean; this is consistent with the already
known host UART signal-integrity issue under combined telemetry load. The
capture script therefore returned its integrity-gate failure even though all
102 valid supply frames decoded correctly.

## Conclusion

The ADC peripheral, task timing, filter, UART frame, and host decoder are
working. The measured PB17 level is not compatible with a powered 4S battery
through the specified divider. The current 3.22 V estimate must not be used as
a battery result; the divider is absent, incorrectly wired, or not connected
to the battery.

## Direct low-voltage retry

The user connected a nominal 1.5 V cell to PB17 through a 10 kohm safety
resistor, with the cell negative connected to controller GND. The first two
attempts remained unstable and were rejected as contact/wiring transients. The
third 10-second capture was stable:

| Metric | Result |
| --- | --- |
| Valid supply frames | 102 |
| Sample / telemetry rate | 100 / 10 Hz |
| Raw minimum / maximum | 1667 / 1687 |
| Raw average | 1678.824 |
| Calculated average ADC input | approximately 1353 mV |
| Latest filtered ADC input | 1351 mV |
| ADC-input peak-to-peak variation | approximately 16 mV |
| Firmware battery estimate | 7490-7526 mV |
| Conversion timeouts / deadline misses | 0 / 0 |
| Actuator output permitted | 0 |

The 7.50 V firmware battery estimate is expected for this temporary direct-input
test: the firmware multiplies the ADC input by the configured 122/22 divider
ratio. The direct-input evidence confirms that PB17, ADC1_CH4, filtering, and
telemetry work. It does not validate the physical 100k/22k divider or absolute
4S calibration.

## Required next test

1. Power down and install or verify the 100k/22k/100nF network and common ground.
2. With PB17 disconnected from the MCU, power the divider and measure its
   midpoint. Continue only if it is <= 3.3 V.
3. Reconnect PB17 and run the same 10-second static capture.
4. Record a simultaneous multimeter reading of the battery.
5. Apply `calibration = multimeter_mv / adc_reported_mv` only after the wiring
   check passes.

No files were staged, committed, or pushed in this stage.
