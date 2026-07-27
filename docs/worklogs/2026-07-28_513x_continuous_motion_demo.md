# 2026-07-28 513X continuous motion demonstration

Nature: chassis-stage closeout

## Scope

- Freeze the installed 513X-4S/GMR speed, heading, distance, voltage
  compensation, MPU6050 attitude, and caster-departure behavior after the
  2026-07-27 ground tuning.
- Add a host-side continuous demonstration sequence without adding a mission
  state machine to production firmware.
- Do not retune direct forward/reverse caster behavior in this closeout.
- Defer early completion of settled pivot commands because the demonstration
  is not a production mission, while recording the issue for future autonomous
  sequencing.

## Starting state

- Development worktree: `C:\Users\Auror\ECHO-513a-work`.
- Branch: `codex/513a-motor-bringup`.
- HEAD before this closeout: `962867de130ebab1aee348bae164eb21728e52f8`.
- Formal `E:\ECHO` main remained at `4b1a3db` and was not modified by the
  chassis tests.
- DAPLink UART was `COM18`, 230400 8N1. Wheel diameter was 65 mm.

## Demonstration sequence

`tools/chassis_demo_rectangle.ps1` keeps COM18 open and sends the next command
after five inactive 100 Hz control frames plus a 120 ms host gap. Each MCU
command retains a finite timeout. The final script performs:

1. In-place right 360 degrees.
2. Forward 1600 mm at 60 rpm.
3. In-place right 90 degrees.
4. Forward 500 mm at 40 rpm.
5. In-place right 90 degrees.
6. Forward 1600 mm at 60 rpm.
7. In-place right 90 degrees.
8. Forward 500 mm at 40 rpm.
9. In-place left 360 degrees.
10. In-place right 90 degrees.
11. Forward 500 mm at 40 rpm.
12. Direct reverse 500 mm at 40 rpm.

Successful completion and every failure path send an explicit zero-speed
command before closing the serial port. The user can still request an abort;
the host process must then be terminated and a zero-speed command sent after
COM18 is released.

## Ground results

The first attempt omitted step 10. The first eight path segments and both
360-degree pivots were accurate, but the final forward move pointed toward a
wall. Contact rotated the chassis about -92.2 degrees and raised peak PWM to
660 permille. This was a sequence-definition error, not a distance or heading
controller failure. Step 10 was added before the accepted repetitions.

Two complete 12-motion runs then passed. Encoder-speed integration and the
100 Hz attitude estimator produced:

| Metric | Accepted range or worst case |
| --- | ---: |
| 1600 mm center travel | 1600.8--1601.3 mm |
| 500 mm forward center travel | 500.3--500.8 mm |
| 500 mm reverse center travel | -500.8 mm |
| 90 degree pivot error | <= 0.46 deg |
| 360 degree pivot error | <= 0.39 deg |
| Normal straight final yaw error | <= 0.27 deg |
| Normal straight yaw span | <= 1.60 deg |
| Direct reverse final yaw, run 1 / run 2 | -4.10 / +0.08 deg |
| Battery range | 16.256--16.508 V |
| Control deadline / I2C / active-sticky Health | 0 / 0 / clean |

The second accepted run ended with `ActuatorOutputPermitted=0`, a matched
zero-speed ACK, MPU state READY, and both motors released. Host-side wireless
DAPLink captures still contained CRC and sequence gaps, while device-side
serial drops, control deadlines, I2C errors, and Health issues remained zero.
Raw captures remain under ignored `tests/artifacts/rectangle-*` directories
and are not committed.

## Deferred improvements

- Pivot targets settle before the command timeout, but heading mode does not
  release the command after its 80 ms brake. In the accepted run, 90-degree
  targets reached zero at 2.36--2.39 s and then waited about 2.67 s; 360-degree
  targets reached zero at 6.71 s and waited about 5.33 s. A future autonomous
  mission should complete and release a settled pivot exactly as distance mode
  does. The user explicitly deferred this change for the demonstration.
- The signed 2-degree, first-40-mm caster trigger makes direct reversal start
  predictably, but the two complete runs still differed in final reverse yaw.
  The user explicitly deferred further caster tuning.
- Right-encoder late IRQ events accumulate occasionally at high edge rate.
  They did not produce a sustained Health issue or measurable distance error;
  later work may review interrupt latency and long critical sections.
- Formal acceptance of ground displacement independent of wheel slip still
  requires an external reference such as vision, ranging, or a field marker.

## Verification and repository state

- The final 2026-07-28 FreeRTOS and ECHO full rebuild passed with 0 errors and
  0 warnings. App size was Code=91080, RO-data=3552, RW-data=188,
  ZI-data=19876.
- `git diff --check` passed.
- No new controller parameters were introduced by the demonstration.
- The accumulated chassis, IMU, ESP UART2, pin-relocation, documentation,
  tests, and tools were committed locally as
  `46b509a feat: complete assembled 513x chassis integration`. The branch has
  not been pushed.
- The formal main worktree contains pre-existing user modifications and is not
  ready for an automatic fast-forward. Do not merge or overwrite it until
  those files are reconciled.

## Next stage

- Close the 513X integration branch without changing the deferred pivot or
  caster behavior.
- Revisit the previously verified ZDT/"Zhang Da Tou" Emm backup stepper on the
  production PCB. The old generation-1 mapping `PB15/PB16` now conflicts with
  the formal ESP32 UART2 link. Start with the generation-2 `PB2/PB3` path, one
  motor, read-only queries, and then a bounded low-speed/small-angle command.
