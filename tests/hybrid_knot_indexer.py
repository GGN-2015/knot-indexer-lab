#!/usr/bin/env python3
"""Run the hybrid_knot_indexer coordinate dataset against this server."""

from __future__ import annotations

import argparse
import http.cookiejar
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_REPOSITORY = "https://github.com/TopologicalKnotIndexer/hybrid_knot_indexer"
UPSTREAM_REVISION = "b959fda15f76ab7bf6eb02571a5dbd237024b65b"
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
DEFAULT_SERVER = ROOT / "build" / ("knot_indexer_lab_server" + EXE_SUFFIX)
DEFAULT_CACHE = ROOT / ".cache" / "hybrid_knot_indexer"
BUILTIN_DATA = ROOT / "tests" / "data" / "hybrid_knot_indexer" / "che_data"
MAX_ARCHIVE_BYTES = 64 * 1024 * 1024


@dataclass(frozen=True)
class TestCase:
    expected: str
    path: Path


@dataclass
class TestResult:
    expected: str
    input_file: str
    passed: bool
    duration_seconds: float
    pd_crossings: int
    candidates: list[str]
    status: str
    error: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run TopologicalKnotIndexer/hybrid_knot_indexer CHE coordinate "
            "cases through the knot-indexer-lab HTTP API."
        )
    )
    source_group = parser.add_mutually_exclusive_group()
    source_group.add_argument(
        "--source",
        type=Path,
        help="Local hybrid_knot_indexer checkout, src/che_data directory, or equivalent dataset directory.",
    )
    source_group.add_argument(
        "--upstream",
        action="store_true",
        help="Download and test an upstream revision instead of using the dataset committed in this repository.",
    )
    parser.add_argument(
        "--revision",
        default=UPSTREAM_REVISION,
        help=f"Archive revision used with --upstream. Default: {UPSTREAM_REVISION}",
    )
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="Redownload the selected --upstream revision instead of using the cache.",
    )
    parser.add_argument(
        "--server",
        type=Path,
        default=DEFAULT_SERVER,
        help=f"Local server executable. Default: {DEFAULT_SERVER}",
    )
    parser.add_argument(
        "--server-url",
        help="Use an already running server instead of starting a local executable.",
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="Build the local server before testing. A missing default executable is built automatically.",
    )
    parser.add_argument(
        "--case",
        action="append",
        default=[],
        dest="cases",
        metavar="KNOT_NAME",
        help="Run only this expected knot name. Repeat to select multiple cases.",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List discovered cases without starting the server.",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=1200,
        help="Per-case HTTP and local server computation timeout in seconds. Default: 1200.",
    )
    parser.add_argument(
        "--max-crossing",
        type=int,
        default=14,
        help="Maximum crossing number for a locally started server. Default: 14.",
    )
    parser.add_argument(
        "--json-report",
        type=Path,
        help="Write a machine-readable test report to this path.",
    )
    args = parser.parse_args()
    if args.timeout <= 0 or args.timeout > 1200:
        parser.error("--timeout must be between 1 and 1200 seconds")
    if args.max_crossing <= 0 or args.max_crossing > 16:
        parser.error("--max-crossing must be between 1 and 16")
    if args.server_url and args.rebuild:
        parser.error("--rebuild cannot be combined with --server-url")
    if args.refresh and not args.upstream:
        parser.error("--refresh requires --upstream")
    if args.revision != UPSTREAM_REVISION and not args.upstream:
        parser.error("--revision requires --upstream")
    return args


def safe_cache_key(revision: str) -> str:
    readable = re.sub(r"[^A-Za-z0-9._-]+", "_", revision).strip("._-")
    if not readable:
        raise ValueError("revision does not contain a usable cache key")
    return readable


def locate_data_directory(source: Path) -> Path:
    source = source.expanduser().resolve()
    candidates = [source / "src" / "che_data", source / "che_data", source]
    for candidate in candidates:
        if not candidate.is_dir():
            continue
        if any(child.is_dir() and any(item.is_file() for item in child.iterdir()) for child in candidate.iterdir()):
            return candidate
    raise FileNotFoundError(f"cannot find hybrid_knot_indexer che_data below {source}")


def read_limited(response: object, limit: int) -> bytes:
    chunks: list[bytes] = []
    total = 0
    while True:
        chunk = response.read(1024 * 1024)  # type: ignore[attr-defined]
        if not chunk:
            return b"".join(chunks)
        total += len(chunk)
        if total > limit:
            raise RuntimeError(f"upstream archive exceeds the {limit // (1024 * 1024)} MiB safety limit")
        chunks.append(chunk)


def download_dataset(revision: str, refresh: bool) -> Path:
    target = DEFAULT_CACHE / safe_cache_key(revision)
    if refresh and target.exists():
        shutil.rmtree(target)
    if target.exists():
        try:
            return locate_data_directory(target)
        except FileNotFoundError:
            shutil.rmtree(target)

    archive_url = (
        "https://codeload.github.com/TopologicalKnotIndexer/"
        f"hybrid_knot_indexer/zip/{urllib.parse.quote(revision, safe='')}"
    )
    print(f"Downloading hybrid_knot_indexer test data at {revision} ...")
    request = urllib.request.Request(archive_url, headers={"User-Agent": "knot-indexer-lab-test"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            archive_bytes = read_limited(response, MAX_ARCHIVE_BYTES)
    except (OSError, urllib.error.URLError) as error:
        raise RuntimeError(f"cannot download {archive_url}: {error}") from error

    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="hybrid_knot_data_", dir=target.parent) as temporary:
        staging = Path(temporary) / "dataset"
        data_target = staging / "src" / "che_data"
        data_target.mkdir(parents=True)
        extracted = 0
        archive_path = Path(temporary) / "upstream.zip"
        archive_path.write_bytes(archive_bytes)
        try:
            with zipfile.ZipFile(archive_path) as archive:
                for member in archive.infolist():
                    marker = "/src/che_data/"
                    if member.is_dir() or marker not in member.filename:
                        continue
                    relative_text = member.filename.split(marker, 1)[1]
                    relative = PurePosixPath(relative_text)
                    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
                        raise RuntimeError(f"unsafe path in upstream archive: {member.filename}")
                    destination = data_target.joinpath(*relative.parts)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(member) as source_file, destination.open("wb") as output:
                        shutil.copyfileobj(source_file, output)
                    extracted += 1
        except (OSError, zipfile.BadZipFile) as error:
            raise RuntimeError(f"invalid upstream archive: {error}") from error
        if extracted == 0:
            raise RuntimeError("the upstream archive contains no src/che_data files")
        (staging / "UPSTREAM.txt").write_text(
            f"Repository: {UPSTREAM_REPOSITORY}\nRevision: {revision}\n",
            encoding="utf-8",
        )
        shutil.move(str(staging), str(target))
    return locate_data_directory(target)


def discover_cases(data_directory: Path, selected: list[str]) -> list[TestCase]:
    requested = set(selected)
    cases: list[TestCase] = []
    available: set[str] = set()
    for knot_directory in sorted((path for path in data_directory.iterdir() if path.is_dir()), key=lambda path: path.name):
        available.add(knot_directory.name)
        if requested and knot_directory.name not in requested:
            continue
        for input_file in sorted(path for path in knot_directory.iterdir() if path.is_file()):
            cases.append(TestCase(knot_directory.name, input_file))
    missing = sorted(requested - available)
    if missing:
        raise ValueError("unknown --case value(s): " + ", ".join(missing))
    if not cases:
        raise ValueError(f"no test cases found in {data_directory}")
    return cases


def local_source_revision(source: Path) -> str:
    current = source.expanduser().resolve()
    for directory in (current, *current.parents):
        if not (directory / ".git").exists():
            continue
        try:
            process = subprocess.run(
                ["git", "-C", str(directory), "rev-parse", "HEAD"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            return "local"
        revision = process.stdout.strip()
        return revision if process.returncode == 0 and revision else "local"
    return "local"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_ready(base_url: str, process: subprocess.Popen[bytes] | None) -> None:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"server exited during startup with code {process.returncode}")
        try:
            with urllib.request.urlopen(base_url + "/api/last_build_info", timeout=1) as response:
                if response.status == 200:
                    return
        except (OSError, urllib.error.URLError):
            time.sleep(0.1)
    raise RuntimeError(f"server did not become ready at {base_url}")


def stop_server(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def build_server(server: Path) -> None:
    command = [sys.executable, str(ROOT / "build.py"), "--output", str(server)]
    subprocess.run(command, cwd=ROOT, check=True)


def post_coordinate_case(
    opener: urllib.request.OpenerDirector,
    base_url: str,
    case: TestCase,
    timeout: int,
) -> TestResult:
    started = time.monotonic()
    candidates: list[str] = []
    pd_crossings = 0
    status = "error"
    error_text = ""
    try:
        coordinate_text = case.path.read_text(encoding="utf-8-sig")
        body = json.dumps({"coord_3d": coordinate_text}).encode("utf-8")
        request = urllib.request.Request(
            base_url + "/api/index_coord_3d",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with opener.open(request, timeout=timeout + 15) as response:
            payload = json.loads(response.read().decode("utf-8"))
        status = str(payload.get("status", "error"))
        candidate_text = str(payload.get("knot_name", ""))
        candidates = [name.strip() for name in candidate_text.split(";") if name.strip()]
        pd_code = str(payload.get("pd_code", ""))
        pd_crossings = len(re.findall(r"\[\s*-?\d+\s*,\s*-?\d+\s*,\s*-?\d+\s*,\s*-?\d+\s*\]", pd_code))
        if status != "success":
            error_text = str(payload.get("message", "request failed"))
        elif case.expected not in candidates:
            error_text = f"expected {case.expected} among the returned candidates"
    except (OSError, UnicodeError, ValueError, urllib.error.URLError) as error:
        error_text = str(error)
    return TestResult(
        expected=case.expected,
        input_file=str(case.path),
        passed=(status == "success" and case.expected in candidates),
        duration_seconds=round(time.monotonic() - started, 3),
        pd_crossings=pd_crossings,
        candidates=candidates,
        status=status,
        error=error_text,
    )


def write_report(path: Path, revision: str, data_directory: Path, results: list[TestResult]) -> None:
    report = {
        "upstream_repository": UPSTREAM_REPOSITORY,
        "upstream_revision": revision,
        "data_directory": str(data_directory),
        "passed": sum(result.passed for result in results),
        "failed": sum(not result.passed for result in results),
        "results": [asdict(result) for result in results],
    }
    path = path.expanduser().resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"JSON report: {path}")


def run(args: argparse.Namespace) -> int:
    if args.source is not None:
        data_directory = locate_data_directory(args.source)
        dataset_revision = local_source_revision(args.source)
    elif args.upstream:
        data_directory = download_dataset(args.revision, args.refresh)
        dataset_revision = args.revision
    else:
        data_directory = locate_data_directory(BUILTIN_DATA)
        dataset_revision = UPSTREAM_REVISION
    cases = discover_cases(data_directory, args.cases)
    print(f"Dataset: {data_directory}")
    print(f"Revision: {dataset_revision}")
    print(f"Cases: {len(cases)}")
    if args.list:
        for case in cases:
            print(f"{case.expected}\t{case.path.name}")
        return 0

    process: subprocess.Popen[bytes] | None = None
    server_log_file = None
    server_log_path: Path | None = None
    temporary: tempfile.TemporaryDirectory[str] | None = None
    base_url = args.server_url.rstrip("/") if args.server_url else ""
    try:
        if not args.server_url:
            server = args.server.expanduser().resolve()
            if args.rebuild or not server.is_file():
                build_server(server)
            temporary = tempfile.TemporaryDirectory(prefix="kil_hybrid_test_")
            temporary_path = Path(temporary.name)
            server_log_path = temporary_path / "server.log"
            server_log_file = server_log_path.open("w+b")
            port = free_port()
            base_url = f"http://127.0.0.1:{port}"
            command = [
                str(server),
                "--host", "127.0.0.1",
                "--port", str(port),
                "--data-folder", str(ROOT / "data"),
                "--web-root", str(ROOT / "web"),
                "--task-history", str(temporary_path / "tasks.history"),
                "--timeout", str(args.timeout),
                "--max-crossing", str(args.max_crossing),
            ]
            process = subprocess.Popen(command, stdout=server_log_file, stderr=subprocess.STDOUT)
        wait_ready(base_url, process)
        print(f"Server: {base_url}")

        cookies = http.cookiejar.CookieJar()
        opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cookies))
        results: list[TestResult] = []
        for index, case in enumerate(cases, start=1):
            result = post_coordinate_case(opener, base_url, case, args.timeout)
            results.append(result)
            outcome = "PASS" if result.passed else "FAIL"
            candidates = ", ".join(result.candidates) if result.candidates else "none"
            print(
                f"[{index:02d}/{len(cases):02d}] {case.expected:<6} {outcome} "
                f"{result.duration_seconds:8.3f}s crossings={result.pd_crossings:<2} candidates={candidates}"
            )
            if result.error:
                print(f"         {result.error}")

        passed = sum(result.passed for result in results)
        failed = len(results) - passed
        print(f"Summary: {passed} passed, {failed} failed, {len(results)} total")
        if args.json_report:
            write_report(args.json_report, dataset_revision, data_directory, results)
        return 0 if failed == 0 else 1
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        if server_log_file is not None and server_log_path is not None:
            server_log_file.flush()
            server_log_file.seek(0)
            log = server_log_file.read().decode("utf-8", errors="replace").strip()
            if log:
                print("--- server log ---", file=sys.stderr)
                print(log, file=sys.stderr)
        return 2
    finally:
        stop_server(process)
        if server_log_file is not None:
            server_log_file.close()
        if temporary is not None:
            temporary.cleanup()


def main() -> int:
    try:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(line_buffering=True)
        args = parse_args()
        return run(args)
    except (FileNotFoundError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
