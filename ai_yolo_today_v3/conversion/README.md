# MaixHub conversion

Upload both files in this directory to the MaixCAM converter:

- `ball_yolo11n_today_v3_320x64.onnx`
- `calibration_today_v3_ball80_empty20.zip`

Select `YOLO11 Detect` and target `MaixCAM` (also used for MaixCAM Pro). The
fixed RGB input must be `1x3x64x320`; label is `ball`, mean is `0,0,0`, and
scale is `0.00392156862745098` for all channels.

After conversion, place the pair here:

- `../models/ball_yolo11n_today_v3.mud`
- `../models/ball_yolo11n_today_v3.cvimodel`

The MUD must reference exactly `ball_yolo11n_today_v3.cvimodel`. ONNX cannot
run directly on the MaixCAM NPU.
