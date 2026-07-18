# 513X 4S Battery Validation

Date: 2026-07-18

## Scope

- Compare the existing MG513X closed-loop behavior after replacing the previous approximately 11 V battery with a 4S battery.
- Preserve the validated 12 V profile and add a separate 4S operating profile.
- Validate suspended dual-wheel operation at 30 rpm and 70 rpm through wired COM9 at 230400 baud.

## Electrical Boundary

- The MG513X motor remains rated at 12 V.
- A normal 4S lithium battery is 14.8 V nominal and can reach 16.8 V fully charged. The actual pack voltage was not measured during this session.
- The AT8236 IC accepts the bus voltage, but the D153B module documentation is inconsistent: one limit is 5.5-17 V and another recommends 5-15 V. The board includes a 25 V bulk capacitor.
- Current, battery sag, driver temperature, and motor temperature were not measured. The 4S result therefore validates control behavior, not the complete electrical or thermal envelope.

## Baseline Failure With The 12 V Profile

With the existing `MG513X v5` parameters and the new 4S battery, a 30 rpm command produced:

- startup peaks of approximately 47.25 rpm left and 49.29 rpm right;
- late-run speed around 37.1 rpm left and 35.1 rpm right;
- normalized PWM around 477/478 permille;
- both integrators saturated at -90 permille, so the controller could not remove the excess feedforward;
- 31 host CRC errors, 33 sequence gaps, and sticky `UART_DMA_STALL` during the first capture.

The output returned to zero and the active issue mask cleared after the test.

## Separate 4S Profile

The original profile remains available as `ECHO_MOTOR_PROFILE_513X`, profile ID 2, version 5. A separate profile was added as `ECHO_MOTOR_PROFILE_513X_4S`, profile ID 5.

The first 4S attempt scaled startup and maximum PWM to 460/520 permille. It was rejected by physical testing: a 30 rpm command stopped on the existing stall guard, and 480, 500, and 520 permille electrical pulses did not reliably start both wheels.

The final `513X-4S v3` profile therefore keeps the validated 600 permille startup and 650 permille output ceiling while scaling the steady-state feedforward:

| Parameter | Left | Right |
| --- | ---: | ---: |
| Feedforward offset | 409 permille | 401 permille |
| Feedforward gain | 1.09 permille/rpm | 1.62 permille/rpm |
| Kp / Ki / Kd | 3 / 8 / 0 | shared |
| Integrator limit | 90 permille | shared |

The right gain was restored after the first 70 rpm run reached the +90 permille integrator limit at 68.45 rpm. The v3 gain reduced the final right integrator to about +68 permille at 70 rpm.

## Final Motion Results

Final 30 rpm standalone run:

- tail mean: 30.865 rpm left, 30.111 rpm right;
- tail PWM: 449.57 permille left, 475.25 permille right;
- tail integrator: +10.42 permille left, +25.91 permille right;
- startup peak: 54.107 rpm left, 55.709 rpm right;
- final frame and Health telemetry both reported zero output.

Final 30 to 70 rpm step:

- t90: 670 ms left, 600 ms right;
- peak: 73.286 rpm left, 71.143 rpm right;
- 70 rpm tail mean: 70.129 rpm left, 70.059 rpm right;
- 70 rpm tail PWM: 527.72 permille left, 582.49 permille right;
- 70 rpm tail integrator: +42.88 permille left, +68.26 permille right;
- active/sticky Health issues, deadline misses, DMA stall, encoder-late count, and output residue were all zero.

The 30 rpm and 70 rpm steady-state errors are within 3%. Speeds above 70 rpm were not validated on 4S.

## UART Limitation

The MCU-side control loop remained healthy, but the host UART capture is not production-clean while the reflectance multiplexer scans internally at 125 Hz:

- final 30 to 70 step: 40 CRC errors / 40 sequence gaps;
- final 30 rpm run: 63 CRC errors / 63 sequence gaps;
- device-side UART drop/overflow counters and Health masks remained zero.

This is consistent with the previously isolated electrical coupling from the reflectance AD0/AD1/AD2 switching. Add 100-330 ohm series damping, improve grounding and wire separation, then repeat the combined motor and reflectance test before using this UART path for production commands.

## Build And Hardware Evidence

- FreeRTOS application build: 0 errors, 0 warnings.
- 12 V and 4S profile compile checks passed.
- Final HEX SHA-256: `D110D1B5DAC13BEDFFC3744CF93412CA949CEF1FC9267713D58061968F7BF0F2`.
- CMSIS-DAP: Horco CMSIS-DAP v2, serial `2b5d6f2a`, 500 kHz SWD.
- UART: COM9, 230400 8N1.
- Raw evidence: `tmp/four-s-v3-final-30rpm` and `tmp/four-s-v3-step-30-70`.

## Remaining Work

- Measure battery voltage before and during load, supply current, driver temperature, and motor temperature.
- Repeat on the ground with the actual chassis load and verify straight-line behavior.
- Fix the reflectance/UART electrical coupling and require a combined zero-error acceptance run.
- Validate any requested speed above 70 rpm separately.
