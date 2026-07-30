# MaixCAM Pro conversion

The ONNX must be compiled for the CV181x NPU. Renaming `.onnx` is not a
conversion.

Upload these files to <https://maixhub.com/toolbox/convert/maixcam>:

- `ball_yolo11n_320x96_opset17.onnx`
- `calibration_100.zip`

Use these values:

- Model type: `YOLO11 Detect`
- Target: `MaixCAM` (this includes MaixCAM Pro)
- Calibration source: uploaded ZIP
- Label: `ball`
- Input: fixed RGB `1x3x96x320`

Download the result and rename only when necessary so the final pair is:

- `models/ball_yolo11n_reviewed_v2.mud`
- `models/ball_yolo11n_reviewed_v2.cvimodel`

Check that the MUD points at the final `.cvimodel` filename. Do not deploy the
ONNX file to the camera.

The repeatable PC-side commands are implemented in `export_for_maixcam.py` and
`prepare_calibration.py`. The exporter intentionally requires Ultralytics
`8.4.104` and ONNX opset 17, matching the Sipeed online converter guide.
