from __future__ import annotations

import argparse
import json
import socket
import time
from urllib.parse import urlsplit


class RtspClient:
    def __init__(self, url: str, timeout_s: float = 5.0) -> None:
        self.url = url
        parsed = urlsplit(url)
        if parsed.scheme.lower() != "rtsp" or not parsed.hostname:
            raise ValueError("expected an rtsp:// URL")
        self.socket = socket.create_connection(
            (parsed.hostname, parsed.port or 554), timeout=timeout_s
        )
        self.socket.settimeout(timeout_s)
        self.buffer = bytearray()
        self.cseq = 0

    def close(self) -> None:
        self.socket.close()

    def request(self, method: str, url: str, headers=None):
        self.cseq += 1
        lines = [f"{method} {url} RTSP/1.0", f"CSeq: {self.cseq}"]
        for key, value in (headers or {}).items():
            lines.append(f"{key}: {value}")
        self.socket.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
        return self._response()

    def _response(self):
        marker = b"\r\n\r\n"
        while marker not in self.buffer:
            self.buffer.extend(self.socket.recv(65536))
        header_end = self.buffer.index(marker) + len(marker)
        header = bytes(self.buffer[:header_end])
        del self.buffer[:header_end]
        lines = header.decode("ascii", "replace").split("\r\n")
        status = int(lines[0].split(" ", 2)[1])
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                key, value = line.split(":", 1)
                headers[key.lower()] = value.strip()
        length = int(headers.get("content-length", "0"))
        while len(self.buffer) < length:
            self.buffer.extend(self.socket.recv(65536))
        body = bytes(self.buffer[:length])
        del self.buffer[:length]
        if status < 200 or status >= 300:
            raise RuntimeError(f"RTSP request failed with status {status}")
        return headers, body

    def play(self, duration_s: float) -> dict:
        describe_headers, sdp_bytes = self.request(
            "DESCRIBE", self.url, {"Accept": "application/sdp"}
        )
        sdp = sdp_bytes.decode("ascii", "replace")
        controls = [
            line.split(":", 1)[1].strip()
            for line in sdp.splitlines()
            if line.startswith("a=control:") and line.strip() != "a=control:*"
        ]
        if not controls:
            raise RuntimeError("RTSP SDP has no media control URL")
        control = controls[0]
        base = describe_headers.get("content-base", self.url)
        if control.startswith("rtsp://"):
            track_url = control
        else:
            track_url = base.rstrip("/") + "/" + control.lstrip("/")
        setup_headers, _ = self.request(
            "SETUP",
            track_url,
            {"Transport": "RTP/AVP/TCP;unicast;interleaved=0-1"},
        )
        session = setup_headers["session"].split(";", 1)[0]
        self.request("PLAY", self.url, {"Session": session})

        self.socket.settimeout(1.0)
        started = time.monotonic()
        received = 0
        packets = 0
        while time.monotonic() - started < duration_s:
            try:
                chunk = self.socket.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            received += len(chunk)
            self.buffer.extend(chunk)
            while len(self.buffer) >= 4 and self.buffer[0] == 0x24:
                packet_length = int.from_bytes(self.buffer[2:4], "big")
                total = packet_length + 4
                if len(self.buffer) < total:
                    break
                packets += 1
                del self.buffer[:total]
        elapsed = time.monotonic() - started
        return {
            "elapsed_s": elapsed,
            "received_bytes": received,
            "interleaved_packets": packets,
            "bitrate_bps": received * 8.0 / max(elapsed, 0.001),
            "track_url": track_url,
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="rtsp://10.5.66.1:8554/live")
    parser.add_argument("--duration", type=float, default=30.0)
    args = parser.parse_args()
    client = RtspClient(args.url)
    try:
        result = client.play(args.duration)
    finally:
        client.close()
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
