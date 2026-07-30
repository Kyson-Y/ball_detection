from __future__ import annotations

import json
import socket
import threading
import time


INDEX_HTML = b"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Ball Status</title><style>
*{box-sizing:border-box}body{margin:0;background:#111;color:#eee;font:14px Arial,sans-serif;letter-spacing:0}header{height:52px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;background:#1b1b1b;border-bottom:1px solid #3a3a3a}header strong{font-size:16px}.state{display:flex;align-items:center;gap:8px;color:#aaa}.dot{width:9px;height:9px;border-radius:50%;background:#c84b3a}.live .dot{background:#43a66b}.live{color:#d8f0df}main{width:min(100%,920px);margin:auto;padding:14px}.preview{width:100%;aspect-ratio:4/3;background:#050505;border:1px solid #3a3a3a;overflow:hidden}.preview img{display:block;width:100%;height:100%;object-fit:contain}.metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));margin-top:14px;border-top:1px solid #3a3a3a;border-left:1px solid #3a3a3a}.metric{min-width:0;height:78px;padding:14px 12px;border-right:1px solid #3a3a3a;border-bottom:1px solid #3a3a3a}.label{display:block;color:#999;font-size:11px;margin-bottom:8px}.value{display:block;font:18px Consolas,monospace;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.ok{color:#59c980}.warn{color:#e5ad47}.bad{color:#ef7463}@media(max-width:650px){main{padding:8px}.metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.metric{height:72px}}
</style></head><body>
<header><strong>YOLO11 Ball Tracking</strong><span id="state" class="state"><i class="dot"></i><span>CONNECTING</span></span></header>
<main><section class="preview"><img id="preview" alt="Camera preview"></section>
<section class="metrics">
<div class="metric"><span class="label">BALL DETECTED</span><span id="detected" class="value">--</span></div>
<div class="metric"><span class="label">CONTROL VALID</span><span id="valid" class="value">--</span></div>
<div class="metric"><span class="label">REASON</span><span id="reason" class="value">--</span></div>
<div class="metric"><span class="label">CANDIDATES</span><span id="candidates" class="value">--</span></div>
<div class="metric"><span class="label">OFFSET</span><span id="offset" class="value">--</span></div>
<div class="metric"><span class="label">VELOCITY</span><span id="velocity" class="value">--</span></div>
<div class="metric"><span class="label">CONFIDENCE</span><span id="confidence" class="value">--</span></div>
<div class="metric"><span class="label">TEMPERATURE</span><span id="temperature" class="value">--</span></div>
<div class="metric"><span class="label">DETECT RATE</span><span id="detectRate" class="value">--</span></div>
<div class="metric"><span class="label">DATA RATE</span><span id="dataRate" class="value">--</span></div>
<div class="metric"><span class="label">NPU TIME</span><span id="npu" class="value">--</span></div>
<div class="metric"><span class="label">PREPROCESS</span><span id="preprocess" class="value">--</span></div>
</section></main><script>
const get=id=>document.getElementById(id),state=get('state'),preview=get('preview');let previewUrl=null;
const number=(value,digits,unit)=>Number.isFinite(value)?value.toFixed(digits)+unit:'--';
async function refreshStatus(){try{const r=await fetch('/status.json',{cache:'no-store'});if(!r.ok)throw 0;const d=await r.json();state.classList.add('live');state.lastElementChild.textContent='LIVE';get('detected').textContent=d.detected?'YES':'NO';get('detected').className='value '+(d.detected?'ok':'bad');get('valid').textContent=d.control_valid?'YES':'NO';get('valid').className='value '+(d.control_valid?'ok':'warn');get('reason').textContent=d.rejection_reason||'--';get('candidates').textContent=Number.isFinite(d.candidate_count)?String(d.candidate_count):'--';get('offset').textContent=number(d.offset_mm,2,' mm');get('velocity').textContent=d.velocity_valid?number(d.velocity_mm_s,1,' mm/s'):'--';get('confidence').textContent=number(d.confidence,3,'');get('temperature').textContent=number(d.temperature_c,1,' C');get('temperature').className='value '+(d.temperature_c>=70?'warn':'');get('detectRate').textContent=number(d.detect_rate,2,' Hz');get('detectRate').className='value '+(d.detect_rate>=30?'ok':'warn');get('dataRate').textContent=number(d.data_rate,2,' Hz');get('npu').textContent=number(d.inference_ms,2,' ms');get('preprocess').textContent=number(d.preprocess_ms,2,' ms')}catch(e){state.classList.remove('live');state.lastElementChild.textContent='RECONNECTING'}}
async function refreshFrame(){try{const r=await fetch('/frame.jpg?ts='+Date.now(),{cache:'no-store'});if(!r.ok)return;const next=URL.createObjectURL(await r.blob()),old=previewUrl;preview.onload=()=>{if(old)URL.revokeObjectURL(old)};previewUrl=next;preview.src=next}catch(e){}}
refreshStatus();refreshFrame();setInterval(refreshStatus,250);setInterval(refreshFrame,500);
</script></body></html>
"""


class StatusServer:
    def __init__(
        self,
        port: int,
        host: str = "0.0.0.0",
        calibration_target_frames: int = 32,
    ) -> None:
        self.host = host
        self.port = int(port)
        self._snapshot = {
            "control_hz": 0.0,
            "control_valid": False,
            "uart_hz": 0.0,
            "detected": False,
            "raw_detected": False,
            "reference_mismatch": False,
            "measurement_rejected": False,
            "rejection_reason": "",
            "ball_x_px": None,
            "offset_px": None,
            "offset_mm": None,
            "origin_x_px": 0.0,
            "position_mm": 0.0,
            "velocity_mm_s": 0.0,
            "velocity_valid": False,
            "confidence": 0.0,
            "alignment_valid": False,
            "marker_valid": False,
            "horizontal_shift": 0,
            "vertical_shift": 0,
            "temperature_c": None,
            "flags": 0,
            "uart_errors": 0,
            "frames": 0,
        }
        self._preview_jpeg = None
        self._preview_seq = 0
        self._preview_updated_at = None
        self._preview_requested_at = None
        self._calibration = {
            "calibration_state": "idle",
            "calibration_captured_frames": 0,
            "calibration_target_frames": int(calibration_target_frames),
            "calibration_id": None,
            "calibration_error": None,
        }
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._socket = None
        self._thread = None

    def start(self) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(4)
        server.settimeout(0.2)
        self.port = int(server.getsockname()[1])
        self._socket = server
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def update(self, snapshot: dict) -> None:
        with self._lock:
            self._snapshot = snapshot

    def update_preview(self, jpeg: bytes) -> None:
        with self._lock:
            self._preview_jpeg = bytes(jpeg)
            self._preview_seq += 1
            self._preview_updated_at = time.monotonic()

    def preview_requested_recently(self, max_age_s: float = 2.0) -> bool:
        with self._lock:
            requested_at = self._preview_requested_at
        return requested_at is not None and time.monotonic() - requested_at <= max_age_s

    def request_empty_calibration(self) -> bool:
        with self._lock:
            if self._calibration["calibration_state"] in ("pending", "capturing"):
                return False
            self._calibration.update(
                {
                    "calibration_state": "pending",
                    "calibration_captured_frames": 0,
                    "calibration_id": None,
                    "calibration_error": None,
                }
            )
            return True

    def consume_empty_calibration_request(self) -> bool:
        with self._lock:
            if self._calibration["calibration_state"] != "pending":
                return False
            self._calibration["calibration_state"] = "capturing"
            return True

    def update_calibration_progress(self, captured_frames: int) -> None:
        with self._lock:
            if self._calibration["calibration_state"] == "capturing":
                self._calibration["calibration_captured_frames"] = int(
                    captured_frames
                )

    def finish_empty_calibration(self, calibration_id: str) -> None:
        with self._lock:
            self._calibration.update(
                {
                    "calibration_state": "succeeded",
                    "calibration_captured_frames": self._calibration[
                        "calibration_target_frames"
                    ],
                    "calibration_id": str(calibration_id),
                    "calibration_error": None,
                }
            )

    def fail_empty_calibration(self, error: str) -> None:
        with self._lock:
            self._calibration.update(
                {
                    "calibration_state": "failed",
                    "calibration_id": None,
                    "calibration_error": str(error),
                }
            )

    def stop(self) -> None:
        self._stop_event.set()
        if self._socket is not None:
            try:
                self._socket.close()
            except OSError:
                pass
        if self._thread is not None:
            self._thread.join(timeout=1.0)

    def _serve(self) -> None:
        while not self._stop_event.is_set():
            try:
                client, _address = self._socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                self._handle(client)
            finally:
                try:
                    client.close()
                except OSError:
                    pass

    def _status_payload(self) -> bytes:
        now = time.monotonic()
        with self._lock:
            snapshot = dict(self._snapshot)
            snapshot["preview_seq"] = self._preview_seq
            snapshot["preview_bytes"] = (
                0 if self._preview_jpeg is None else len(self._preview_jpeg)
            )
            snapshot["preview_age_ms"] = (
                None
                if self._preview_updated_at is None
                else int((now - self._preview_updated_at) * 1000.0)
            )
            snapshot.update(self._calibration)
        return json.dumps(snapshot, separators=(",", ":")).encode("ascii")

    def _mark_preview_requested(self) -> None:
        with self._lock:
            self._preview_requested_at = time.monotonic()

    def _handle(self, client) -> None:
        client.settimeout(0.2)
        try:
            request = client.recv(1024)
        except (OSError, socket.timeout):
            return
        parts = request.split(b" ", 2)
        method = parts[0].upper() if parts else b""
        path = parts[1].split(b"?", 1)[0] if len(parts) >= 2 else b""
        if path == b"/calibrate/empty":
            if method != b"POST":
                payload = b'{"accepted":false,"error":"method_not_allowed"}'
                status = b"405 Method Not Allowed"
            else:
                payload = b'{"accepted":false,"error":"not_required_for_yolo"}'
                status = b"410 Gone"
            content_type = b"application/json"
        elif path == b"/status.json":
            payload = self._status_payload()
            content_type = b"application/json"
            status = b"200 OK"
        elif path == b"/frame.jpg":
            self._mark_preview_requested()
            with self._lock:
                payload = self._preview_jpeg
            if payload is None:
                payload = b""
                status = b"503 Service Unavailable"
            else:
                status = b"200 OK"
            content_type = b"image/jpeg"
        elif path in (b"/", b"/index.html"):
            self._mark_preview_requested()
            payload = INDEX_HTML
            content_type = b"text/html; charset=utf-8"
            status = b"200 OK"
        else:
            payload = b"not found\n"
            content_type = b"text/plain; charset=utf-8"
            status = b"404 Not Found"
        response = (
            b"HTTP/1.1 "
            + status
            + b"\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: "
            + content_type
            + b"\r\nContent-Length: "
            + str(len(payload)).encode("ascii")
            + b"\r\n\r\n"
            + payload
        )
        try:
            client.sendall(response)
        except OSError:
            pass
