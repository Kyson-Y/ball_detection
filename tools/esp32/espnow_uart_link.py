import espnow
import gc
import machine
import network
import struct
import time
import ubinascii


UART_BAUD = 230400
UART_FRAME_BYTES = 16
UART_SYNC_0 = 0xA5
UART_SYNC_1 = 0x5A
UART_VERSION = 1
UART_PING = 1
UART_ACK = 2
UART_PATTERN_0 = 0x3C
UART_PATTERN_1 = 0xC3

RADIO_SYNC_0 = 0xE7
RADIO_SYNC_1 = 0x3A
RADIO_VERSION = 1
RADIO_DATA = 1
RADIO_ACK = 2
RADIO_FRAME_BYTES = 28
RADIO_PAYLOAD_OFFSET = 10

WIFI_CHANNEL = 6
RETRY_INTERVAL_MS = 15
MAX_SEND_ATTEMPTS = 6
REPORT_INTERVAL_MS = 5000
WATCHDOG_TIMEOUT_MS = 8000

NODE_CONFIG = {
    "aca7041db67c": (1, "master", "14c19f2eb240"),
    "14c19f2eb240": (2, "slave", "aca7041db67c"),
}


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


def valid_uart_ping(frame):
    if len(frame) != UART_FRAME_BYTES:
        return False
    if frame[0] != UART_SYNC_0 or frame[1] != UART_SYNC_1:
        return False
    if frame[2] != UART_VERSION or frame[3] != UART_PING:
        return False
    if frame[12] != UART_PATTERN_0 or frame[13] != UART_PATTERN_1:
        return False
    received_crc = frame[14] | (frame[15] << 8)
    return crc16(frame[2:14]) == received_crc


def uart_ack_from_ping(frame):
    reply = bytearray(frame)
    reply[3] = UART_ACK
    reply_crc = crc16(reply[2:14])
    reply[14] = reply_crc & 0xFF
    reply[15] = reply_crc >> 8
    return reply


def make_radio_packet(packet_type, origin, sequence, uart_frame):
    packet = bytearray(RADIO_FRAME_BYTES)
    packet[0] = RADIO_SYNC_0
    packet[1] = RADIO_SYNC_1
    packet[2] = RADIO_VERSION
    packet[3] = packet_type
    packet[4] = origin
    packet[5] = 0
    struct.pack_into("<I", packet, 6, sequence)
    packet[RADIO_PAYLOAD_OFFSET:RADIO_PAYLOAD_OFFSET + UART_FRAME_BYTES] = uart_frame
    packet_crc = crc16(packet[2:26])
    packet[26] = packet_crc & 0xFF
    packet[27] = packet_crc >> 8
    return bytes(packet)


def decode_radio_packet(packet):
    if packet is None or len(packet) != RADIO_FRAME_BYTES:
        return None
    if packet[0] != RADIO_SYNC_0 or packet[1] != RADIO_SYNC_1:
        return None
    if packet[2] != RADIO_VERSION:
        return None
    received_crc = packet[26] | (packet[27] << 8)
    if crc16(packet[2:26]) != received_crc:
        return None
    packet_type = packet[3]
    if packet_type != RADIO_DATA and packet_type != RADIO_ACK:
        return None
    origin = packet[4]
    sequence = struct.unpack_from("<I", packet, 6)[0]
    uart_frame = bytes(packet[RADIO_PAYLOAD_OFFSET:RADIO_PAYLOAD_OFFSET + UART_FRAME_BYTES])
    if not valid_uart_ping(uart_frame):
        return None
    return packet_type, origin, sequence, uart_frame


station = network.WLAN(network.STA_IF)
access_point = network.WLAN(network.AP_IF)
station.active(False)
access_point.active(False)
station.active(True)
try:
    station.disconnect()
except OSError:
    pass
station.config(channel=WIFI_CHANNEL)

local_mac = bytes(station.config("mac"))
local_mac_hex = ubinascii.hexlify(local_mac).decode()
if local_mac_hex not in NODE_CONFIG:
    raise RuntimeError("unconfigured ESP32 MAC: " + local_mac_hex)

node_id, role, peer_mac_hex = NODE_CONFIG[local_mac_hex]
peer_mac = ubinascii.unhexlify(peer_mac_hex)

radio = espnow.ESPNow()


def configure_radio():
    try:
        radio.active(False)
    except OSError:
        pass
    radio.config(rxbuf=8192, timeout_ms=0)
    radio.active(True)
    try:
        radio.del_peer(peer_mac)
    except OSError:
        pass
    radio.add_peer(peer_mac, channel=WIFI_CHANNEL)


configure_radio()

uart = machine.UART(
    1,
    baudrate=UART_BAUD,
    tx=17,
    rx=18,
    bits=8,
    parity=None,
    stop=1,
    txbuf=2048,
    rxbuf=4096,
    timeout=0,
)
while uart.read():
    pass

uart_frame = bytearray(UART_FRAME_BYTES)
uart_index = 0
pending_packet = None
pending_frame = None
pending_sequence = 0
pending_last_send_ms = 0
pending_attempts = 0
last_remote_sequence = None

uart_rx_bytes = 0
uart_ping_count = 0
uart_ack_count = 0
uart_crc_error_count = 0
uart_format_error_count = 0
uart_write_error_count = 0
local_busy_count = 0
radio_tx_count = 0
radio_rx_count = 0
radio_ack_tx_count = 0
radio_ack_rx_count = 0
radio_retry_count = 0
radio_timeout_count = 0
radio_duplicate_count = 0
radio_invalid_count = 0
radio_stale_ack_count = 0
radio_buffer_error_count = 0
radio_recover_count = 0
radio_peer_error_count = 0


def send_radio(packet):
    global radio_tx_count, radio_peer_error_count
    try:
        if radio.send(peer_mac, packet, False):
            radio_tx_count += 1
            return True
    except OSError:
        pass
    radio_peer_error_count += 1
    return False


def start_pending(frame, now_ms):
    global pending_packet, pending_frame, pending_sequence
    global pending_last_send_ms, pending_attempts

    pending_frame = bytes(frame)
    pending_sequence = struct.unpack_from("<I", pending_frame, 4)[0]
    pending_packet = make_radio_packet(
        RADIO_DATA, node_id, pending_sequence, pending_frame
    )
    pending_attempts = 1
    pending_last_send_ms = now_ms
    send_radio(pending_packet)


def service_uart(now_ms):
    global uart_index, uart_rx_bytes, uart_ping_count
    global uart_crc_error_count, uart_format_error_count, local_busy_count

    chunk = uart.read()
    if not chunk:
        return
    uart_rx_bytes += len(chunk)
    for value in chunk:
        if uart_index == 0:
            if value == UART_SYNC_0:
                uart_frame[0] = value
                uart_index = 1
            continue
        if uart_index == 1:
            if value == UART_SYNC_1:
                uart_frame[1] = value
                uart_index = 2
            elif value != UART_SYNC_0:
                uart_index = 0
            continue

        uart_frame[uart_index] = value
        uart_index += 1
        if uart_index < UART_FRAME_BYTES:
            continue
        uart_index = 0

        received_crc = uart_frame[14] | (uart_frame[15] << 8)
        if crc16(uart_frame[2:14]) != received_crc:
            uart_crc_error_count += 1
            continue
        if not valid_uart_ping(uart_frame):
            uart_format_error_count += 1
            continue

        uart_ping_count += 1
        if pending_packet is None:
            start_pending(uart_frame, now_ms)
        else:
            local_busy_count += 1


def service_radio():
    global pending_packet, pending_frame, pending_attempts
    global last_remote_sequence, uart_ack_count, uart_write_error_count
    global radio_rx_count, radio_ack_tx_count, radio_ack_rx_count
    global radio_duplicate_count, radio_invalid_count, radio_peer_error_count
    global radio_stale_ack_count
    global radio_buffer_error_count, radio_recover_count

    for _ in range(16):
        if not radio.any():
            return
        try:
            host, message = radio.recv(0)
        except ValueError:
            radio_buffer_error_count += 1
            configure_radio()
            radio_recover_count += 1
            return
        except OSError:
            return
        if host is None:
            return
        if host != peer_mac:
            radio_peer_error_count += 1
            continue

        decoded = decode_radio_packet(message)
        if decoded is None:
            radio_invalid_count += 1
            continue
        packet_type, origin, sequence, frame = decoded
        radio_rx_count += 1

        if packet_type == RADIO_DATA:
            if origin == node_id:
                radio_invalid_count += 1
                continue
            if last_remote_sequence == sequence:
                radio_duplicate_count += 1
            else:
                last_remote_sequence = sequence
            response = make_radio_packet(RADIO_ACK, origin, sequence, frame)
            if send_radio(response):
                radio_ack_tx_count += 1
            continue

        if origin != node_id:
            radio_invalid_count += 1
            continue
        if pending_packet is None or sequence != pending_sequence:
            radio_stale_ack_count += 1
            continue
        if frame != pending_frame:
            radio_invalid_count += 1
            continue

        response = uart_ack_from_ping(frame)
        if uart.write(response) == UART_FRAME_BYTES:
            uart_ack_count += 1
            radio_ack_rx_count += 1
            pending_packet = None
            pending_frame = None
            pending_attempts = 0
        else:
            uart_write_error_count += 1


def service_retry(now_ms):
    global pending_packet, pending_frame, pending_attempts
    global pending_last_send_ms, radio_retry_count, radio_timeout_count

    if pending_packet is None:
        return
    if time.ticks_diff(now_ms, pending_last_send_ms) < RETRY_INTERVAL_MS:
        return
    if pending_attempts >= MAX_SEND_ATTEMPTS:
        radio_timeout_count += 1
        pending_packet = None
        pending_frame = None
        pending_attempts = 0
        return

    pending_attempts += 1
    pending_last_send_ms = now_ms
    radio_retry_count += 1
    send_radio(pending_packet)


print(
    "ECHO_ESPNOW_READY",
    "role=" + role,
    "node=" + str(node_id),
    "mac=" + local_mac_hex,
    "peer=" + peer_mac_hex,
    "channel=" + str(WIFI_CHANNEL),
)

watchdog = machine.WDT(timeout=WATCHDOG_TIMEOUT_MS)
started_ms = time.ticks_ms()
next_report_ms = time.ticks_add(started_ms, REPORT_INTERVAL_MS)
while True:
    try:
        now_ms = time.ticks_ms()
        service_uart(now_ms)
        service_radio()
        service_retry(now_ms)
    except Exception as error:
        print("LINK_FATAL", repr(error))
        time.sleep_ms(200)
        machine.reset()

    if time.ticks_diff(now_ms, next_report_ms) >= 0:
        esp_stats = radio.stats()
        print(
            "LINK_STATS role=" + role
            + " uptime_ms=" + str(time.ticks_diff(now_ms, started_ms))
            + " uart_ping=" + str(uart_ping_count)
            + " uart_ack=" + str(uart_ack_count)
            + " radio_tx=" + str(radio_tx_count)
            + " radio_rx=" + str(radio_rx_count)
            + " radio_ack_tx=" + str(radio_ack_tx_count)
            + " radio_ack_rx=" + str(radio_ack_rx_count)
            + " radio_retry=" + str(radio_retry_count)
            + " radio_timeout=" + str(radio_timeout_count)
        )
        print(
            "LINK_ERRORS role=" + role
            + " uart_crc=" + str(uart_crc_error_count)
            + " uart_format=" + str(uart_format_error_count)
            + " uart_write=" + str(uart_write_error_count)
            + " local_busy=" + str(local_busy_count)
            + " radio_duplicate=" + str(radio_duplicate_count)
            + " radio_invalid=" + str(radio_invalid_count)
            + " radio_stale_ack=" + str(radio_stale_ack_count)
            + " radio_buffer=" + str(radio_buffer_error_count)
            + " radio_recover=" + str(radio_recover_count)
            + " radio_peer=" + str(radio_peer_error_count)
            + " esp_tx_fail=" + str(esp_stats[2])
            + " esp_rx_drop=" + str(esp_stats[4])
        )
        gc.collect()
        next_report_ms = time.ticks_add(next_report_ms, REPORT_INTERVAL_MS)
    watchdog.feed()
    time.sleep_ms(1)
