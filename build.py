#!/usr/bin/env python3
"""Build the pure C++ knot-indexer-lab server."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import shlex
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
VENDOR = ROOT / "third_party" / "cpp_knot_indexer"
PD_CODE_TO_DIAGRAM = ROOT / "third_party" / "pd_code_to_diagram" / "cpp_src"
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
TARGET_NAME = "knot_indexer_lab_server" + EXE_SUFFIX
VCS_METADATA_DIRS = {".git", ".hg", ".svn"}

SERVER_SOURCES = [
    ROOT / "src" / "server" / "main.cpp",
    VENDOR / "third_party" / "cppkh" / "cppkh_main.cpp",
]

LIBHOMFLY_SOURCES = [
    VENDOR / "third_party" / "libhomfly" / "bound.c",
    VENDOR / "third_party" / "libhomfly" / "control.c",
    VENDOR / "third_party" / "libhomfly" / "dllink.c",
    VENDOR / "third_party" / "libhomfly" / "homfly.c",
    VENDOR / "third_party" / "libhomfly" / "knot.c",
    VENDOR / "third_party" / "libhomfly" / "model.c",
    VENDOR / "third_party" / "libhomfly" / "order.c",
    VENDOR / "third_party" / "libhomfly" / "poly.c",
]


def split_command(value: str) -> list[str]:
    return shlex.split(value, posix=(os.name != "nt"))


def command_display(cmd: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in cmd)


def run_quiet(cmd: list[str], timeout: int = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )


def is_vcs_metadata_path(relative: Path) -> bool:
    return any(part in VCS_METADATA_DIRS for part in relative.parts)


def package_file_list() -> list[Path]:
    cmd = [
        "git",
        "-C",
        str(ROOT),
        "ls-files",
        "--cached",
        "--others",
        "--exclude-standard",
        "-z",
        "--",
        ".",
    ]
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        raise SystemExit(f"ERROR: git is required to package with .gitignore rules: {exc}") from exc

    if proc.returncode != 0:
        error = proc.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"ERROR: failed to collect package files with git ls-files: {error}")

    files: list[Path] = []
    seen: set[str] = set()
    for raw_name in proc.stdout.split(b"\0"):
        if not raw_name:
            continue
        relative_text = raw_name.decode("utf-8", errors="surrogateescape")
        relative = Path(relative_text)
        relative_key = relative.as_posix()
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"ERROR: unsafe package path reported by git: {relative_text}")
        if relative_key in seen or is_vcs_metadata_path(relative):
            continue
        source = ROOT / relative
        if source.is_file():
            files.append(relative)
            seen.add(relative_key)

    return sorted(files, key=lambda path: path.as_posix())


def package_project(output: Path) -> None:
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    try:
        output_relative = output.relative_to(ROOT)
    except ValueError:
        output_relative = None

    files = package_file_list()
    written = 0
    with zipfile.ZipFile(
        output,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=6,
        strict_timestamps=False,
    ) as archive:
        for relative in files:
            if output_relative is not None and relative == output_relative:
                continue
            archive.write(ROOT / relative, (Path(ROOT.name) / relative).as_posix())
            written += 1

    print(f"INFO: packaged {written} files into {output}")


def compiler_version(cxx: list[str]) -> str:
    try:
        proc = run_quiet(cxx + ["--version"], timeout=10)
    except (OSError, subprocess.SubprocessError):
        return ""
    return (proc.stdout + proc.stderr).strip()


def find_compiler(user_cxx: str | None) -> list[str]:
    candidates: list[list[str]] = []

    if user_cxx:
        candidates.append(split_command(user_cxx))
    elif os.environ.get("CXX"):
        candidates.append(split_command(os.environ["CXX"]))

    names = ["g++", "clang++", "c++"]
    if os.name == "nt":
        names = ["g++.exe", "clang++.exe", "c++.exe", *names]

    for name in names:
        path = shutil.which(name)
        if path:
            candidates.append([path])

    seen: set[tuple[str, ...]] = set()
    for candidate in candidates:
        key = tuple(candidate)
        if not candidate or key in seen:
            continue
        seen.add(key)
        version = compiler_version(candidate)
        if not version:
            continue
        lowered = version.lower()
        if "clang" in lowered or "g++" in lowered or "gcc" in lowered:
            return candidate

    raise SystemExit("ERROR: no g++-style C++ compiler found; pass --cxx or set CXX")


def test_compile(cxx: list[str], flags: list[str], link_flags: list[str] | None = None, code: str = "int main(){return 0;}") -> bool:
    link_flags = link_flags or []
    with tempfile.TemporaryDirectory(prefix="kil_build_probe_") as tmp:
        source = Path(tmp) / "probe.cpp"
        output = Path(tmp) / ("probe" + EXE_SUFFIX)
        source.write_text(code, encoding="utf-8")
        cmd = cxx + flags + [str(source), "-o", str(output)] + link_flags
        try:
            proc = run_quiet(cmd, timeout=30)
        except (OSError, subprocess.SubprocessError):
            return False
        return proc.returncode == 0


def supported_flag(cxx: list[str], flags: list[str], flag: str, link_flags: list[str]) -> bool:
    return test_compile(cxx, flags + [flag], link_flags)


def detect_filesystem_link_flag(cxx: list[str], flags: list[str], link_flags: list[str]) -> list[str]:
    code = """
        #include <filesystem>
        int main() {
            return std::filesystem::path(".").empty() ? 1 : 0;
        }
    """
    if test_compile(cxx, flags, link_flags, code):
        return link_flags
    if test_compile(cxx, flags, link_flags + ["-lstdc++fs"], code):
        return link_flags + ["-lstdc++fs"]
    return link_flags


def build_flags(args: argparse.Namespace, cxx: list[str]) -> tuple[list[str], list[str]]:
    flags = ["-std=c++17"]
    flags += ["-O0", "-g"] if args.debug else ["-O3", "-DNDEBUG"]
    flags += ["-DCPPKH_SHARED_LIBRARY"]

    system = platform.system().lower()
    if system == "windows":
        flags += ["-DKH_THREAD_BACKEND_WIN32", "-DNOMINMAX"]
        link_flags = ["-lws2_32"]
    else:
        flags += ["-DKH_THREAD_BACKEND_STD"]
        link_flags = ["-pthread"]

    flags += [
        "-I", str(ROOT / "src" / "server"),
        "-I", str(VENDOR / "src" / "common"),
        "-I", str(VENDOR / "src" / "knot_indexer"),
        "-I", str(VENDOR / "src" / "link_pd_code"),
        "-I", str(VENDOR / "third_party" / "cpp-pd-code-simplify" / "include"),
        "-I", str(VENDOR / "third_party" / "libhomfly"),
        "-I", str(PD_CODE_TO_DIAGRAM),
    ]

    if not args.debug:
        if supported_flag(cxx, flags, "-pipe", link_flags):
            flags.append("-pipe")
        if args.native:
            for flag in ["-march=native", "-mtune=native"]:
                if supported_flag(cxx, flags, flag, link_flags):
                    flags.append(flag)
        if not args.no_lto and supported_flag(cxx, flags, "-flto", link_flags):
            flags.append("-flto")
        if system not in ("windows", "darwin") and supported_flag(cxx, flags, "-fno-plt", link_flags):
            flags.append("-fno-plt")

    if args.static_runtime and system == "windows":
        link_flags += ["-static-libstdc++", "-static-libgcc"]

    flags.extend(args.extra_cxxflag)
    link_flags.extend(args.extra_ldflag)
    link_flags = detect_filesystem_link_flag(cxx, flags, link_flags)
    return flags, link_flags


def source_args() -> list[str]:
    args = [str(path) for path in SERVER_SOURCES]
    args += [str(path) for path in LIBHOMFLY_SOURCES]
    args += ["-x", "none"]
    return args


def copy_runtime_tree(source: Path, target: Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"ERROR: required runtime folder not found: {source}")
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target)
    print(f"INFO: copied {source.name}/ to {target}")


def overlay_tree(source: Path, target: Path) -> None:
    if not source.is_dir():
        return
    copied = 0
    skipped = 0
    for item in source.rglob("*"):
        relative = item.relative_to(source)
        destination = target / relative
        if item.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists() and destination.stat().st_size == item.stat().st_size:
                skipped += 1
                continue
            shutil.copy2(item, destination)
            copied += 1
    print(f"INFO: overlaid {source.name}/ onto {target} ({copied} copied, {skipped} unchanged)")


def copy_runtime_assets(build_dir: Path) -> None:
    data_target = build_dir / "data"
    if data_target.exists():
        shutil.rmtree(data_target)
    data_target.mkdir(parents=True, exist_ok=True)
    for name in ["homfly", "khovanov", "knotname-reg"]:
        copy_runtime_tree(ROOT / "data" / name, data_target / name)
    copy_runtime_tree(ROOT / "web", build_dir / "web")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build or package the pure C++ knot-indexer-lab server.")
    parser.add_argument("--package", action="store_true", help="Create a source zip archive instead of building.")
    parser.add_argument(
        "--package-output",
        default=str(ROOT / "build" / f"{ROOT.name}.zip"),
        help="Zip output path used with --package.",
    )
    parser.add_argument("--cxx", help="C++ compiler command, for example g++ or clang++.")
    parser.add_argument("--build-dir", default=str(ROOT / "build"), help="Build output directory.")
    parser.add_argument("--output", help="Output executable path.")
    parser.add_argument("--debug", action="store_true", help="Build with -O0 -g.")
    parser.add_argument("--native", action="store_true", help="Enable -march=native/-mtune=native when supported.")
    parser.add_argument("--no-lto", action="store_true", help="Disable link-time optimization.")
    parser.add_argument("--static-runtime", action="store_true", help="On Windows, link libstdc++/libgcc statically.")
    parser.add_argument("--extra-cxxflag", action="append", default=[], help="Append an extra compiler flag.")
    parser.add_argument("--extra-ldflag", action="append", default=[], help="Append an extra linker flag.")
    parser.add_argument("--show-command", action="store_true", help="Print the compiler command.")
    parser.add_argument("--clean", action="store_true", help="Remove the build directory before compiling.")
    parser.add_argument("--skip-assets", action="store_true", help="Do not copy data/ and web/ after building.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.package:
        package_project(Path(args.package_output))
        return 0

    cxx = find_compiler(args.cxx)
    version = compiler_version(cxx).splitlines()[0]

    build_dir = Path(args.build_dir).resolve()
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    output = Path(args.output).resolve() if args.output else build_dir / TARGET_NAME
    output.parent.mkdir(parents=True, exist_ok=True)

    flags, link_flags = build_flags(args, cxx)
    cmd = cxx + flags + source_args() + ["-o", str(output)] + link_flags

    print(f"INFO: compiler: {version}")
    print(f"INFO: output: {output}")
    if args.show_command:
        print(command_display(cmd))

    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        return proc.returncode

    if not args.skip_assets:
        copy_runtime_assets(output.parent)

    print(f"INFO: built {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
