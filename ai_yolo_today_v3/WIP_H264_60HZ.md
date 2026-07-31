# H.264 + 60 Hz UART resume point (2026-07-31)

This file records the paused media experiment. The production test was moved
back to the no-stream release after this checkpoint.

## Required result

- Camera measurements as close to the sensor's real 60 FPS as possible.
- Exactly 60 scheduled UART packets/s on UART0 (`A16 TX`, `A17 RX`, 115200).
- Video has lower priority and may use very low resolution.

## Measurements already made

- No RTSP: about 59.2-59.8 detection packets/s, `uart_errors=0`.
- RTSP parent `320x240`, RTSP configured at 15 FPS: the camera graph was gated
  to about 15.15 detection packets/s. Do not use this combination.
- RTSP `320x240@60`, 600 kbit/s, active client: about 57.40 measurements/s.
- RTSP `160x120@60`, 300 kbit/s, active client: about 57.86 measurements/s;
  status-only instantaneous rates were about 59 Hz.
- Requesting camera 65 FPS selects the sensor's 80 FPS mode and reduced the
  complete loop to about 57 Hz. Keep the camera request at 60 FPS.
- MaixCAM's current `maix.rtsp.Rtsp.write()` raises `ERR_NOT_IMPL`; manual
  15 FPS frame injection into the built-in RTSP server is unavailable.

## Saved but not yet deployed

`app/maixcam_ai_ball.py` contains a dedicated 60 Hz UART scheduler. Detection
publishes the latest measurement to the scheduler. A repeated valid sample is
marked `PREDICTED`; velocity extrapolation is limited to 25 ms, and data older
than 50 ms is sent invalid. The 22-byte packet layout is unchanged.

Before production use, deploy this branch and verify over at least 30 seconds:

1. `uart_packets` delta / elapsed time is 59.8-60.2 Hz.
2. `detect_rate` remains near the camera's actual rate with an RTSP client.
3. `uart_errors=0`, sequence increments continuously, and CRC is valid.
4. With a ball present, inspect `PREDICTED`, `age_ms`, jumps, and dropouts.
5. Reboot once and verify launcher ownership and autostart.

The RTSP load probe is `scripts/rtsp_load_probe.py`. The status endpoint is
`http://10.5.66.1:8080/status.json`, and the stream is
`rtsp://10.5.66.1:8554/live`.
