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
