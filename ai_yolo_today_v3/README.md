# AI YOLO today v3

This folder is the MaixCAM Pro release for the final `640x128` pipe ROI. It
keeps the proven UART0 packet, continuity gate, alpha-beta filter, launcher
ownership, and asynchronous low-rate web preview from `ai_yolo_reviewed_v2`.

## Current result

- Raw evaluation: 455 ball frames with 0 misses; 143 no-ball frames with 0
  false positives at confidence thresholds `0.25`, `0.50`, and `0.70`.
- 94 manually reviewed motion frames: median x-center error `0.34 px`, p95
  `1.67 px`, maximum `3.58 px` after ONNX export.
- Model: YOLO11n, RGB input `1x3x64x320`, ONNX opset 12.
- Board result on 2026-07-31: sustained detection and UART rates of about
  `59-60 Hz` with the asynchronous web preview enabled, `uart_errors=0`, and
  roughly `1 ms` NPU inference time. This exceeds the `30 Hz` requirement.

Reports are in `reports/`. The MaixHub input model and INT8 calibration ZIP
are in `conversion/`.

## Per-frame pipeline

1. Read `640x480` RGB at a requested 60 FPS.
2. Crop the final ROI `(x=0, y=176, width=640, height=128)`.
3. Resize directly to `320x64`; this preserves the ROI aspect ratio.
4. Run `maix.nn.YOLO11` on the NPU with dual buffering.
5. Filter class, box size, aspect ratio, and image bounds.
6. Associate multiple candidates with the previous position and velocity;
   reject ambiguity and physically impossible jumps.
7. Map model x back to the 640-pixel ROI, subtract provisional origin
   `x=320`, and convert with `0.390625 mm/px`.
8. Update the alpha-beta position/velocity filter only when the measurement is
   valid, then send one CRC-protected 22-byte UART packet.
9. Update `/status.json`; encode at most 2 JPEG previews per second in a
   background thread so the web page cannot set the detection rate.

Rejected, ambiguous, or missing detections send `DETECTED=0`, position and
velocity zero, and `control_valid=false`. They are never guessed into valid MCU
commands.

## Runtime files

- `app/maixcam_ai_ball.py`: camera, NPU, tracking, UART, status, and thermal loop.
- `app/ai_ball_detector.py`: candidate filtering and continuity association.
- `app/ball_uart_protocol.py`: 22-byte packet, CRC16, and alpha-beta filter.
- `app/status_server.py`: web UI, `/status.json`, and `/frame.jpg`.
- `app/preview_encoder.py`: non-blocking 2 FPS JPEG worker.
- `config/ai_ball.json`: all geometry, thresholds, calibration, UART, and web settings.
- `scripts/deploy_release.py`: immutable release upload, activation, and autostart.
- `README_MCU_UART_CN.md`: MCU fields, flags, CRC, C decoder, and receiver rules.

## Hardware and outputs

- MaixCAM `A16/UART0_TX` -> MSPM0 `PB16/UART2_RX`
- MaixCAM `A17/UART0_RX` is reserved; current data flow is one-way.
- Common GND, `115200 8N1`, 3.3 V TTL.
- Web: `http://10.5.66.1:8080/`
- Status: `http://10.5.66.1:8080/status.json`

The UART layout is unchanged: header `AA 55`, version 1, type 1, fixed length
22, little-endian fields, and CRC16-CCITT-FALSE. The MCU must validate CRC,
`DETECTED`, `CALIBRATION_VALID`, packet age, and receive timeout before using
position or velocity.

## Tuning order

1. Measure `detect_rate`, `data_rate`, `inference_ms`, and temperature on the board.
2. Recalibrate only `center_x_px`, `mm_per_pixel`, and possibly `position_sign`.
3. Tune `confidence_threshold` only from real false-positive/miss evidence.
4. Tune continuity speed limits from measured physical motion, not to hide bad detections.
5. Keep preview FPS low until the visual and UART rates remain above 30 Hz.

Do not change the MCU packet while tuning the detector. Do not deploy the ONNX
file directly; MaixCAM requires the MaixHub-generated `.mud` and `.cvimodel`.
