#!/usr/bin/env python3
import argparse
import ctypes
import json
import os
import threading
import time
from ctypes import wintypes
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import ThreadingMixIn
from urllib.parse import parse_qs, urlparse


GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
MAXDWORD = 0xFFFFFFFF


class DCB(ctypes.Structure):
    _fields_ = [
        ("DCBlength", wintypes.DWORD),
        ("BaudRate", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("wReserved", wintypes.WORD),
        ("XonLim", wintypes.WORD),
        ("XoffLim", wintypes.WORD),
        ("ByteSize", wintypes.BYTE),
        ("Parity", wintypes.BYTE),
        ("StopBits", wintypes.BYTE),
        ("XonChar", ctypes.c_char),
        ("XoffChar", ctypes.c_char),
        ("ErrorChar", ctypes.c_char),
        ("EofChar", ctypes.c_char),
        ("EvtChar", ctypes.c_char),
        ("wReserved1", wintypes.WORD),
    ]


class COMMTIMEOUTS(ctypes.Structure):
    _fields_ = [
        ("ReadIntervalTimeout", wintypes.DWORD),
        ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
        ("ReadTotalTimeoutConstant", wintypes.DWORD),
        ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
        ("WriteTotalTimeoutConstant", wintypes.DWORD),
    ]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.GetCommState.argtypes = [wintypes.HANDLE, ctypes.POINTER(DCB)]
kernel32.GetCommState.restype = wintypes.BOOL
kernel32.SetCommState.argtypes = [wintypes.HANDLE, ctypes.POINTER(DCB)]
kernel32.SetCommState.restype = wintypes.BOOL
kernel32.SetCommTimeouts.argtypes = [
    wintypes.HANDLE, ctypes.POINTER(COMMTIMEOUTS)
]
kernel32.SetCommTimeouts.restype = wintypes.BOOL
kernel32.SetupComm.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD]
kernel32.SetupComm.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.WriteFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPCVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.WriteFile.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL


def win_error(prefix):
    code = ctypes.get_last_error()
    return OSError(code, "%s: %s" % (prefix, ctypes.FormatError(code)))


class SerialBridge:
    def __init__(self, maximum_buffer=1024 * 1024):
        self.maximum_buffer = maximum_buffer
        self.lock = threading.RLock()
        self.condition = threading.Condition(self.lock)
        self.write_lock = threading.Lock()
        self.handle = None
        self.port = None
        self.baud = None
        self.error = None
        self.stop_event = threading.Event()
        self.reader_thread = None
        self.buffer = bytearray()
        self.base_cursor = 0
        self.end_cursor = 0

    def _open_handle(self, port, baud):
        path = r"\\.\%s" % port
        handle = kernel32.CreateFileW(
            path,
            GENERIC_READ | GENERIC_WRITE,
            0,
            None,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            None,
        )
        if handle == INVALID_HANDLE_VALUE:
            raise win_error("cannot open %s" % port)

        try:
            kernel32.SetupComm(handle, 1024 * 1024, 64 * 1024)
            dcb = DCB()
            dcb.DCBlength = ctypes.sizeof(DCB)
            if not kernel32.GetCommState(handle, ctypes.byref(dcb)):
                raise win_error("GetCommState failed")
            dcb.BaudRate = baud
            dcb.flags = 1 | (1 << 7)  # binary, continue TX after XOFF
            dcb.ByteSize = 8
            dcb.Parity = 0
            dcb.StopBits = 0
            if not kernel32.SetCommState(handle, ctypes.byref(dcb)):
                raise win_error("SetCommState failed")
            timeouts = COMMTIMEOUTS(MAXDWORD, 0, 0, 0, 1000)
            if not kernel32.SetCommTimeouts(handle, ctypes.byref(timeouts)):
                raise win_error("SetCommTimeouts failed")
            return handle
        except Exception:
            kernel32.CloseHandle(handle)
            raise

    def connect(self, port, baud):
        port = str(port).upper()
        baud = int(baud)
        with self.lock:
            if self.handle is not None and self.port == port and self.baud == baud:
                return self.status()
        self.close()
        handle = self._open_handle(port, baud)
        with self.lock:
            self.handle = handle
            self.port = port
            self.baud = baud
            self.error = None
            self.stop_event.clear()
            self.buffer.clear()
            self.base_cursor = 0
            self.end_cursor = 0
            self.reader_thread = threading.Thread(
                target=self._reader_loop, name="echo-serial-reader", daemon=True
            )
            self.reader_thread.start()
            return self.status()

    def _reader_loop(self):
        read_buffer = ctypes.create_string_buffer(4096)
        while not self.stop_event.is_set():
            with self.lock:
                handle = self.handle
            if handle is None:
                return
            count = wintypes.DWORD()
            ok = kernel32.ReadFile(
                handle, read_buffer, len(read_buffer), ctypes.byref(count), None
            )
            if not ok:
                with self.lock:
                    self.error = str(win_error("ReadFile failed"))
                    self.condition.notify_all()
                return
            if count.value == 0:
                time.sleep(0.002)
                continue
            data = read_buffer.raw[: count.value]
            with self.condition:
                self.buffer.extend(data)
                self.end_cursor += len(data)
                overflow = len(self.buffer) - self.maximum_buffer
                if overflow > 0:
                    del self.buffer[:overflow]
                    self.base_cursor += overflow
                self.condition.notify_all()

    def write(self, data):
        if not data:
            return 0
        with self.lock:
            handle = self.handle
        if handle is None:
            raise RuntimeError("serial port is not connected")
        payload = ctypes.create_string_buffer(data, len(data))
        written = wintypes.DWORD()
        with self.write_lock:
            if not kernel32.WriteFile(
                handle, payload, len(data), ctypes.byref(written), None
            ):
                raise win_error("WriteFile failed")
        if written.value != len(data):
            raise IOError("short serial write: %d/%d" % (written.value, len(data)))
        return written.value

    def read_since(self, cursor, wait_seconds=0.20, maximum=65536):
        deadline = time.monotonic() + wait_seconds
        with self.condition:
            while cursor >= self.end_cursor and self.error is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self.condition.wait(remaining)
            overflow = cursor < self.base_cursor
            cursor = max(cursor, self.base_cursor)
            offset = cursor - self.base_cursor
            data = bytes(self.buffer[offset : offset + maximum])
            return data, cursor + len(data), overflow

    def status(self):
        with self.lock:
            return {
                "bridge": True,
                "connected": self.handle is not None and self.error is None,
                "port": self.port,
                "baudRate": self.baud,
                "cursor": self.end_cursor,
                "error": self.error,
            }

    def close(self):
        with self.lock:
            handle = self.handle
            thread = self.reader_thread
            self.handle = None
            self.reader_thread = None
            self.stop_event.set()
            self.condition.notify_all()
        if handle is not None:
            kernel32.CloseHandle(handle)
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=1.0)


class BridgeHandler(SimpleHTTPRequestHandler):
    bridge = None

    def log_message(self, fmt, *args):
        if self.path.startswith("/api/") and self.command == "GET":
            return
        super().log_message(fmt, *args)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _json_response(self, status, value):
        payload = json.dumps(value).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self._write_payload(payload)

    def _write_payload(self, payload):
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            pass

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/serial/status":
            self._json_response(200, self.bridge.status())
            return
        if parsed.path == "/api/serial/read":
            query = parse_qs(parsed.query)
            try:
                cursor = int(query.get("cursor", ["0"])[0])
                data, next_cursor, overflow = self.bridge.read_since(cursor)
            except Exception as error:
                self._json_response(500, {"error": str(error)})
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("X-Echo-Cursor", str(next_cursor))
            self.send_header("X-Echo-Overflow", "1" if overflow else "0")
            self.end_headers()
            self._write_payload(data)
            return
        super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        try:
            if parsed.path == "/api/serial/connect":
                request = json.loads(body.decode("utf-8") or "{}")
                value = self.bridge.connect(
                    request.get("port", "COM9"), request.get("baudRate", 230400)
                )
                self._json_response(200, value)
                return
            if parsed.path == "/api/serial/write":
                written = self.bridge.write(body)
                self._json_response(200, {"written": written})
                return
            self._json_response(404, {"error": "unknown API"})
        except Exception as error:
            self._json_response(409, {"error": str(error), **self.bridge.status()})


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--http-port", type=int, default=8765)
    parser.add_argument("--serial-port", default="COM9")
    parser.add_argument("--baud-rate", type=int, default=230400)
    parser.add_argument(
        "--web-root",
        default=os.path.join(os.path.dirname(__file__), "telemetry-web"),
    )
    args = parser.parse_args()

    bridge = SerialBridge()
    try:
        bridge.connect(args.serial_port, args.baud_rate)
    except Exception as error:
        bridge.error = str(error)
        print("Serial bridge startup warning: %s" % error, flush=True)

    BridgeHandler.bridge = bridge
    os.chdir(os.path.abspath(args.web_root))
    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), BridgeHandler)
    server.daemon_threads = True
    print(
        "ECHO PID bridge: http://127.0.0.1:%d/ -> %s @ %d"
        % (args.http_port, args.serial_port, args.baud_rate),
        flush=True,
    )
    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        server.server_close()
        bridge.close()


if __name__ == "__main__":
    main()
