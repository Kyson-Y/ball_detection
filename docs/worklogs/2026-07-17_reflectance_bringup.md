# Eight-Channel Reflectance Sensor Bring-Up

Date: 2026-07-17

## Scope

- Connect the no-MCU eight-channel reflectance board through its 74HC4051 analog output.
- Preserve the 100 Hz chassis control loop and keep all motor outputs at zero.
- Publish raw ADC evidence without adding line-following or calibration logic.

## Wiring

| Sensor | MSPM0G3507 |
| --- | --- |
| `GND` | `GND` |
| `+5V` | clean regulated 5 V |
| `OUT` | `PA26 / ADC0_CH1` |
| `AD0` | `PA27` |
| `AD1` | `PA24` |
| `AD2` | `PA25` |
| `EN` | open, internal pull-down enables board |
| `ERR` | open |

The user measured OUT against GND and confirmed it remains at or below 3.3 V before connection to PA26.

## Implementation

- `bsp_reflectance` samples the address selected during the previous service period, then advances to the next address using one masked GPIO write without an intermediate `000` state.
- The retained scheduler samples one channel every 1 ms. A complete CH1-CH8 scan is produced every 8 ms, or 125 Hz; raw telemetry publishes every 125th scan at 1 Hz while the electrical-coupling fix is pending.
- ADC conversions use a 100 us bounded poll. Timeout and incomplete-scan counters are exposed in diagnostics and telemetry.
- Telemetry frame type 8 carries scan sequence, eight 12-bit raw values, minimum/maximum, timeout count, incomplete scan count, and sample count.
- `telemetry_capture.ps1` decodes type 8 while retaining legacy 40/44/96-byte Control compatibility.
- The static UART transmit ring was increased from 1024 to 2048 bytes after
  the first capture reached a 1020-byte high-water mark.

## Verification

- SysConfig: 0 errors; PA26 is ADC0 channel 1 and PA27/PA24/PA25 are digital outputs.
- FreeRTOS and application full rebuild: 0 errors, 0 warnings.
- Program size: Code 67,984 B, RO 3,176 B, RW 188 B, ZI 18,140 B.
- Final HEX SHA-256: `5EA259EADF24D1949F59AA38B12BE72D850707BA738BE4731C7B36B39D2BB745`.
- Wireless CMSIS-DAP at 500 kHz program, fast verify, and reset passed for the final image.
- 30-second COM7 capture: 3052 Control at 100 Hz, 381 Reflectance at 12.5 Hz, 30 Health and 30 Motor Profile at 1 Hz.
- CRC errors, sequence gaps, deadline misses, ADC timeouts, incomplete scans, Health issues, and transport drops were all zero.
- Latest raw values: `[131, 845, 1569, 33, 411, 733, 1076, 1061]`.
- All 3052 Control frames had zero target, zero encoder delta, zero PWM, and no motor-active flags.
- After the UART ring increase, a final 10-second capture produced 1019
  Control and 127 Reflectance frames with zero errors or drops. Ring high-water
  remained 1020 bytes out of the new 2048-byte capacity. Latest raw values were
  `[73, 181, 913, 70, 985, 464, 912, 689]` and all motor fields remained zero.

## Scan-Rate Sweep

| Full scan | Raw telemetry | 30 s host result | Device result |
| ---: | ---: | --- | --- |
| 12.5 Hz | 12.5 Hz | 0 CRC / 0 gap in the initial baseline | clean |
| 15.625 Hz | 15.625 Hz | 5 CRC / 5 gap | clean |
| 20.833 Hz | 10.417 Hz | 3 CRC / 3 gap | clean |
| 31.25 Hz | 15.592 Hz | 2 CRC / 3 gap | clean |
| 62.5 Hz | 12.5 Hz | 10 CRC / 11 gap after masked GPIO update | clean |
| 125 Hz | about 124 Hz | 23 CRC / 26 gap | clean |
| 125 Hz | 1 Hz | 21 CRC / 29 gap | clean |

The device-side ADC, deadlines, Health, transport counters, and motor outputs
remained clean at every rate. The 125 Hz / 1 Hz telemetry isolation proves the
high-rate failure is not UART bandwidth alone.

On 2026-07-18 the same VID_FAED:4873 DAP was switched to its wired mode and the
sensor was powered for a new test. At 125 Hz, a 30-second capture measured an
exact 125 Hz internal scan rate with 0 ADC timeout, 0 incomplete scan, 0
deadline miss, and raw values `[3169, 3340, 2369, 345, 225, 193, 195, 212]`.
The host still observed 20 CRC errors and 26 sequence gaps. A software control
build reduced mux-address switching to 0.5 Hz without changing the 100 Hz
Control stream; the following 30-second wired capture was completely clean
with 0 CRC errors and 0 sequence gaps. This isolates the corruption to
electrical coupling from AD0/AD1/AD2 switching, not DAP UART bandwidth.

The final board image retains the proven 125 Hz internal scan and 1 Hz raw
telemetry. Add 100-330 ohm series damping to AD0, AD1, and AD2, keep the UART
wires short with a solid common ground, then repeat the zero-error acceptance
test before treating the UART command path as production-ready.

## Remaining Work

- Identify the exact sensor response-time variant before increasing scan rate.
- Record black and white references for every channel, then add normalization and hysteresis.
- Validate sensor height, illumination sensitivity, channel order on the physical board, and moving-surface performance before line-following control.
