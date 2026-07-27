# MPU6050 PA0/PA1 validation

Date: 2026-07-27

## Wiring and configuration

- I2C peripheral: I2C0, 400 kHz
- SDA: PA0
- SCL: PA1
- Observed slave address: `0x68`
- Observed `WHO_AM_I`: `0x68` (MPU6050 profile)
- OLED, TFmini, reflectance, ESP link: disabled
- Motors: no command sent, output remained zero

## Result

The IMU completed probe, reset, configuration register readback, 500 ms settle,
300-sample stationary gyro calibration, and entered READY. A 10-second COM18
capture contained 253 IMU telemetry frames. Device sampling reached 2770 valid
samples at 100 Hz with zero failures and zero I2C errors.

Latest stationary values:

- Acceleration: `-0.0172, -0.1359, 1.0872 g`
- Acceleration norm: `1.0958 g`
- Calibrated gyro: `-0.0274, -0.0076, -0.0075 dps`
- Temperature: `31.32 C`
- Sample age: `2615 us`

The host serial capture reported 7 CRC/gap events. Device transport drops,
deadline misses, active/sticky issues, and I2C errors remained zero, so these
are classified as the existing DAPLink UART capture limitation rather than an
IMU fault.

## Firmware

- Build: 0 errors, 0 warnings
- HEX SHA-256: `ABD6011458614DFD5C8B2AC3FB2821F81BD73C98FB092143F853989F060D92A6`
- Flash binary readback SHA-256: `2BE288F884E59772E14AAF0330842A7D62E11789287B560146A6EE507C875497`
- Final actuator state: stopped

## 100 Hz static characterization and bias tracking

A temporary diagnostic build suppressed control telemetry and exported every
100 Hz IMU snapshot with raw gyro, calibrated gyro, and active bias. The first
60.731-second static capture contained 6073 valid IMU frames. Device sampling
was 100.000 Hz with zero sample failures and zero I2C errors. DAPLink lost 9
host frames; device-side health and transport counters remained clean.

Baseline static measurements:

- Gyro raw mean X/Y/Z: `12.29858 / 2.53605 / -0.47809 dps`
- Bias-corrected mean X/Y/Z: `0.00097 / 0.00109 / 0.01030 dps`
- Bias-corrected standard deviation: `0.03598 / 0.03304 / 0.02888 dps`
- Integrated X/Y/Z drift: `0.0593 / 0.0648 / 0.6266 deg`
- Acceleration norm: mean `1.09380 g`, standard deviation `0.00228 g`
- Temperature: `29.7918 -> 29.9388 C`

The 300-sample startup calibration is long enough for random-noise averaging;
the remaining Z drift is dominated by bias movement after startup. Increasing
startup calibration duration alone was therefore rejected. The 100 Hz sample
rate and existing approximately 25 Hz software low-pass were retained.

Stationary bias tracking was added after READY. It requires one continuous
second with actuator output disabled, both applied outputs zero, both measured
wheel speeds below `0.5 rpm`, every gyro residual below `0.5 dps`, and the
acceleration norm inside the existing validity window. It then updates gyro
bias with alpha `0.002` (approximately 5 seconds at 100 Hz). Any failed gate
immediately freezes tracking and restarts stationary confirmation.

The 60.730-second comparison capture contained 6065 valid IMU frames:

- Bias-corrected mean X/Y/Z: `0.00058 / -0.00095 / 0.00057 dps`
- Bias-corrected standard deviation: `0.02751 / 0.02800 / 0.02497 dps`
- Integrated X/Y/Z drift: `0.0348 / -0.0554 / 0.0372 deg`
- Z drift improved by approximately `94.1%` versus the baseline.

The acceleration magnitude remains about 9.3% high. A single static pose cannot
separate chassis tilt, axis offsets, and scale error, so no one-pose correction
was applied. Perform a six-face accelerometer calibration before using absolute
pitch/roll as a precision reference.

The final formal build restores control telemetry to 100 Hz and IMU telemetry
to 25 Hz while device sampling remains 100 Hz. Final validation received 1018
control frames at 100.000 Hz and 240 IMU frames with a measured 100.000 Hz
device sample rate. Health, I2C, deadlines, device drops, encoder-late count,
and actuator output were all clean. Final HEX SHA-256:
`210D6AC1C67730664BA04C4B1EDD26C74559E03F9345CD9FF3CB39924D1E2748`.

## Hair-dryer temperature-ramp experiment

A 910.36-second formal-mode capture recorded 21175 valid raw IMU frames. The
die temperature started at `29.927 C`, reached `39.451 C` at 361.7 seconds,
and naturally cooled to `33.730 C`. Device sampling remained 100 Hz with zero
sample failures, I2C errors, health issues, deadlines, or actuator output.
Host DAPLink capture corruption was high over the 13 MB stream, but valid IMU
coverage remained approximately 23 Hz and was sufficient for slow thermal
analysis.

Five-second-window linear regressions of raw gyro bias versus die temperature:

| Axis | Heating slope (dps/C) | Heating R2 | Cooling slope (dps/C) | Cooling R2 |
| --- | ---: | ---: | ---: | ---: |
| X | -0.000167 | 0.0065 | -0.003503 | 0.3458 |
| Y | -0.015347 | 0.9938 | -0.018774 | 0.9755 |
| Z | -0.006226 | 0.8792 | -0.012315 | 0.9416 |

At matching 0.5 C bins, cooling-minus-heating bias differed by approximately
`0.02--0.06 dps` on X and `0.017--0.050 dps` on Z. Heating also increased
acceleration-norm standard deviation from `0.00244 g` to `0.00654 g`. These
results show temperature sensitivity but also substantial airflow vibration,
thermal-gradient, or package/PCB stress hysteresis. The key Z-axis heating and
cooling slopes differ by nearly 2x, so a single temperature coefficient from
this run would be unsafe and was not added to firmware.

Temperature compensation remains deferred until an independently repeated,
slower ramp reproduces the Z-axis curve. The validated motion-gated stationary
bias tracker remains the active correction.

## Final decision and next action

- At room temperature before stationary tracking, mean Z residual was
  `0.01030 dps`, equivalent to `0.6266 deg` integrated drift over 60.731 s.
- With stationary tracking active, mean Z residual was `0.00057 dps` and the
  measured 60.730-second drift was `0.0372 deg`.
- The approximately 9.5 C hair-dryer excursion produced an apparent Z-bias
  change of roughly `0.06--0.12 dps`, but heating/cooling hysteresis prevents
  that range from being used as a deterministic correction coefficient.
- No temperature coefficient is enabled in formal firmware. A wrong Z model
  could add several degrees per minute and would be worse than the validated
  stationary tracker.
- Operational rule: power on with the chassis stationary for at least 30--60
  seconds before a precision run so the online bias estimate can settle.
- Development priority: perform six-face accelerometer calibration, then add
  and bench-test the 100 Hz attitude/angle estimator. Revisit temperature
  compensation only after a second slower temperature ramp independently
  reproduces the Z-axis relationship.

## Installed-module six-face accelerometer calibration

Six accepted 30-second captures were collected at approximately
`30.11--30.46 C`. Rejected transition or unstable captures were not used.
Accepted dominant-axis means were:

| Vehicle pose | Sensor direction | Dominant measurement (g) |
| --- | --- | ---: |
| Left side down | +X | 1.007889 |
| Right side down | -X | -0.991385 |
| Nose up | +Y | 0.988998 |
| Nose down | -Y | -1.015357 |
| Normal | +Z | 1.086690 |
| Inverted | -Z | -0.929402 |

The installed-module diagonal correction is
`corrected = (uncalibrated - bias) * scale` with:

- Bias X/Y/Z: `0.00825236 / -0.01317945 / 0.07864387 g`
- Scale X/Y/Z: `1.00036323 / 0.99782687 / 0.99201797`

Offline replay reduced the six pose mean-norm range from
`0.930984--1.089627 g` to `1.000289--1.002875 g`; maximum absolute norm error
was approximately 0.29%. The constants are stored in
`config/vehicle_bringup_config.h` because they are specific to the currently
installed sensor and mounting.

The calibrated formal build passed with 0 errors and 0 warnings and was
programmed/verified/reset through DAPLink `2e4c7219`. A fresh normal-pose
30-second validation measured acceleration `(-0.03643, -0.04091, 1.00259) g`,
mean norm `1.00409 g`, norm standard deviation `0.00222 g`, and latest norm
`1.00019 g`. This reduced normal-pose magnitude error from about 8.96% to
0.41%. Device sampling, I2C, health, timing, drops, and actuator state remained
clean. Final HEX SHA-256:
`F8333A274EB99B51C327469AA3171327D03E905E81AFB35CD94361B3129AA3EF`.

## 100 Hz six-axis attitude estimator

The six-face calibration solved the installed accelerometer's per-axis offset
and scale errors. It provides a trustworthy gravity magnitude, roll/pitch
reference, and stationary gate. It does not provide absolute yaw, remove gyro
temperature drift, or estimate sensor-axis non-orthogonality. The approximately
3 degree normal-pose roll/pitch values observed below include physical chassis,
table, and sensor-mount alignment; a separate installed-level trim is required
if the parked chassis must display exactly zero degrees.

An independent attitude estimator now consumes each new IMU snapshot at
100 Hz. Installed sensor coordinates are mapped to vehicle coordinates as:

```text
vehicle forward = sensor +Y
vehicle left    = sensor -X
vehicle up      = sensor +Z
```

The estimator integrates a unit quaternion from the calibrated gyro and uses
the calibrated gravity vector for proportional roll/pitch correction. Full
accelerometer correction is used within 0.05 g of 1 g, tapered to zero between
0.05 and 0.15 g, and rejected beyond 0.15 g. Duplicate samples are ignored.
Sample intervals outside 2.5--50 ms cause a gravity-based tilt reset while
preserving relative yaw. Yaw starts at zero after power-up and remains relative;
without a magnetometer or external landmark it cannot be geographic heading.

Telemetry frame type 13 exports roll, pitch, relative yaw, mapped axis rates,
accelerometer weight, estimator interval, and diagnostic counters. The formal
build computes at 100 Hz and sends attitude telemetry at 25 Hz. Parser support
and binary fixtures were added to `tools/telemetry_capture.ps1`.

A 60.72-second static capture contained 1509 valid attitude frames. Device-side
results were:

- IMU sample rate: `100.000 Hz`
- Attitude estimator sample rate: `100.000 Hz`
- Roll: mean `-3.38816 deg`, standard deviation `0.01019 deg`, peak-to-peak
  `0.05918 deg`
- Pitch: mean `-3.11431 deg`, standard deviation `0.01023 deg`, peak-to-peak
  `0.06449 deg`
- Relative yaw: standard deviation `0.00658 deg`, peak-to-peak `0.03162 deg`
- Relative yaw first-to-last change: `-0.00134 deg`
- Timing resets: `0`
- Acceleration correction weight: `1.0` for every received attitude frame
- IMU sample failures, I2C errors, deadline misses, device telemetry drops, and
  actuator output permission: all zero

DAPLink UART capture again lost host frames and reported CRC/sequence gaps, but
all device-side transport and health counters were clean. A second 60-second
capture intended for manual movement remained fully stationary, so it only
confirmed the same return noise and was not used as dynamic validation.

A subsequent manual dynamic capture accepted all three installed-axis signs:

- Nose-up motion changed pitch from approximately `-3.1 deg` to `+21.1 deg`.
- Lowering the vehicle's left side changed roll from approximately `-3.4 deg`
  to `+21.8 deg`.
- Turning the vehicle left changed relative yaw from approximately `0 deg` to
  `+86.2 deg`.
- The estimator remained at `100.000 Hz` with zero timing resets, I2C errors,
  health issues, deadline misses, or actuator output permission.
- Brief hand-motion acceleration correctly reduced the accelerometer correction
  weight to zero for two received frames instead of corrupting tilt correction.

The 60-second capture ended just after the manual return turn began. A follow-up
stationary capture ended at `-10.54 deg` relative yaw because the hand return was
approximate, while roll/pitch returned to `-3.34/-3.11 deg`. This is accepted as
operator return error rather than estimator drift; the earlier untouched static
run remains the drift measurement. Dynamic attitude validation is complete.

The final source-validity safety update passed with 0 errors and 0 warnings and
was programmed/verified/reset through DAPLink `2e4c7219`. A fresh 10-second
smoke capture measured both IMU and estimator sampling at `100.000 Hz`, with
zero I2C errors, health issues, deadline misses, timing resets, or actuator
output permission. The programmed HEX SHA-256 was
`2EF9EC76C0B4B5697EBE2429B52308F249749F8229209B70EEF7D85AB7717261`.

## Heading outer-loop suspended-wheel test

After dynamic attitude validation, a heading control mode was added above the
existing wheel-speed loops. A command carries base wheel speed in deci-rpm,
relative heading target in deci-degrees, and a finite duration. The heading
controller runs at the 100 Hz system boundary:

```text
error = wrap(target_yaw - current_yaw, -180..180)
correction = limit(0.25 * error - 0.03 * yaw_rate, +/-6 rpm)
left_target  = base_rpm - correction
right_target = base_rpm + correction
```

The existing speed PID, startup boost, stall handling, battery-voltage
compensation, output limits, and timed safe stop remain in the actuator path.
The mode rejects stale/invalid attitude data and stops if the estimator is not
fresh within 50 ms. An explicit zero speed command can also interrupt an active
heading command.

Suspended-wheel test command: base `12 rpm`, relative target `+10 deg`, duration
`4 s`. Results from the COM18 capture:

- Heading command ACK: valid, status `0`, mode `2`
- Heading control frames: `398`, tracking frames: `396`
- Target right-minus-left difference: `+5.00 rpm`
- Measured right-minus-left difference: `+4.33 rpm`
- Attitude sample rate: `100 Hz`; control rate: `99.633 Hz`
- Device health, I2C, deadlines, and final actuator permission: clean/zero
- Timed stop completed with actuator output permission `0`

The sign is correct: positive (left-turn) heading error makes the right wheel
faster than the left wheel. Host DAPLink capture still had CRC/sequence gaps,
but the test deliberately evaluates device health and valid frames separately.
This test proves command parsing, sign, speed-loop interaction, and stop safety;
it does not prove ground trajectory tracking because the wheels were suspended.
The programmed HEX SHA-256 for this build is
`E3DEFF1F1115CCD8114E7698F57A933388715DC922AA203E57E20D9E6333CD3A` and the
Flash readback SHA-256 was
`3A8DC3FE672383A787CEE726CD3DE72892D0CBB7C5EB1E25D9266282718086D7`.
