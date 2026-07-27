import machine
import time


BAUD = 921600
FRAME_BYTES = 16
SYNC = b"\xa5\x5a"
PING = 1
ACK = 2


def crc16(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


uart = machine.UART(
    1,
    baudrate=BAUD,
    tx=17,
    rx=18,
    bits=8,
    parity=None,
    stop=1,
    txbuf=2048,
    rxbuf=4096,
    timeout=0,
)
frame = bytearray(FRAME_BYTES)
frame_index = 0
rx_bytes = 0
valid_ping = 0
ack_sent = 0
crc_error = 0
format_error = 0
write_error = 0

print("ESP_UART_ECHO_READY", uart)

while True:
    chunk = uart.read()
    if chunk:
        rx_bytes += len(chunk)
        for value in chunk:
            if frame_index == 0:
                if value == SYNC[0]:
                    frame[0] = value
                    frame_index = 1
                continue
            if frame_index == 1:
                if value == SYNC[1]:
                    frame[1] = value
                    frame_index = 2
                elif value != SYNC[0]:
                    frame_index = 0
                continue

            frame[frame_index] = value
            frame_index += 1
            if frame_index < FRAME_BYTES:
                continue
            frame_index = 0

            received_crc = frame[14] | (frame[15] << 8)
            if crc16(frame[2:14]) != received_crc:
                crc_error += 1
                continue
            if (
                frame[2] != 1
                or frame[3] != PING
                or frame[12] != 0x3C
                or frame[13] != 0xC3
            ):
                format_error += 1
                continue

            valid_ping += 1
            frame[3] = ACK
            response_crc = crc16(frame[2:14])
            frame[14] = response_crc & 0xFF
            frame[15] = response_crc >> 8
            if uart.write(frame) == FRAME_BYTES:
                ack_sent += 1
            else:
                write_error += 1
    time.sleep_ms(1)
