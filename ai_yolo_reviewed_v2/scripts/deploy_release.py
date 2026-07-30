from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import posixpath
import stat
import sys
import time
from pathlib import Path


EXPECTED_FINGERPRINT = "NR1VK1J6oYAk4aXxfwMPdWg3MRbXdpKcRbaO1EhpoC4"
APP_ID = "ball_detection_control"
CURRENT_LINK = "/root/ball_detection/current"
APP_DIRECTORY = f"/maixapp/apps/{APP_ID}"
PRODUCTION_ARGS = f"/usr/bin/python3 {APP_DIRECTORY}/main.py"
DAEMON_ARGS = "/maixapp/apps/launcher/launcher_daemon"
LAUNCHER_ARGS = "/maixapp/apps/launcher/launcher daemon"
LAUNCHER_EXE = "/maixapp/apps/launcher/launcher"
RUN_APP = (
    b"/usr/bin/python3\n"
    + APP_ID.encode("ascii")
    + b"\n"
    + f"{APP_DIRECTORY}/main.py\n".encode("ascii")
)


def remote_run(client, command: str, timeout: float = 15.0, check: bool = True):
    _, stdout, stderr = client.exec_command(command, timeout=timeout)
    output = stdout.read().decode("utf-8", "replace")
    error = stderr.read().decode("utf-8", "replace")
    code = stdout.channel.recv_exit_status()
    if check and code != 0:
        raise RuntimeError(f"remote command failed: {command}\n{output}{error}")
    return code, output.strip(), error.strip()


def processes(client):
    _, output, _ = remote_run(client, "ps -eo pid,ppid,stat,comm,args")
    parsed = []
    for line in output.splitlines()[1:]:
        fields = line.split(None, 4)
        if len(fields) == 5:
            parsed.append(
                {
                    "pid": int(fields[0]),
                    "ppid": int(fields[1]),
                    "stat": fields[2],
                    "comm": fields[3],
                    "args": fields[4],
                }
            )
    return parsed


def exact_processes(client, args: str):
    return [process for process in processes(client) if process["args"] == args]


def readlink(client, path: str) -> str:
    return remote_run(client, f"readlink -f {path}")[1]


def wait_for_process(client, args: str, timeout_s: float):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = exact_processes(client, args)
        if len(found) == 1:
            return found[0]
        if len(found) > 1:
            raise RuntimeError(f"multiple matching processes: {args}")
        time.sleep(0.25)
    return None


def wait_for_exit(client, pid: int, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        code, _, _ = remote_run(client, f"test ! -d /proc/{pid}", check=False)
        if code == 0:
            return
        time.sleep(0.25)
    raise RuntimeError(f"process did not exit: {pid}")


def mkdirs(sftp, path: str) -> None:
    parts = []
    cursor = path
    while cursor not in ("", "/"):
        parts.append(cursor)
        cursor = posixpath.dirname(cursor)
    for directory in reversed(parts):
        try:
            sftp.stat(directory)
        except OSError:
            sftp.mkdir(directory, 0o755)


def upload_tree(sftp, local_root: Path, remote_root: str) -> None:
    mkdirs(sftp, remote_root)
    for local_path in sorted(local_root.rglob("*")):
        if "__pycache__" in local_path.parts:
            continue
        relative = local_path.relative_to(local_root).as_posix()
        remote_path = posixpath.join(remote_root, relative)
        if local_path.is_dir():
            mkdirs(sftp, remote_path)
            continue
        if local_path.suffix in {".pt", ".onnx", ".zip", ".template"}:
            continue
        mkdirs(sftp, posixpath.dirname(remote_path))
        sftp.put(str(local_path), remote_path)
        sftp.chmod(remote_path, 0o644)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install and activate an immutable AI YOLO release."
    )
    parser.add_argument("--host", default="10.5.66.1")
    parser.add_argument("--password", default=os.environ.get("MAIXCAM_PASSWORD", ""))
    parser.add_argument("--paramiko-path", type=Path)
    parser.add_argument("--activate", action="store_true")
    args = parser.parse_args()
    if not args.password:
        raise ValueError("set MAIXCAM_PASSWORD or pass --password")
    if args.paramiko_path:
        sys.path.insert(0, str(args.paramiko_path))
    import paramiko

    root = Path(__file__).resolve().parents[1]
    mud = root / "models" / "ball_yolo11n_reviewed_v2.mud"
    cvimodel = root / "models" / "ball_yolo11n_reviewed_v2.cvimodel"
    if not mud.is_file() or not cvimodel.is_file():
        raise FileNotFoundError(
            "conversion result is missing; add both .mud and .cvimodel first"
        )
    release_id = f"ai_yolo_reviewed_v2_{time.strftime('%Y%m%d_%H%M%S')}"
    release = f"/root/ball_detection/releases/{release_id}"

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        args.host,
        username="root",
        password=args.password,
        timeout=5,
        auth_timeout=5,
        look_for_keys=False,
        allow_agent=False,
    )
    key = client.get_transport().get_remote_server_key()
    fingerprint = (
        base64.b64encode(hashlib.sha256(key.asbytes()).digest())
        .decode()
        .rstrip("=")
    )
    if fingerprint != EXPECTED_FINGERPRINT:
        raise RuntimeError(f"unexpected host key SHA256:{fingerprint}")

    previous_release = readlink(client, CURRENT_LINK)
    sftp = client.open_sftp()
    upload_tree(sftp, root, release)
    sftp.put(str(mud), f"{release}/models/{mud.name}")
    sftp.put(str(cvimodel), f"{release}/models/{cvimodel.name}")
    for filename in ("main.py", "app.yaml"):
        mkdirs(sftp, APP_DIRECTORY)
        sftp.put(str(root / "maix_app" / filename), f"{APP_DIRECTORY}/{filename}")
    sftp.close()
    remote_run(client, f"test -r {release}/models/{mud.name}")
    remote_run(client, f"test -r {release}/models/{cvimodel.name}")
    print(
        json.dumps(
            {
                "release": release,
                "previous_release": previous_release,
                "uploaded": True,
                "activated": False,
            }
        )
    )
    if not args.activate:
        client.close()
        return 0

    daemon = exact_processes(client, DAEMON_ARGS)
    production = exact_processes(client, PRODUCTION_ARGS)
    if len(daemon) != 1 or len(production) != 1:
        raise RuntimeError("unexpected launcher or app process cardinality")
    if production[0]["ppid"] != daemon[0]["pid"]:
        raise RuntimeError("production app is not owned by launcher_daemon")

    temporary_link = f"{CURRENT_LINK}.new.{int(time.time())}"
    remote_run(client, f"ln -s {release} {temporary_link}")
    remote_run(client, f"mv -T {temporary_link} {CURRENT_LINK}")
    if readlink(client, CURRENT_LINK) != release:
        raise RuntimeError("release link verification failed")
    autostart = f"/maixapp/auto_start.txt.new.{int(time.time())}"
    remote_run(client, f"printf %s {APP_ID} > {autostart}")
    remote_run(client, f"mv {autostart} /maixapp/auto_start.txt")
    remote_run(client, "sync")

    remote_run(client, f"kill -TERM {production[0]['pid']}")
    wait_for_exit(client, production[0]["pid"], 10.0)
    new_process = wait_for_process(client, PRODUCTION_ARGS, 2.0)
    if new_process is None:
        launcher = wait_for_process(client, LAUNCHER_ARGS, 10.0)
        if launcher is None or launcher["ppid"] != daemon[0]["pid"]:
            raise RuntimeError("launcher UI did not become ready")
        sftp = client.open_sftp()
        with sftp.open("/tmp/run_app.txt.new", "wb") as handle:
            handle.write(RUN_APP)
        sftp.posix_rename("/tmp/run_app.txt.new", "/tmp/run_app.txt")
        sftp.close()
        remote_run(client, f"kill -TERM {launcher['pid']}")
        wait_for_exit(client, launcher["pid"], 10.0)
        new_process = wait_for_process(client, PRODUCTION_ARGS, 15.0)
    if new_process is None:
        raise RuntimeError("new AI app did not start")
    print(
        json.dumps(
            {
                "release": release,
                "previous_release": previous_release,
                "uploaded": True,
                "activated": True,
                "pid": new_process["pid"],
            }
        )
    )
    client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
