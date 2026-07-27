# 513X Ground Speed and Heading Tuning

Date: 2026-07-27

## Test setup

- Wheel diameter: 65 mm.
- Floor: ceramic tiles with higher transient resistance at the joints.
- Chassis: two driven wheels and a front swivel caster. The caster may produce
  a large startup yaw transient while rotating into the travel direction.
- Battery during the tests: approximately 16.42--16.56 V.
- Motor profile: MG513X-4S.
- Reflectance scanning was disabled during motion tests.
- Every command used a finite device-side duration and ended with actuator
  output disabled.

## Speed-only baseline

The first 20 rpm, 4 s ground run started both wheels at 10 ms with zero start
skew, but the final one-second averages were only 14.067 rpm left and
12.570 rpm right. Both speed-loop integrators reached the old 90 permille
limit. The output limit was not reached, proving that the suspended-wheel
integrator limit, rather than available motor voltage, prevented load
rejection.

Yaw changed by -2.82 degrees. The first second included approximately
+/-11 dps caster-alignment motion. A slower residual yaw remained because the
left wheel was faster than the right wheel.

## Ground-load speed-loop update

Profile v14 changed only the load-correction authority:

- `Ki`: 8.0 -> 20.0 permille/(rpm*s)
- Integrator limit: 90 -> 200 permille
- Proportional gain: unchanged at 6.0 permille/rpm
- Derivative gain: unchanged at 0
- Output limit: unchanged at 650 permille

The 20 rpm, 5 s repeat passed:

| Metric | Left | Right |
| --- | ---: | ---: |
| Start time | 10 ms | 10 ms |
| Final 1 s average | 19.355 rpm | 20.087 rpm |
| Final 1 s range | 17.679--20.786 rpm | 18.429--21.429 rpm |

Battery voltage was 16.418--16.556 V. Device Health, deadlines, encoder ISR
late count, I2C, and final actuator output were all clean.

## Heading-loop ground tests

The user manually straightened the chassis before the positive-angle test.
The command therefore intentionally moved the chassis five degrees away from
that manually aligned direction; it was still a valid signed-angle test.

| Command | Start yaw | End yaw | Achieved | Final error | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| +5 deg, 20 rpm, 6 s | +2.167 deg | +7.722 deg | +5.555 deg | -0.555 deg | pass |
| -5 deg, 20 rpm, 5 s | +2.007 deg | -3.940 deg | -5.947 deg | +0.947 deg | pass |
| 0 deg hold, 20 rpm, 4 s | -3.914 deg | -3.706 deg | +0.207 deg | -0.207 deg | pass |

The original host test incorrectly required the final wheel-speed difference
to retain the commanded turn sign. A closed loop should reduce or reverse its
correction after reaching the target, so acceptance now uses measured yaw
change and final heading error directly.

During the zero-degree hold, final-one-second average motor outputs were
541 permille left and 561 permille right; the maximum was 566 permille. The
maximum observed integrators were 116 and 144 permille. This leaves useful
headroom below the 650 permille output and 200 permille integrator limits for
normal payload changes and tile-joint disturbances.

## Interpretation and next validation

PID gains depend on chassis inertia, drivetrain friction, payload, center of
gravity, tire grip, and floor conditions. Small and moderate payload changes
should not require manual retuning: feedforward provides the nominal drive,
while PI supplies the additional torque. The current anti-windup, load-release
unwind, output slew limits, finite-duration command, and stall recovery remain
active.

After repositioning the car in a longer clear lane, run repeated zero-heading
holds across several tile joints with no payload and representative payloads.
Record speed-drop depth, recovery time, maximum PWM/integrator, yaw excursion,
and battery sag. Retune feedforward or acceleration limits only if repeated
data shows insufficient headroom or excessive recovery time.

Wireless DAPLink captures still showed host-side CRC/sequence gaps, while all
device-side transport, Health, deadline, and encoder counters remained zero.
Motion results above use valid device frames and final device Health.

## 40 rpm / 1.5 m run

The chassis was returned to the start position and the MCU was reset. The
MPU6050 repeated its 300-sample startup calibration and reached READY at
100 Hz. Preflight yaw was 0.044 degrees and actuator output was disabled.

An initial 11 s command was correctly rejected without motion because the
device-side heading-command limit was still 10 s. The finite heading duration
limit was extended to 15 s, the application rebuilt with zero errors and zero
warnings, and the full image passed byte-for-byte Flash readback verification.

The accepted run used 40 rpm, zero relative heading, and an 11 s device-side
timeout. With a 65 mm wheel, the target distance was 1.497 m. Encoder-derived
results were:

| Metric | Left | Right |
| --- | ---: | ---: |
| Whole-run average | 39.119 rpm | 39.102 rpm |
| Average after 2 s | 39.683 rpm | 40.007 rpm |
| Minimum after 2 s | 36.429 rpm | 37.714 rpm |
| Average output after 2 s | 565 permille | 573 permille |
| Maximum output after 2 s | 581 permille | 584 permille |
| Maximum integrator after 2 s | 106 permille | 119 permille |

The estimated traveled distance was 1.460 m; the approximately 37 mm shortfall
is consistent with the finite acceleration ramp. Startup caster alignment
reached -3.04 degrees, the controller crossed zero near 4 s, peaked at
+1.26 degrees, and finished at +0.25 degrees. Battery voltage remained
16.451--16.532 V. The command ended automatically with clean device Health,
zero deadlines, zero I2C or encoder errors, and actuator output disabled.

The complete attitude stream contained 380 published frames and retained a
100 Hz device sample rate. Roll remained -2.31 to -2.00 degrees and pitch
-3.21 to -1.75 degrees. Yaw rate was -9.07 to +5.60 dps. Tile-joint and caster
transients moved acceleration norm through 0.66--1.15 g, so accelerometer
correction was temporarily gated as designed while gyro propagation remained
valid. IMU sample failures, attitude rejects during the run, timing resets,
and I2C errors were all zero.

## Low-slip 90 degree pivot

A dedicated zero-base-speed pivot was added without changing the normal
straight-line correction limit. It uses opposite wheel targets capped at
10 rpm, holds at least 8 rpm while outside the target window, and latches both
targets to zero inside a 2 degree error window. It does not reverse to chase a
small overshoot. The finite command timeout remains 12 s for this test.

The ground test commanded a +90 degree left pivot:

- Start yaw: -0.051 degrees
- Target-stop yaw: 88.064 degrees
- Final yaw: 88.667 degrees
- Achieved turn: 88.718 degrees
- Final error: 1.282 degrees
- Active turning time: 8.00 s
- Average yaw rate while turning: 11.03 dps
- Average measured wheel speed: -6.01 / +6.40 rpm
- Commanded wheel directions never reversed
- Final actuator output and all device Health counters: zero/clean

The wheel-speed/yaw-rate relationship implies an effective track width near
0.22 m, which is physically plausible and does not indicate sustained wheel
spin. Individual wheels briefly slowed against caster or tile resistance and
the existing recovery logic increased output to about 604 permille. Absolute
zero slip cannot be proven without the measured physical track width or an
external position reference; visual tire motion and floor marks remain the
final check for small scrub.

## Smooth pivot update and speed sweep

The first pivot was angle-accurate but visibly stuttered at high frequency.
Telemetry confirmed that the generic low-speed stall recovery repeatedly
cleared both PI integrators and applied approximately 600 permille recovery
pulses. This behavior is useful for a wheel that is genuinely stuck during
normal driving, but it is unsuitable for a deliberate low-speed pivot where
the caster and tire scrub can repeatedly reduce one encoder speed.

Pivot mode now bypasses the pulse recovery path, starts each wheel-speed PI
integrator with a signed 150 permille preload, and remains under continuous PI
control. Normal speed and straight-heading modes retain their original stall
recovery. The user reported that the resulting 10 rpm right pivot was much
smoother. It achieved -89.046 degrees with 0.954 degree final error and clean
device Health.

The pivot speed cap was then swept to 15 and 25 rpm. The 25 rpm cap does not
produce a full 25 rpm target for a 90 degree command because the current
0.25 rpm/degree heading gain requests 22.5 rpm at the initial error and then
reduces the target continuously.

| Speed cap | Peak target | Active target time | Peak measured wheel speed | Peak PWM | PWM steps >50 | Achieved turn | Final error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 rpm | 10.0 rpm | 5.45 s | 14.04 rpm | 604 permille | 1 | -89.046 deg | -0.954 deg |
| 15 rpm | 15.0 rpm | 4.73 s | 20.46 rpm | 604 permille | 1 | +89.270 deg | +0.730 deg |
| 25 rpm | 22.5 rpm | 4.58 s | 28.71 rpm | 605 permille | 0 | -89.076 deg | -0.924 deg |

All three smooth-pivot runs stopped automatically with actuator output
disabled, 100 Hz attitude updates, zero device deadline/I2C/encoder errors,
and final heading error below two degrees. Raising only the speed cap from 15
to 25 rpm saved 0.15 s because the proportional heading target begins slowing
immediately and the 8 rpm minimum dominates the final approach. A materially
faster pivot therefore requires a dedicated pivot acceleration/deceleration
profile rather than a still higher cap.

The next revision gave pivot mode its own 0.45 rpm/degree proportional gain
while leaving the normal straight-heading gain at 0.25 rpm/degree. This keeps
the 25 rpm target through more of the initial turn and delays the 8 rpm final
approach until the remaining error is approximately 18 degrees. The resulting
left 90 degree test achieved 89.247 degrees with 0.753 degree final error.
Active target time fell from 4.58 s to 3.47 s (24 percent faster), peak measured
wheel speed was 32.89 rpm, and peak PWM was 611 permille. The largest adjacent
PWM step was only 17 permille and there were no steps above 50 permille, so the
faster profile did not reintroduce the earlier pulse-recovery stutter. Device
Health remained clean and the actuator stopped automatically.

The user then identified the final approach as unnecessarily slow. Raising
only the pivot minimum from 8 to 12 rpm shortened active target time from
3.47 s to 2.96 s. The right 90 degree test achieved -90.432 degrees, an
overshoot of only 0.432 degree. Peak measured wheel speed was 31.61 rpm, peak
PWM was 606 permille, and the largest adjacent PWM step was 36 permille with
no steps above 50 permille. This 25 rpm maximum, 12 rpm minimum, and
0.45 rpm/degree pivot gain is the current fast-pivot baseline.

## Fast half-turn and deterministic braking

The pivot controller was extended to accumulate each wrapped 100 Hz yaw
increment instead of deriving progress only from the final wrapped yaw. This
removes the direction ambiguity at exactly +/-180 degrees. The host heading
test now also unwraps the complete attitude stream, so a positive turn that
crosses +180 to -180 is not incorrectly reported as a negative turn.

The faster half-turn profile uses a 35 rpm maximum, 15 rpm minimum, and
0.55 rpm/degree pivot gain. Two experimental final-approach strategies were
rejected:

- Rate-feedback reverse torque stopped with very low yaw rate but required
  8.62 s and produced repeated final corrections and visible high-frequency
  vibration.
- One-way targets below 2 rpm avoided reversal but still applied roughly
  54--56 percent PWM because of static-friction feedforward. It required
  6.37 s and still vibrated at the end.

The AT8236 chip datasheet page 5 and module manual page 10 both specify the
same input truth table: IN1=IN2=0 is coast/sleep, while IN1=IN2=1 drives both
outputs low and is the brake state. `BSP_Motor_Brake()` now encapsulates this
state. A pivot applies it to both motors for exactly eight 10 ms control cycles
after entering the 1.5 degree target window, then automatically returns both
inputs low. Any force-safe or timeout path also immediately returns both
inputs low.

The validated +180 degree ground test produced:

| Metric | Result |
| --- | ---: |
| Active drive time | 3.81 s |
| Nonzero target range | 15--35 rpm |
| Peak measured wheel speed | 42.86 rpm |
| Peak drive PWM | 608 permille |
| Drive PWM steps above 50 permille | 0 |
| Yaw/rate when braking began | 178.84 deg / 27.20 dps |
| Yaw/rate after 80 ms braking | 179.94 deg / -2.31 dps |
| Final achieved turn | 179.745 deg |
| Final error | 0.255 deg |
| Minimum battery voltage | 16.435 V |

Device Health, deadlines, I2C, and encoder error counters remained clean. The
actuator stopped automatically and the brake was released after its bounded
80 ms interval. This is the current fast half-turn baseline pending repeated
surface and payload validation.

## Bidirectional turns and tile-joint validation

The 35 rpm maximum, 15 rpm minimum, 0.55 rpm/degree pivot gain, 1.5 degree
stop window, and 80 ms AT8236 brake were retained. The user reported that the
resulting turn motion was acceptable and no longer showed the objectionable
high-frequency end vibration.

The car first moved approximately 13.5 cm so the front swivel caster would
encounter a tile joint. Positive and negative 90 and 180 degree pivots were
then run at that position. The right 360 degree pivot was also run there; the
left 360 degree result came from the immediately preceding run on the same
floor.

| Command | Achieved | Final error | Result |
| --- | ---: | ---: | --- |
| Left 90 deg at tile joint | +90.033 deg | -0.033 deg | pass |
| Right 90 deg at tile joint | -89.987 deg | -0.013 deg | pass |
| Left 180 deg at tile joint | +179.890 deg | +0.110 deg | pass |
| Right 180 deg at tile joint | -179.754 deg | -0.246 deg | pass |
| Left 360 deg | +359.945 deg | +0.055 deg | pass |
| Right 360 deg at tile joint | -359.985 deg | -0.015 deg | pass |

Every command stopped automatically and released the motor outputs. The
control and attitude sample rates remained approximately 100 Hz, I2C and
deadline counters stayed at zero, and the battery remained above 16.37 V.
One encoder ISR late event accumulated during the tile-joint series without
causing a control deadline miss or angle failure. Host-side wireless UART CRC
and sequence gaps remained visible, while the device-side Health and transport
counters remained clean.

## Straight-line low-speed recovery debounce

The 15 rpm positioning run used before the tile-joint pivots visibly vibrated
at high frequency. Its telemetry showed that the normal straight-line stall
recovery was classifying brief sub-1-rpm encoder intervals as stalls after
only 20 ms. It repeatedly replaced the PI output with approximately 604
permille recovery pulses. This was separate from the already corrected pivot
behavior.

The straight-line recovery confirmation interval was increased from 20 to
150 ms. Short caster and tile-joint disturbances now remain under continuous
PI control; a wheel that is actually stuck for 150 ms still enters the bounded
recovery path. Pivot mode continues to bypass this recovery path entirely.

| Metric, 15 rpm for 3 s | 20 ms debounce | 150 ms debounce |
| --- | ---: | ---: |
| Left average speed | 8.240 rpm | 14.621 rpm |
| Right average speed | 8.631 rpm | 15.064 rpm |
| Left speed maximum | 21.214 rpm | 16.607 rpm |
| Right speed maximum | 22.710 rpm | 17.143 rpm |
| Left/right frames near 604 permille | 54 / 36 | 7 / 7 |
| Left/right PWM steps above 50 permille | 37 / 25 | 4 / 5 |

The remaining high-output frames in the revised run occurred only during the
first approximately 0.45 s of startup. The final one-second averages were
14.621 and 15.064 rpm, both inside the 15 +/- 1 rpm steady-state gate. Device
Health, deadlines, I2C, encoder ISR, finite timeout, and trailing-safe output
checks were clean. The host result file reports failure only because wireless
UART gaps removed four of the expected 300 control frames; the command ACK,
device-side timeout, and post-command safe frames were all present.

## 10/40/60 rpm closed-loop shuttle

A short shuttle test used both wheel-speed PI loops and the gyro heading loop.
The available lane was approximately 1.0 m forward and 0.7 m rearward. Every
straight segment used a finite MCU-side timeout and was followed by an
automatic 180 degree pivot before the next reverse-direction segment.

The first 10 rpm run failed visibly and numerically. It averaged only 4.054
and 4.613 rpm and accumulated 8.330 degrees of yaw in 15 s. Telemetry recorded
144/158 frames near 600 permille, 97/146 PWM steps above 50 permille, and 20
right-wheel zero-output frames. Heading correction occasionally reduced one
wheel target below 8 rpm, causing the crawl bang-bang path to alternate with
the normal stall-recovery pulses.

All heading-controlled motion now stays on continuous wheel-speed PI. It no
longer enters crawl or pulse-recovery control. A separate one-second true-stall
timer stops and releases the motors if a commanded heading wheel remains below
the recovery speed at high output. Speed-only mode retains its bounded recovery
behavior, and pivot mode retains its validated continuous PI plus 80 ms brake.

The revised 10 rpm run had only the single normal startup PWM transition on
each wheel. No recovery pulse or zero-output chopping occurred afterward.

| Target and duration | Whole-run L/R | Final 1 s L/R | Estimated distance | Yaw change | Peak PWM L/R | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 40 rpm, 4 s | 37.610 / 37.507 rpm | 39.791 / 40.093 rpm | 0.511 m | -0.223 deg | 608 / 603 | pass |
| 10 rpm, 5 s, revised | 8.540 / 8.525 rpm | 10.017 / 10.123 rpm | 0.145 m | -0.018 deg | 569 / 560 | pass |
| 60 rpm, 3 s | 55.065 / 54.521 rpm | 60.224 / 58.980 rpm | 0.559 m | -0.671 deg | 617 / 602 | pass |

The whole-run averages include acceleration. All three final-one-second
averages met their speed gates. Battery minimums were 16.426, 16.394, and
16.410 V respectively. Device Health, I2C, deadlines, encoder ISR, and final
safe-output state were clean for the accepted runs.

The three automatic 180 degree pivots achieved 179.998, 180.088, and 180.095
degrees, with absolute errors of 0.002, 0.088, and 0.095 degrees. The car was
left stopped with motor outputs released after the final pivot.

## Encoder-distance closed loop

Distance commands now stop from accumulated encoder travel rather than elapsed
time. The 65 mm wheel circumference converts the left and right raw encoder
deltas independently, using each wheel's configured CPR and count sign. Center
travel is their average. Command duration remains a finite MCU-side safety
timeout only. The distance controller also reduces the base speed over the last
50 mm, holds the initial gyro yaw, applies the validated 80 ms active brake at
the target, and then releases both motor outputs.

The first 200 mm test exposed a straight-line heading weakness rather than a
distance-calculation error. Before the final approach had started, yaw had
already changed by 5.18 degrees; total motion yaw was 7.23 degrees. Left and
right wheel travel were 187.56 and 214.08 mm while their average stopped at
200.82 mm. The average-distance calculation revealed the wheel mismatch but
did not cause it.

Straight heading gain was increased from 0.25 to 0.55 rpm/degree and its
correction limit from 6 to 8 rpm. An initial same-condition comparison, with
the derivative gain still at 0.03 rpm/(degree/s), then passed:

| Metric | Original heading gain | Revised heading gain |
| --- | ---: | ---: |
| Command | 20 rpm / 200 mm | 20 rpm / 200 mm |
| Left travel | 187.56 mm | 204.16 mm |
| Right travel | 214.08 mm | 197.00 mm |
| Center travel | 200.82 mm | 200.58 mm |
| Yaw change | +7.225 deg | -1.479 deg |
| Active time | 3.31 s | 3.70 s |

Both runs stopped from encoder distance before the 10 s safety timeout. The
revised run met the +/-25/+80 mm distance gate and 2 degree heading gate.
Device Health, actuator release, deadlines, I2C, and encoder ISR diagnostics
were clean. DAPLink UART capture still showed host-side CRC and sequence gaps;
these did not appear in device-side control or transport diagnostics.

## Caster departure ramp and 1.8 m speed matrix

The first 30 rpm out-and-back distance test showed a direction-dependent
departure transient after a 180 degree pivot. One direction peaked at 5.74
degrees, crossed through the target to -2.30 degrees, and took approximately
7 seconds to settle. The opposite direction peaked at only 1.33 degrees under
the same firmware and command. This isolated the front swivel caster's initial
orientation as the disturbance; distance averaging was unchanged.

Distance mode now starts at 15 rpm and increases its base target by 0.1 rpm per
millimetre of encoder travel until it reaches the requested cruise speed. The
straight-heading derivative gain is 0.20 rpm/(degree/s), providing yaw-rate
damping while retaining the validated 0.55 rpm/degree proportional gain. In a
same-condition 30 rpm out-and-back repeat, the worst-direction departure peak
fell from 5.74 to 0.47 degrees. The other direction peaked at 1.29 degrees,
and the previous large correction overshoot disappeared.

The revised controller then completed 45, 65, and 10 rpm out-and-back runs.
Every straight leg used 1800 mm encoder distance and every turnaround used the
validated finite 180 degree pivot:

| Speed/direction | Center distance | Wheel difference | Final yaw | Peak absolute yaw | Active time | Cruise L/R |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 45 rpm out | 1800.75 mm | 3.98 mm | +0.007 deg | 0.921 deg | 14.67 s | 45.07 / 45.03 rpm |
| 45 rpm return | 1801.16 mm | 5.23 mm | +0.416 deg | 1.968 deg | 14.61 s | 45.07 / 45.03 rpm |
| 65 rpm out | 1800.46 mm | 9.65 mm | -0.086 deg | 0.736 deg | 12.30 s | 65.28 / 65.00 rpm |
| 65 rpm return | 1800.41 mm | 7.52 mm | -0.080 deg | 1.728 deg | 12.26 s | 65.26 / 65.09 rpm |
| 10 rpm out | 1800.68 mm | 3.42 mm | +0.067 deg | 2.345 deg | 53.52 s | 9.90 / 9.88 rpm |
| 10 rpm return | 1800.47 mm | 4.74 mm | -0.054 deg | 1.407 deg | 53.66 s | 9.88 / 9.86 rpm |

The 45 rpm runs held cruise speed for about 8.65 seconds and the 65 rpm runs
for about 4.65 seconds, proving that both reached the requested cruise speed
within 1.8 m. The 10 rpm safety timeout was extended to 60 seconds because its
ideal 1.8 m travel time is approximately 53 seconds; it remains a timeout, not
the stop criterion. All six distance legs stopped from encoder travel before
their timeout and released both motor outputs.

The speed profile intentionally treats requested rpm as a cruise ceiling. A
short move that lacks enough distance for both acceleration and braking uses a
triangular profile and may never reach that ceiling. Forcing 60 rpm in such a
move would trade away distance accuracy and stopping margin. Battery stayed
above 16.288 V. Device Health, I2C, and control deadline counters stayed clean.
The cumulative encoder ISR late counter reached 60 without a deadline miss or
observed control failure. Host-side DAPLink UART CRC and sequence gaps remain a
capture-path issue and are not present in device-side transport diagnostics.

## Direct forward/reverse caster transition

Pivot-then-forward testing does not reproduce the caster's most difficult
case. The larger disturbance occurs when chassis translation changes directly
from forward to reverse, or vice versa, while the body heading is unchanged.
The caster can remain exactly aligned in its previous direction as an unstable
leading wheel and continue rolling for a substantial distance before a floor
disturbance finally makes it swivel through approximately 180 degrees.

An initial +30/-30 rpm, 700 mm test demonstrated the measurement limitation.
The reverse leg ended at 700.58 mm encoder distance with 0.278 degree final yaw,
but the user observed that the caster did not actually swivel until late in the
move. Telemetry placed the main yaw disturbance at 4.06--4.94 seconds; by 4.0
seconds the wheels had already accumulated approximately 308 mm. Encoder
distance and final yaw therefore cannot, by themselves, prove equal chassis
translation while the caster is dragging or slipping.

The controller now remembers the last accepted distance-command direction. On
a direct sign reversal, the first 40 mm uses a signed 2 degree heading target,
then returns to the original heading. This small controlled asymmetry breaks
the caster's straight but unstable leading-wheel equilibrium near departure.
The normal 15 rpm departure profile, encoder-distance stop, gyro heading loop,
80 ms brake, and finite timeout remain active.

A same-condition +30/-30 rpm, 700 mm repeat passed. The user visually confirmed
that the caster began swivelling in the departure portion rather than near the
end. On the reverse leg, the first yaw extremum occurred at approximately 106.5
mm encoder travel and the return extremum at 202.9 mm, compared with the old
main disturbance after approximately 308 mm. Peak yaw stayed within about
/-1.2 degrees, final yaw was +0.092 degree, center encoder distance was 700.54
mm, and left/right wheel distance differed by 2.42 mm. Device Health, I2C,
control deadlines, command ACK, automatic stop, and final motor release were
clean.

This strategy makes the variable caster transition early and repeatable but
does not turn wheel encoders into an independent ground-distance sensor. Tasks
requiring accurate physical displacement while the caster is slipping still
need an external reference such as vision, optical flow, ranging to a fixed
surface, or a field landmark.
