# AI YOLO reviewed v2

This directory is the self-contained MaixCAM Pro YOLO11 replacement for the
pixel-aligned empty-pipe difference detector. It keeps the existing MCU wire
protocol and web port.

## Runtime path

1. Capture `640x480` at a requested `60 FPS`.
2. Crop `(0, 144, 640, 192)`, the same color ROI used for training.
3. Resize to the fixed `320x96` RGB model input.
4. Run `nn.YOLO11(..., dual_buff=True)` on the NPU.
5. Reject invalid box sizes, ambiguous targets, and physically impossible
   position jumps.
6. Map the accepted center back to the 640-pixel pipe axis. The provisional O
   point is `x=320`; `0.390625 mm/px` maps 640 pixels to 250 mm.
7. Apply the existing alpha-beta position/velocity filter.
8. Send the unchanged 22-byte CRC16 state packet through UART0.
9. Serve `/status.json`, `/frame.jpg`, and `/` on port 8080. JPEG encoding is
   asynchronous and limited to 2 FPS.

An uncertain frame is sent with `DETECTED=0`, zero position/velocity, and
`control_valid=false`. A guessed position must never become a valid MCU input.

## Hardware contract

- MaixCAM Pro UART0 TX: `A16`
- MaixCAM Pro UART0 RX: `A17`
- Baud: `115200 8N1`
- Web: `http://10.5.66.1:8080/`
- Status: `http://10.5.66.1:8080/status.json`

The UART frame remains `AA 55`, version 1, type 1, 22 bytes total, little
endian fields, and CRC16-CCITT-FALSE. See `app/ball_uart_protocol.py`.
The MCU wiring, byte layout, validity rules, CRC implementation, reference C
decoder, and acceptance checklist are in
[`README_MCU_UART_CN.md`](README_MCU_UART_CN.md).

## Model files

Training result:

- 48/48 validation balls detected and 48/48 empty frames clean.
- 56/56 test balls detected and 47/47 empty frames clean.
- Test maximum center error: about 2.80 pixels at the 640-pixel ROI scale.

MaixHub task 7405 produced the deployed CV181x model. Its CVIModel SHA-256 is
`14b077d119cc4826833b443d2e3affb997546eecb708132ae94fbe2cd3d2a9ce`.
The first board run sustained about `45.9 Hz` with UART and the 2 FPS web
preview enabled. MaixCAM cannot execute the ONNX file directly; use the `.mud`
and `.cvimodel` pair in `models/`.

## Tuning order

1. Verify sustained `detect_rate >= 30` with UART and web preview enabled.
2. Tune `detector.confidence_threshold` for false positives versus misses.
3. Tune box limits only after inspecting real NPU boxes.
4. Tune `continuity_gate.max_speed_model_px_s` from measured physical motion.
5. Replace provisional `center_x_px` and `mm_per_pixel` after mechanical
   calibration.

Do not lower continuity limits merely to hide rejected jumps. Inspect
`rejection_reason`, `candidate_count`, `rejected_jumps`, `inference_ms`, and
`preprocess_ms` in `status.json` first.

## Deployment

The production app ID remains `ball_detection_control`, so the launcher daemon
continues to own the only camera process. `maix_app/main.py` loads the immutable
release selected by `/root/ball_detection/current`. Keep the previous release
for rollback and stop a running app only with `SIGTERM`.
