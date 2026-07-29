# H ball competition UI skeleton, 2026-07-29

## Scope

- Added five registered H2--H6 mission placeholders.
- Reduced navigation to `MAIN -> TEST -> TUNE -> DIAG`.
- Made formal runs use a dedicated large `SS.mm` timer screen.
- Preserved test diagnostics and the existing control tuning views.
- Preserved immediate any-key emergency stop and frozen result time.

The placeholder missions deliberately keep chassis output safe and do not
select or command either ZDT backend. They exist only to validate competition
workflow before each real task state machine is implemented.

## Hardware validation

Target: the board reporting on `COM18`, CMSIS-DAP serial `2e4c7219`.

| Check | Result |
| --- | --- |
| Full rebuild | 0 errors, 0 warnings |
| Flash readback | SHA-256 matched |
| Formal task start | H6 entered `RUNNING` immediately |
| Timer | 1.606 s while running; 1.632 s after stop |
| Frozen time | Still 1.632 s after another 700 ms |
| Emergency stop | Arbitrary key produced `ABORTED` |
| Chassis output | Permission stayed zero throughout |
| H6 callbacks | Start/service/stop counters advanced |
| OLED framebuffer | `H6 RUN` plus unclipped large `SS.mm` |
| Runtime health | 0 I2C error, 0 deadline miss, OLED online |
| Display stack | 286 words minimum free |

The target was reset after debugger validation and left in the normal READY,
output-locked startup state.

## Generation-2 ZDT qualification for the H mechanism

The bare generation-2 X42S/Emm motor was connected to UART3 and qualified
before installing the linkage or pipe. The H-problem limits used for this
assessment were a `+5 cm -> -5 cm` ball move within 5 s and ball-position error
within 1 cm. Motor angle tracking is only the inner actuator requirement; the
complete ball requirement still needs the camera, linkage and pipe.

- Device address `0x01`, firmware `200`, hardware `8970`, 115200 8N1.
- A relative `+15 deg` command measured `+14.952 deg`; return-to-baseline error
  was `0.011 deg`.
- Positive and negative speed feedback reached `+29/-29 rpm`; explicit stop
  and the 1.5 s speed lease both returned speed to zero.
- For a `+/-5 deg`, 1 Hz sine target, requested/effective update rates and
  tracking errors were:

| Requested | Effective | RMS error | Maximum error | Strict rate result |
| --- | --- | --- | --- | --- |
| 20 Hz | 20.0 Hz | 1.507 deg | 2.342 deg | pass |
| 30 Hz | 27.7 Hz | 1.345 deg | 2.348 deg | pass |
| 50 Hz | 43.7 Hz | 1.125 deg | 1.925 deg | below 90% rate threshold |
| 100 Hz | 89.7 Hz | 0.899 deg | 1.622 deg | below 90% rate threshold |
| 200 Hz | 151.7 Hz | 0.873 deg | 1.585 deg | below 90% rate threshold |

All tests returned exactly to the captured center, with zero UART timeout,
invalid response, stall or stall-protection event. The useful H-control choice
is a 30 Hz vision output loop with motor targets allowed to refresh at 50 Hz.
Higher request rates add little angular accuracy and should not be the initial
competition setting. The final state was backend deselected, motor disabled,
motion inactive and speed zero.
