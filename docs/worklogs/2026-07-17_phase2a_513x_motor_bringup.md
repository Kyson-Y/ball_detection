# Phase 2A MG513X Motor Bring-Up

Date: 2026-07-17
Profile: MG513X, 12 V, 0.36 A rated, 3.2 A stall, 1:28 gearbox
Encoder: GMR AB, 500 PPR, 3.3 V
Debug UART: COM7, 230400 8N1

## Firmware change

- 513X motor profile version: 5
- Electrical jog limit: 650 permille (65%)
- Common dual-motor start PWM candidate: 600 permille (60%)
- Closed-loop tuning: 100 rpm software limit, Kp=3, Ki=8, Kd=0
- Jog duration limit remains 1000 ms
- Closed-loop speed control is enabled for the provisional encoder CPR.
- Final HEX SHA-256: `B2287E3E460F7955D24B4E6BA04C3DF6BDFD31DDAED89D8A461E9B8C9E783CE8`
- Full FreeRTOS and application rebuild: 0 errors, 0 warnings
- Wireless DAP programming and verification: passed at 500 kHz
- Parameter-service defaults now come from the selected Motor Profile, so a
  reset restores the 513X PID values instead of MG370 values.
- Speed commands support bounded-duration operation and explicit continuous
  operation. Continuous motion stops only on a zero-speed command, reset, or
  a safety fault.
- Control telemetry is append-only extended to 96 bytes with both wheel
  targets, outputs, P/I/D/feedforward terms, active gains, and apply sequence;
  host tooling remains compatible with legacy 40-byte and 44-byte frames.
- Profile IDs are distinct: MG370=1, 513X=2, 513A=3, 513B=4. The 513A and
  513B selections remain compile-locked pending their own measurements.

## Bring-up evidence

The motor moved from a direct 3.3 V supply. With the AT8236 driver and 12 V
current-limited supply at 3 A:

- Left motor, +650 permille, 500 ms: encoder response detected, 41,256 total
  absolute counts, Health clean.
- Left motor, -650 permille, 500 ms: encoder response detected, 43,899 total
  absolute counts, Health clean.
- Both motors, left +650 / right -650, 500 ms: 41,512 / 10,189 total absolute
  counts, both encoder responses detected, Health clean.
- Both motors, left +650 / right -650, 1000 ms: 100 active frames, 76,865 /
  18,623 total absolute counts, Health clean, CRC/gap/deadline all zero.

For the 1000 ms dual-motor test, the active-window average was approximately
65.3 rpm left and 65.7 rpm right; the final 200 ms was approximately 72.3 rpm
left and 71.0 rpm right. The provisional CPR values remain 56,000 left and
14,000 right. The right encoder sign remains -1 so forward chassis motion is
normalized positive.

Artifacts:

- `tests/artifacts/phase2a-pulse-20260717-135757`
- `tests/artifacts/phase2a-pulse-20260717-135834`
- `tests/artifacts/phase2a-pulse-20260717-135909`
- `tests/artifacts/phase2a-pulse-20260717-140041`

## Current conclusion

The previous no-motion result at 50% was a power/driver startup margin issue,
not a motor or encoder failure. At 65%, both MG513X motors start and run in
both directions with stable telemetry. The common startup threshold is between
57% and 58%; the profile uses 60% for margin.

## Closed-loop evidence

- 60 rpm / 10 s: 60.036 / 60.000 rpm, start skew 0 ms.
- -60 rpm / 8 s: -60.037 / -60.004 rpm, start skew 0 ms.
- 30 rpm / 8 s: 30.136 / 30.763 rpm.
- 70 rpm / 10 s: 69.919 / 70.016 rpm.
- 70 rpm / 30 s after reset: 70.014 / 70.012 rpm, 3000/3000 control frames,
  start skew 0 ms, and profile-default PID confirmed without UART overrides.
- 30 -> 70 rpm: t90 320 / 260 ms, overshoot 2.3% / 1.8%.
- 70 -> 30 rpm: t90 750 / 580 ms, final 29.953 / 29.944 rpm.
- 10 rpm / 10 s: 9.688 / 10.007 rpm.
- 5 rpm crawl / 10 s: 4.279 / 4.281 rpm average; motion is intentionally
  pulsed and is not a smooth position-hold mode.

All listed tests completed with clean Health, CRC, sequence, deadline, and
encoder-late diagnostics. Multi-revolution CPR calibration and loaded testing
remain required before position or odometry claims.

Final long-run artifact:

- `tests/artifacts/phase2a-speed-20260717-143659`

The 70 rpm measurements above remain the highest completed moving validation.
The later v5 change raises only the command limit to 100 rpm and adds continuous
speed/extended telemetry support; its final board check was static at 0/0 rpm
with clean 100 Hz telemetry and zero output.

## Wheel inertia and disturbance tests

With the wheels installed and the chassis still suspended:

- 30 -> 70 rpm: t90 280 / 250 ms, skew 30 ms, overshoot 2.59% / 2.89%,
  tail 70.032 / 70.001 rpm.
- 70 -> 30 rpm: t90 710 / 520 ms, skew 190 ms, undershoot 12.86% / 12.87%,
  tail 29.999 / 30.036 rpm.

The valid left-wheel disturbance run used a 60 rpm target. Baseline was
60.030 / 59.998 rpm with left output 624.2 permille. Manual load reduced the
left wheel to 18.853 rpm (68.59% drop) and saturated left output at 650
permille. The untouched right wheel stayed at 59.947 rpm average during the
event. After release, the left wheel returned to the target +/-3% band in
230 ms, output returned to baseline +/-5 permille in 200 ms, and peak speed was
63.006 rpm (5.01% overshoot). Health, CRC, sequence, deadline, and encoder-late
diagnostics remained clean.

Artifacts:

- `tests/artifacts/phase2a-step-20260717-144917`
- `tests/artifacts/phase2a-step-20260717-145040`
- `tests/artifacts/phase2a-speed-20260717-145520`

Three attempted right-wheel disturbance captures did not contain a measurable
right-wheel speed reduction and are not accepted as disturbance evidence.

A follow-up isolated right-channel pulse used right -650 permille for 500 ms.
The left encoder remained at zero and the right encoder produced 8,869 total
absolute counts. Electrical and encoder channel mapping is therefore correct.
The user's visible right-wheel slowdown without an encoder slowdown indicates
mechanical slip downstream of the motor-side encoder: wheel hub/coupler, tire
on rim, output attachment, or the gearbox. Straight-line validation is blocked
until this mechanical discrepancy is resolved.

That preliminary conclusion was superseded after control telemetry was extended
from 40 to 44 payload bytes to append `right_auxiliary`, the right normalized
PWM. The parser accepts both legacy and current control payloads.

The valid right-wheel disturbance baseline was 59.996 rpm and 631.6 permille.
Manual load reduced the right wheel to 27.857 rpm (53.57% drop) and saturated
right output at 650 permille. The untouched left wheel stayed at 59.956 rpm
average. After release, the right wheel returned to target +/-3% in 150 ms,
right PWM returned to baseline +/-5 permille in 150 ms, and peak speed was
63.429 rpm (5.71% overshoot). Health, CRC, sequence, deadline, and encoder-late
diagnostics remained clean.

Artifact:

- `tests/artifacts/phase2a-speed-20260717-151455`

## Stage close

The user ended the 513X stage after direction, encoder, startup PWM, speed PI,
step response, long-run, wheel-inertia, and bilateral disturbance tests passed.
The final firmware reports zero actuator output after every test. 513A and
513B are reserved for later independent bring-up and remain compile-locked.
