#!/usr/bin/env python3
"""Integration tests for the knot-indexer-lab server."""

from __future__ import annotations

import argparse
import base64
import http.cookiejar
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
DEFAULT_EXE = ROOT / "build" / f"knot_indexer_lab_server{EXE_SUFFIX}"
TREFOIL = "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def encode_payload(value: str) -> str:
    encoded = base64.b64encode(value.encode("utf-8")).decode("ascii")
    return urllib.parse.quote(encoded, safe="")


def request_json(opener: urllib.request.OpenerDirector, url: str) -> dict[str, object]:
    with opener.open(url, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def wait_ready(url: str, process: subprocess.Popen[bytes]) -> None:
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"server exited early with code {process.returncode}")
        try:
            with urllib.request.urlopen(url + "/api/last_build_info", timeout=1) as response:
                if response.status == 200:
                    return
        except (OSError, urllib.error.URLError):
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def start_server(exe: Path, history: Path, worker_memory_mb: int) -> tuple[subprocess.Popen[bytes], str]:
    port = free_port()
    command = [
        str(exe),
        "--host", "127.0.0.1",
        "--port", str(port),
        "--data-folder", str(ROOT / "data"),
        "--web-root", str(ROOT / "web"),
        "--task-history", str(history),
        "--worker-memory-mb", str(worker_memory_mb),
        "--memory-reserve-mb", "128",
        "--timeout", "60",
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base_url = f"http://127.0.0.1:{port}"
    wait_ready(base_url, process)
    return process, base_url


def stop_server(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def build(exe: Path) -> None:
    command = [sys.executable, str(ROOT / "build.py"), "--output", str(exe), "--skip-assets"]
    subprocess.run(command, check=True, cwd=ROOT)


def run_tests(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="kil_server_test_") as temporary:
        history = Path(temporary) / "tasks.history"
        cookies = http.cookiejar.CookieJar()
        opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cookies))

        process, base_url = start_server(exe, history, 64)
        try:
            failed = request_json(opener, base_url + "/api/index_pd_code/" + encode_payload(TREFOIL))
            assert failed["status"] == "error", failed
            history_page = request_json(opener, base_url + "/api/tasks/history/0")
            records = history_page["tasks"]
            assert isinstance(records, list) and len(records) == 1, history_page
            assert records[0]["status"] == "failed", records[0]
            assert records[0]["homfly_status"] == "resource_exhausted", records[0]
            with opener.open(base_url + "/api/last_build_info", timeout=5) as response:
                assert response.status == 200

            redirect_opener = urllib.request.build_opener(NoRedirect())
            try:
                redirect_opener.open(base_url + "/tasks.html", timeout=5)
                raise AssertionError("legacy task URL did not redirect")
            except urllib.error.HTTPError as error:
                assert error.code == 302
                assert error.headers["Location"] == "/"
        finally:
            stop_server(process)

        with history.open("ab") as output:
            output.write(b"incomplete-tail")

        cookies = http.cookiejar.CookieJar()
        opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cookies))
        process, base_url = start_server(exe, history, 256)
        try:
            completed = request_json(opener, base_url + "/api/index_pd_code/" + encode_payload(TREFOIL))
            assert completed["status"] == "success", completed
            assert completed["task_id"] == 2, completed
            assert completed["homfly_status"] == "success", completed
            assert completed["khovanov_status"] == "success", completed

            history_page = request_json(opener, base_url + "/api/tasks/history/0")
            records = history_page["tasks"]
            assert isinstance(records, list) and len(records) == 2, history_page
            assert records[0]["id"] == 2 and records[0]["status"] == "completed", records[0]

            session = request_json(opener, base_url + "/api/tasks")
            assert session["last_session_task"]["id"] == 2, session
            with opener.open(base_url + "/", timeout=5) as response:
                html = response.read().decode("utf-8")
            assert "import Tasks from '/static/js/tasks.js'" in html
            assert "/tasks.html" not in html
        finally:
            stop_server(process)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--rebuild", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    exe = args.exe.resolve()
    if args.rebuild or not exe.exists():
        build(exe)
    run_tests(exe)
    print("All integration tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
