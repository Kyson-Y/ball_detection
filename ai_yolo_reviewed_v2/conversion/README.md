# MaixCAM Pro conversion

The ONNX must be compiled for the CV181x NPU. Renaming `.onnx` is not a
conversion.

Upload these files to <https://maixhub.com/toolbox/convert/maixcam>:

- `ball_yolo11n_320x96_opset17.onnx`
- `calibration_100_ball80.zip` (80 ball, 20 empty; use this first)

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

Task 7405 converted successfully with `calibration_100_ball80.zip`; its renamed
deployment files are already committed under `models/`.

The repeatable PC-side commands are implemented in `export_for_maixcam.py` and
`prepare_calibration.py`. The exporter intentionally requires Ultralytics
`8.4.104` and ONNX opset 17, matching the Sipeed online converter guide.

If the INT8 comparison fails only at
`/model.23/Sigmoid_output_0_Sigmoid`, retry the same ONNX with
`calibration_100_ball100.zip`. Do not repeat the old 50-ball/50-empty
`calibration_100.zip`; it produced a classification maximum of `0.259843`
instead of the FP32 reference `0.932613` in task 7399.
