# Codex Board Handoff

Read this file before changing or starting anything on MaixCAM Pro. The vision
process owns shared VI/ISP/VB media resources, so a process that appears to run
normally can still be incorrectly launched and lose most of its frame rate.

## Non-Negotiable Rules

1. Only one process may own the camera.
2. The production app must be a child of `launcher_daemon`.
3. Never start production with direct SSH `python3`, `nohup`, a background shell,
   or `scripts/maixcam_ball_control_launcher.sh` while `launcher_daemon` exists.
4. Before sending a signal, verify PID, executable path, complete command line,
   PPID, parent executable, and that exactly one matching process exists.
5. Use `SIGTERM` only. Never use `SIGKILL`.
6. Never trigger empty-pipe calibration unless the user explicitly says to
   calibrate after physically preparing the empty pipe.
7. Do not overwrite `/root/ball_detection/runtime/`; it contains the live
   device-specific reference and calibration history and is intentionally not
   stored in Git.

## Known-Good Runtime

- App ID: `ball_detection_control`
- App entry: `/maixapp/apps/ball_detection_control/main.py`
- Release link: `/root/ball_detection/current`
- Persistent runtime: `/root/ball_detection/runtime`
- Autostart file: `/maixapp/auto_start.txt`
- Web status: `http://<board-ip>:8080/status.json`
- Control UART: `/dev/ttyS0`, `UART0`, `A16=TX`, `A17=RX`, `115200 8N1`
- Expected process command:

  ```text
  /usr/bin/python3 /maixapp/apps/ball_detection_control/main.py
  ```

- Expected parent executable:

  ```text
  /maixapp/apps/launcher/launcher_daemon
  ```

While production runs, the launcher UI must not also be running. With the fixed
ROI detector, UART, and no active preview client, measured control rate is about
`40-46 Hz`. The JPEG preview is globally limited to `2 FPS` and does not define
the control rate.

MaixPy opens UART0 for its default Maix Comm Protocol during import. Production
must call `maix.comm.rm_default_comm_listener()` once before mapping A16/A17 and
opening `/dev/ttyS0`. Do not remove this release step and do not let MaixVision
UART traffic share the control port. Web and SSH diagnostics remain available.

## Lightweight False-Positive Suppression

The production entry point is unchanged. Filtering is added to the existing
detector and control loop; there is no second vision script or camera owner.

1. Every fourth frame, estimate the pipe's vertical shift. Limit one update to
   6 px and apply a 0.5 smoothing factor before moving the ROI.
2. Subtract the global brightness offset, then estimate a smoothed per-column
   residual offset. Local correction is capped at 12 gray levels so geometry
   changes cannot be completely hidden as illumination changes.
3. Threshold the empty-pipe difference and project it to the original 1-D
   response. Keep up to five separated peaks that pass minimum area, width, and
   height checks instead of committing immediately to the global maximum.
4. Initial acquisition requires three consistent frames and confidence >= 0.15.
5. Once locked, choose only candidates compatible with the previous position
   and velocity. `max_speed_mm_s=800`, a 20 px jump margin, and a 70 px
   prediction gate reject a left-to-right teleport while retaining nearby weaker
   candidates when a distant reflection has a stronger response.
6. A rejected measurement does not update the alpha-beta tracker. UART clears
   `FLAG_DETECTED`; a short-lived tracker estimate may still be sent with
   `FLAG_PREDICTED`, and the receiver must continue honoring all flags.
7. A lock is retained for 0.35 s of missing measurements. Reacquisition then
   again requires three consistent frames.

Useful `/status.json` fields are `raw_detected`, `measurement_rejected`,
`rejection_reason`, `candidate_count`, `raw_center_x`, `expected_center_x`,
`filter_locked`, `rejected_jumps`, `vertical_shift`, `alignment_cost`, and
`alignment_update_ms`. The web page shows `REJECTED` when a raw difference peak
exists but fails measurement validation.

These checks prevent impossible measurements from reaching control; they do not
make arbitrary pipe rotation equivalent to calibration. Two endpoint markers or
pipe-coordinate rectification are still required if mechanical fixing cannot
keep rotation and perspective changes small.

## 2026-07-30 Frame-Rate Incident

At LAN address `192.168.43.87`, diagnostics observed:

- Direct vision process PID `922`, PPID `1`:
  `python3 /root/ball_detection/current/app/maixcam_ball_control.py --config ...`
- Launcher UI PID `927`, child of `launcher_daemon` PID `296`, running at the
  same time.
- `control_hz` and `uart_hz` about `13 Hz`.
- `preview_seq=0`, proving JPEG preview was not the cause.
- Load average `8.03`; temperature about `69.6 C`.
- Current, original release, and Git key-file SHA-256 values all matched.

Conclusion: this was a runtime ownership/lifecycle fault, not an algorithm or
Git code change. PIDs are transient; never reuse these numbers without fresh
verification.

## Read-Only Diagnosis

Run these before changing board state:

```sh
readlink -f /root/ball_detection/current
cat /maixapp/auto_start.txt
ps -eo pid,ppid,stat,comm,args
wget -qO- http://127.0.0.1:8080/status.json
tail -n 80 /maixapp/tmp/last_run.log
cat /sys/class/thermal/thermal_zone0/temp
cat /proc/loadavg
```

Required healthy state:

- Exactly one production Python process.
- Its PPID resolves to the one verified `launcher_daemon` process.
- No concurrent `/maixapp/apps/launcher/launcher` UI or built-in Camera process.
- `frames` increases, `uart_hz > 0`, and `uart_errors == 0`.
- Use `control_hz` for detection rate. Do not infer it from browser video.

## Recover From A Direct SSH Start

This operation briefly interrupts UART output. Do not perform it while physical
control depends on uninterrupted packets.

1. Locate the direct vision process. Verify its executable is
   `/usr/bin/python3.11`, its full command points to this project, and its PPID
   is `1`. Verify it is the only matching vision process.
2. Locate exactly one launcher UI and one `launcher_daemon`. Verify paths are
   `/maixapp/apps/launcher/launcher` and
   `/maixapp/apps/launcher/launcher_daemon`, and verify the UI PPID equals the
   daemon PID.
3. Send `SIGTERM` only to the verified direct vision PID and wait up to 10
   seconds for it to exit and release the camera.
4. Atomically write the following exact three lines to `/tmp/run_app.txt`:

   ```text
   /usr/bin/python3
   ball_detection_control
   /maixapp/apps/ball_detection_control/main.py
   ```

5. After verifying the three-line file, send `SIGTERM` to the verified launcher
   UI PID. `launcher_daemon` will serially start the queued production app.
6. Verify the new Python process is a child of `launcher_daemon`; then sample
   `status.json` at least twice and confirm frames increase and rate recovers.

If any identity or cardinality check fails, stop. Do not guess a PID, launch a
second copy, or retry by direct SSH.

## Modify And Deploy

1. Read the current code and `git status`; preserve unrelated user changes.
2. Change source in Git, add focused tests, and run:

   ```sh
   python -m unittest discover -s tests -v
   git diff --check
   ```

3. Create a new immutable directory below `/root/ball_detection/releases/`.
   Never edit the active release in place.
4. Verify the uploaded archive SHA-256 and run all tests inside the new release
   before switching `/root/ball_detection/current` atomically.
5. Update `/maixapp/apps/ball_detection_control/main.py` and `app.yaml`
   atomically.
6. Restart only through the official launcher lifecycle described above.
7. Verify process ownership, status, UART errors, frame progression, temperature,
   and logs. Then commit and push the same source to GitHub.

## Calibration

The production page has no calibration button. A calibration happens only after
one explicit request:

```text
POST http://<board-ip>:8080/calibrate/empty
```

Poll `status.json` until `calibration_state` becomes `succeeded`, then verify
`reference_mismatch` clears on later frames. Never retry POST after an ambiguous
timeout; poll first to determine whether the first request was accepted.
