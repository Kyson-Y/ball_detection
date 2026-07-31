from __future__ import annotations

import json
import mimetypes
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit


INDEX_HTML = b"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MaixCAM Media</title><style>
*{box-sizing:border-box}body{margin:0;background:#101214;color:#edf0f2;font:14px Arial,sans-serif;letter-spacing:0}header{height:54px;padding:0 16px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #34383c;background:#181b1e}main{max-width:960px;margin:auto;padding:14px}.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:14px}button,a.action{height:36px;padding:0 14px;border:1px solid #5c646b;background:#252a2e;color:#fff;border-radius:4px;text-decoration:none;display:inline-flex;align-items:center;cursor:pointer}button.primary{background:#287a4d;border-color:#3a9a65}button.stop{background:#923f36;border-color:#b45449}button:disabled{opacity:.45;cursor:default}.status{color:#aab2b8}.stream{padding:12px 0;border-top:1px solid #34383c;border-bottom:1px solid #34383c;margin-bottom:16px}.stream code{word-break:break-all;color:#7fd2a2}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.item{border:1px solid #34383c;background:#181b1e;padding:10px;border-radius:6px;min-width:0}.item video{display:block;width:100%;aspect-ratio:4/3;background:#050505;margin-bottom:9px}.name{font:13px Consolas,monospace;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.meta{color:#9aa2a8;font-size:12px;margin:6px 0 9px}.empty{color:#9aa2a8;padding:30px 0}@media(max-width:680px){.grid{grid-template-columns:1fr}main{padding:10px}}
</style></head><body>
<header><strong>MaixCAM Media</strong><span id="state" class="status">CONNECTING</span></header>
<main><div class="bar"><button id="start" class="primary">Start recording</button><button id="stop" class="stop">Stop recording</button><button id="refresh">Refresh files</button><span id="elapsed" class="status"></span></div>
<section class="stream">Live H.264: <code id="rtsp">--</code><br><span class="status">Open this URL with VLC on the tablet. Saved MP4 files can be previewed below.</span></section>
<section id="files" class="grid"></section></main><script>
const get=id=>document.getElementById(id);let busy=false;
const size=n=>n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(1)+' MB';
async function api(path,method='GET'){const r=await fetch(path,{method,cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.error||'request failed');return d}
async function status(){try{const d=await api('/api/status');get('state').textContent=d.recording?'RECORDING':'READY';get('elapsed').textContent=d.recording?d.elapsed_s.toFixed(1)+' s':'';get('start').disabled=d.recording||busy;get('stop').disabled=!d.recording||busy;get('rtsp').textContent=d.stream_url}catch(e){get('state').textContent='OFFLINE'}}
async function files(){const d=await api('/api/recordings'),root=get('files');root.replaceChildren();if(!d.recordings.length){const p=document.createElement('p');p.className='empty';p.textContent='No recordings';root.append(p);return}for(const f of d.recordings){const box=document.createElement('article');box.className='item';const v=document.createElement('video');v.controls=true;v.preload='metadata';v.src=f.url;const name=document.createElement('div');name.className='name';name.textContent=f.name;const meta=document.createElement('div');meta.className='meta';meta.textContent=size(f.size_bytes);const a=document.createElement('a');a.className='action';a.href=f.url+'?download=1';a.textContent='Download';box.append(v,name,meta,a);root.append(box)}}
async function command(path){busy=true;await status();try{await api(path,'POST');await files()}catch(e){alert(e.message)}finally{busy=false;await status()}}
get('start').onclick=()=>command('/api/record/start');get('stop').onclick=()=>command('/api/record/stop');get('refresh').onclick=files;status();files();setInterval(status,1000);
</script></body></html>"""


def _parse_range(value: str, size: int):
    if not value or not value.startswith("bytes="):
        return None
    spec = value[6:].split(",", 1)[0].strip()
    if "-" not in spec:
        return None
    start_text, end_text = spec.split("-", 1)
    try:
        if not start_text:
            length = int(end_text)
            if length <= 0:
                return None
            start = max(0, size - length)
            end = size - 1
        else:
            start = int(start_text)
            end = size - 1 if not end_text else int(end_text)
    except ValueError:
        return None
    if start < 0 or start >= size or end < start:
        return None
    return start, min(end, size - 1)


class MediaHttpServer:
    def __init__(self, manager, stream_url: str, port: int, host="0.0.0.0"):
        self.manager = manager
        self.stream_url = str(stream_url)
        self.host = str(host)
        self.port = int(port)
        self._server = None
        self._thread = None

    def start(self) -> None:
        manager = self.manager
        stream_url = self.stream_url

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, _format, *_args) -> None:
                return

            def _send_bytes(self, status, content_type, payload, headers=None):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Cache-Control", "no-store")
                if headers:
                    for key, value in headers.items():
                        self.send_header(key, value)
                self.end_headers()
                if self.command != "HEAD":
                    self.wfile.write(payload)

            def _send_json(self, status, payload):
                body = json.dumps(payload, separators=(",", ":")).encode("ascii")
                self._send_bytes(status, "application/json", body)

            def _recording(self, path, download):
                name = unquote(path[len("/recordings/") :])
                file_path = manager.resolve_recording(name)
                if file_path is None:
                    self._send_bytes(404, "text/plain", b"not found\n")
                    return
                size = os.path.getsize(file_path)
                byte_range = _parse_range(self.headers.get("Range", ""), size)
                start, end = (0, size - 1) if byte_range is None else byte_range
                status = 200 if byte_range is None else 206
                headers = {"Accept-Ranges": "bytes"}
                if byte_range is not None:
                    headers["Content-Range"] = f"bytes {start}-{end}/{size}"
                if download:
                    headers["Content-Disposition"] = f'attachment; filename="{name}"'
                self.send_response(status)
                self.send_header(
                    "Content-Type", mimetypes.guess_type(name)[0] or "video/mp4"
                )
                self.send_header("Content-Length", str(end - start + 1))
                self.send_header("Cache-Control", "no-store")
                for key, value in headers.items():
                    self.send_header(key, value)
                self.end_headers()
                if self.command == "HEAD":
                    return
                with open(file_path, "rb") as handle:
                    handle.seek(start)
                    remaining = end - start + 1
                    while remaining > 0:
                        chunk = handle.read(min(64 * 1024, remaining))
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        remaining -= len(chunk)

            def _get(self):
                parsed = urlsplit(self.path)
                if parsed.path in ("/", "/index.html"):
                    self._send_bytes(200, "text/html; charset=utf-8", INDEX_HTML)
                elif parsed.path == "/api/status":
                    payload = manager.status()
                    payload["stream_url"] = stream_url
                    self._send_json(200, payload)
                elif parsed.path == "/api/recordings":
                    self._send_json(200, {"recordings": manager.list_recordings()})
                elif parsed.path.startswith("/recordings/"):
                    self._recording(parsed.path, "download=1" in parsed.query)
                else:
                    self._send_bytes(404, "text/plain", b"not found\n")

            def do_GET(self):
                self._get()

            def do_HEAD(self):
                self._get()

            def do_POST(self):
                path = urlsplit(self.path).path
                try:
                    if path == "/api/record/start":
                        self._send_json(200, manager.start())
                    elif path == "/api/record/stop":
                        self._send_json(200, manager.stop())
                    else:
                        self._send_json(404, {"error": "not_found"})
                except Exception as exc:
                    self._send_json(409, {"error": str(exc)})

        class Server(ThreadingHTTPServer):
            daemon_threads = True

        self._server = Server((self.host, self.port), Handler)
        self.port = int(self._server.server_address[1])
        self._thread = threading.Thread(
            target=self._server.serve_forever, daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
