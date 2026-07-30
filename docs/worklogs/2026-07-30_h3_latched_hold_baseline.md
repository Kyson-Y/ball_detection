# H3 Latched-Hold Formal Baseline

## Status

This is the current formal H3 baseline. Further H3 experiments must branch
from this revision and retain a direct rollback path to it.

## Verified Run

- Capture: `tests/artifacts/h3_latched_hold_run1_20260730-193508`
- Completion time: `5.785 s`
- Final position: about `-52.3 mm`
- Post-completion hold: `-51.9 mm` to `-52.8 mm` for `3.84 s`
- Latched motor output: `+1.06 deg`

## Firmware Identity

- Image: `keil/Objects/ECHO.hex`
- SHA-256: `0CE4E3E5091390B7F13DE811E64EDF68226C98ACB850934FED9486524A76E060`
- Flash method: CMSIS-DAP serial `2e4c7219`, `500 kHz`

## Required Behavior

- Capture the Zhang Datou current position as a fresh `theta0` for every H3 run.
- Hold the actual terminal angle at completion; do not automatically return to center.
- Preserve the original positive-to-negative search and braking sequence.
- Do not reintroduce the rejected positive `70 mm/s` limit or the direct
  positive-to-negative tracking shortcut without a separate, measured trial.
