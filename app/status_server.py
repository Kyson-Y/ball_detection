from __future__ import annotations

import json
import socket
import threading


INDEX_HTML = b"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ball Control Status</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#111;color:#eee;font:14px Arial,sans-serif}header{height:52px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;background:#1b1b1b;border-bottom:1px solid #3a3a3a}header strong{font-size:16px}.state{display:flex;align-items:center;gap:8px;color:#aaa}.dot{width:9px;height:9px;border-radius:50%;background:#c84b3a}.live .dot{background:#43a66b}.live{color:#d8f0df}main{width:min(100%,920px);margin:auto;padding:18px}.metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));border-top:1px solid #3a3a3a;border-bottom:1px solid #3a3a3a}.metric{min-width:0;padding:16px 14px;border-right:1px solid #3a3a3a}.metric:nth-child(4n){border-right:0}.label{display:block;color:#999;font-size:12px;margin-bottom:8px}.value{display:block;font:20px Consolas,monospace;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.ok{color:#59c980}.warn{color:#e5ad47}.bad{color:#ef7463}@media(max-width:650px){main{padding:10px}.metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.metric:nth-child(2n){border-right:0}.metric{border-bottom:1px solid #3a3a3a}}
</style>
</head>
<body>
<header><strong>Ball Control</strong><span id="state" class="state"><i class="dot"></i><span>CONNECTING</span></span></header>
<main><section class="metrics">
<div class="metric"><span class="label">CONTROL RATE</span><span id="control" class="value">--</span></div>
<div class="metric"><span class="label">UART RATE</span><span id="uart" class="value">--</span></div>
<div class="metric"><span class="label">DETECTION</span><span id="detection" class="value">--</span></div>
<div class="metric"><span class="label">POSITION</span><span id="position" class="value">--</span></div>
<div class="metric"><span class="label">VELOCITY</span><span id="velocity" class="value">--</span></div>
<div class="metric"><span class="label">CONFIDENCE</span><span id="confidence" class="value">--</span></div>
<div class="metric"><span class="label">TEMPERATURE</span><span id="temperature" class="value">--</span></div>
<div class="metric"><span class="label">FLAGS</span><span id="flags" class="value">--</span></div>
</section></main>
<script>
const get=id=>document.getElementById(id),state=get('state');
async function refresh(){try{const r=await fetch('/status.json',{cache:'no-store'});if(!r.ok)throw 0;const d=await r.json();state.classList.add('live');state.lastElementChild.textContent='LIVE';get('control').textContent=d.control_hz.toFixed(2)+' Hz';get('uart').textContent=d.uart_hz.toFixed(2)+' Hz';const valid=d.detected&&!d.reference_mismatch;get('detection').textContent=d.reference_mismatch?'REFERENCE':(d.detected?'FOUND':'NONE');get('detection').className='value '+(valid?'ok':(d.reference_mismatch?'warn':'bad'));get('position').textContent=valid?d.position_mm.toFixed(2)+' mm':'--';get('velocity').textContent=d.velocity_valid?d.velocity_mm_s.toFixed(1)+' mm/s':'--';get('confidence').textContent=d.confidence.toFixed(3);get('temperature').textContent=d.temperature_c===null?'--':d.temperature_c.toFixed(1)+' C';get('temperature').className='value '+(d.temperature_c!==null&&d.temperature_c>=70?'warn':'');get('flags').textContent='0x'+d.flags.toString(16).padStart(2,'0').toUpperCase()}catch(e){state.classList.remove('live');state.lastElementChild.textContent='RECONNECTING'}}
refresh();setInterval(refresh,250);
</script>
</body>
</html>
"""


class StatusServer:
    def __init__(self, port: int, host: str = "0.0.0.0") -> None:
        self.host = host
        self.port = int(port)
        self._snapshot = {
            "control_hz": 0.0,
            "uart_hz": 0.0,
            "detected": False,
            "reference_mismatch": False,
            "position_mm": 0.0,
            "velocity_mm_s": 0.0,
            "velocity_valid": False,
            "confidence": 0.0,
            "temperature_c": None,
            "flags": 0,
            "uart_errors": 0,
            "frames": 0,
        }
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
        self._snapshot = snapshot

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

    def _handle(self, client) -> None:
        client.settimeout(0.2)
        try:
            request = client.recv(1024)
        except (OSError, socket.timeout):
            return
        parts = request.split(b" ", 2)
        path = parts[1].split(b"?", 1)[0] if len(parts) >= 2 else b""
        if path == b"/status.json":
            payload = json.dumps(self._snapshot, separators=(",", ":")).encode("ascii")
            content_type = b"application/json"
            status = b"200 OK"
        elif path in (b"/", b"/index.html"):
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
