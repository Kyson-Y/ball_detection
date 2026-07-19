# ECHO PID Console

Double-click `open_pid_console.cmd` from the project root. It starts the local
server when needed and opens Edge or Chrome at the fixed address:

    http://127.0.0.1:8765/

The launcher can be run repeatedly. It reuses a healthy local serial bridge
instead of starting duplicate processes. The bridge owns COM9 continuously,
so page reloads and browser closes do not toggle DAPLink DTR or reset the MCU.
To run the foreground bridge manually:

    python .\tools\telemetry_bridge.py --http-port 8765 --serial-port COM9 --baud-rate 230400

Click `启动模拟` to exercise the complete tuning workflow without opening a
COM port or driving a motor. The simulator runs the same 100 Hz chart and
command paths with the 513X-4S feedforward and limits plus an illustrative
first-order motor model. It validates workflow and parameter effects; it is not
a substitute for measured motor tuning.

Open http://127.0.0.1:8765/ in Microsoft Edge or Chrome, then:

1. Close UartAssist and every other user of the board UART before starting the bridge.
2. Click Connect. The page uses the running COM9 bridge; there is no device chooser.
3. Keep the default 230400 baud setting.
4. Enable the desired speed and PID channels below the synchronized charts.
5. Apply the shared Kp/Ki/Kd values; the page waits for every device ACK.
6. Send independent left/right targets in the range -100..100 rpm.
7. Send 0/0 rpm or use the stop button to stop continuous speed control.

Direct Web Serial remains available only for diagnostics by adding
`?transport=webserial` to the URL. It performs a real bidirectional 0/0 ACK
probe over each DTR/RTS combination before enabling controls. The bridge is the
normal mode because direct Chrome Web Serial toggles this DAPLink control line
on close and resets the MCU.

## Protocol

- Sync: A5 5A
- Version: 1
- Control telemetry: type 1, 96-byte payload, 112-byte frame
- Parameter set: type 2, 12-byte payload, 28-byte frame
- Parameter ACK: type 3, 16-byte payload, 32-byte frame
- Continuous speed command: type 5, 20-byte payload, duration 0, speed mode 1
- Actuator ACK: type 6, 16-byte payload, 32-byte frame
- CRC: CRC16-CCITT-FALSE over version through payload
- Integer and float fields: little-endian

Parameter changes update RAM only. They are validated and applied by
SystemTask at the 10 ms control-cycle boundary. No Flash write occurs. Closing
the page does not restore the defaults; reset or power cycling does.

The browser sends speed commands with duration 0, which means continuous speed
control. The controller keeps the last non-zero target until it receives 0/0
rpm, resets, or enters a fault stop. Closing or disconnecting the browser does
not close the bridge or stop the motor. Stop the bridge process only after
sending 0/0 rpm.

The capture parser remains compatible with legacy 40-byte and 44-byte control
payloads. Those formats do not contain the PID component fields.

## Command-line tools

Stop the PID bridge before running a command-line tool because COM9 is an
exclusive Windows serial port. Restart `open_pid_console.cmd` afterward.

Capture a bounded CSV session without opening the web page:

    powershell -ExecutionPolicy Bypass -File .\tools\telemetry_capture.ps1 -Port COM9 -BaudRate 230400 -DurationSeconds 600 -CsvPath .\logs\telemetry.csv

Send one RAM parameter update. The port stays open for up to three attempts
with the same transaction ID, so a retry cannot apply the value twice:

    powershell -ExecutionPolicy Bypass -File .\tools\parameter_set.ps1 -Port COM9 -BaudRate 230400 -Parameter kp -Value 2.5

Run protocol fault and recovery checks on a connected board:

    powershell -ExecutionPolicy Bypass -File .\tools\protocol_stress_test.ps1 -Port COM9 -BaudRate 230400 -TimeoutMilliseconds 1000

The capture tool limits an in-memory session to 900 seconds. Its gap count is
the number of missing frames, with 32-bit sequence and timestamp wraparound.
