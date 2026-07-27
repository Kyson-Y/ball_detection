# ESP32 UART test fixture

`uart_echo_test.py` is a MicroPython-only fixture for validating the physical
MSPM0 UART2 connection before ESP-NOW is introduced. It is not production
ESP32 firmware.

`espnow_uart_link.py` is the persistent two-node link test. The same file is
installed as `main.py` on both ESP32-S3 boards. It selects the master/slave
identity from the local station MAC, uses ESP-NOW channel 6, and adds an
application CRC, sequence, ACK, retry, peer allowlist, and duplicate handling.
Use `install_micropython.ps1` to upload and verify it through the USB REPL.

Wiring:

```text
MSPM0 PB15 / UART2_TX -> ESP32-S3 GPIO18 / UART1_RX
MSPM0 PB16 / UART2_RX <- ESP32-S3 GPIO17 / UART1_TX
MSPM0 GND             --- ESP32-S3 GND
```

Both sides use 921600 baud, 8N1, and no flow control. Run the script from the
MicroPython REPL with `exec(open("uart_echo_test.py").read())`, or inject it as
a temporary REPL program. A reset removes a temporary REPL program unless the
file was explicitly installed on the ESP32 filesystem.
