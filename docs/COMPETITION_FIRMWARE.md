# Competition firmware handoff

This branch is the formal installed-513X chassis baseline. The heading loop,
distance loop, wheel speed loop, MPU6050 calibration, voltage compensation and
caster reversal handling are kept from the validated chassis closeout. The new
competition layer only schedules those existing actuator requests.

## Build

Use `tools\build_echo.ps1 -Mode All` from PowerShell. The application project
is `keil\ECHO.uvprojx`; the last 1 KiB of the 128 KiB main flash is reserved
for settings at `0x0001FC00`. Do not change the scatter file back to `0x20000`
unless the storage address is moved as well.

The formal serial ownership is:

| Resource | Pins | Use |
| --- | --- | --- |
| UART0 | PA10/PA11 | DAPLink/debug telemetry |
| UART1 | PA8/PA9 | Lidar reservation |
| UART2 | PB15/PB16 | ESP32 link, DMA RX/TX |
| UART3 | PB2/PB3 | ZDT generation-2 backup |

Generation-1 ZDT protocol code remains available, but its old transport is not
enabled together with the formal ESP32 UART2 link.

## OLED pages and buttons

The five physical keys map to `UP`, `DOWN`, `LEFT`, `RIGHT`, and `OK`.
Short press, long press and held repeat are decoded by `ui_input`. Any key while
a task, test countdown or test motion is active enters the emergency-safe path.
The elapsed time freezes at the stop event.

The four pages are `MAIN`, `TEST`, `TUNE`, and `DIAG`. Left/right changes page
when the car is safe. `MAIN` selects H2--H6 with up/down and starts the selected
formal task immediately with a short `OK` press. During a formal task the OLED
is a dedicated timing screen: the task/state stays on the top line and the
largest fitting `SS.mm` timer occupies the display body. A stopped result keeps
the frozen time until `OK` acknowledges it.

`TEST` retains the configurable distance/heading test, start delay, task slot,
and advanced KP/KI/KD/TARGET settings. Test runs keep the existing diagnostic
data pages instead of switching to the formal full-screen timer. `TUNE` shows
the final normalized motor PWM and control terms. `DIAG` contains the IMU view,
IMU reset, runtime resource diagnostics, and peripheral scan.

The default test values are:

```text
DIST  +1000 mm    SPD 40.0 rpm
ANG   +90.0 deg   TRN 35.0 rpm
DELAY 3 s
```

The default 35 RPM pivot limit is the value used by the last successful ground
turn tests. It is constrained to 15--35 RPM and cannot change while output is
permitted. Mission slots are deliberately callback-based so the actual contest
task can be registered without changing the display or emergency-stop path.

## Persistence and safety

`competition_storage.c` stores the normal competition settings and the hidden
KP/KI/KD/TARGET values in a versioned record containing magic, size,
generation and CRC-32. It erases and programs the reserved sector only when
the chassis output is disabled, verifies the complete record, and reports the
result on `SYS`. Invalid records fall back to the compiled defaults. Settings
are saved after editing settles for 750 ms rather than on every repeat event.

At boot `ChassisActuator_Init` and `CompetitionService_Init` leave
`armed=0` and `output_permitted=0`. A motor request can only be staged by a
registered mission callback or the configurable `TEST` action after its
countdown. Any physical key, injected UI event, rejected request, actuator
fault or mission stop enters the safe path.

## Current H-problem state

The five registered formal slots are `H2 LINE LAP`, `H3 BALL STEP`,
`H4 AB CENTER`, `H5 LAP CENTER`, and `H6 LAP HOLD`. Their current callbacks are
safe placeholders for validating task selection, timing and emergency stop:
they never grant chassis output and never select or command ZDT. Replace each
callback body as its real H-task state machine is implemented; do not bypass the
competition service safety and timer path.

The installed diagnostics report battery, MPU6050, encoders, OLED/I2C,
eight-channel reflectance, ESP32, lidar reservation, both ZDT generations and
camera-link placeholders.

## Validation on 2026-07-29

- Full FreeRTOS/application rebuild: 0 errors, 0 warnings.
- Program/readback passed on CMSIS-DAP `2e4c7219`; image SHA-256
  `AB99C4F8D7CA94CC10922D2A5EF06B6AD350C29DEB18764E08F6099CA06C2F7F`.
- Five-second runtime capture: control 100 Hz, IMU 100 Hz, no CRC/sequence/I2C
  error, no deadline miss, OLED online, display stack minimum 286 words.
- Automated H6 run: entered `RUNNING`, reached 1.606 s with output permission
  still zero, then an injected arbitrary key produced `ABORTED` at 1.632 s.
  The value remained 1.632 s after another 700 ms and the H6 stop callback ran.
- Captured OLED framebuffer showed `H6 RUN` and an unclipped large `SS.mm`
  value with no unsupported glyphs.

## Validation on 2026-07-28

- Full FreeRTOS library and application rebuild: 0 errors, 0 warnings.
- Application size: Code 96440, RO 3532, RW 188, ZI 21920 bytes.
- Main-flash HEX maximum address: `0x0001873F`; no data overlaps the
  `0x0001FC00--0x0001FFFF` settings sector.
- DAPLink programming and byte-for-byte readback passed; image SHA-256 was
  `AEE9DAE22809E3E082DCDE6A4465C145AB7AC66A2FB9047996F15B50E4C63FFA`.
- Runtime inspection after 1.2 s showed System, Service and Display tasks all
  advancing, OLED initialized and refreshed, `initialized=1`, `armed=0`, and
  `output_permitted=0`.
- Injected UI events produced `READY -> ARMED -> COUNTDOWN -> RESULT(EMPTY)`
  with no motor request. A key during countdown produced `ABORTED` and the
  emergency safe state.
- A settings record was written with CRC, loaded after reset, changed back to
  the default task slot, written as generation 2, and loaded again after reset.
