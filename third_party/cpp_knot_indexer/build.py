#!/usr/bin/env python3
"""Build the pure C++ knot tools with a g++-style compiler."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
SQLITE = ROOT / "third_party" / "sqlite" / "sqlite-amalgamation-3530300"

TARGET_OUTPUTS = {
    "knot_indexer": "cpp_knot_indexer",
    "che_to_coord": "che_to_coord",
    "link_pd_code": "link_pd_code",
}

TARGET_SOURCES = {
    "knot_indexer": [
        "src/knot_indexer/main.cpp",
        "third_party/cppkh/cppkh_main.cpp",
    ],
    "che_to_coord": [
        "src/che_to_coord/che_to_coord.cpp",
    ],
    "link_pd_code": [
        "src/link_pd_code/link_pd_code.cpp",
    ],
}

DEFAULT_TARGETS = ["knot_indexer", "che_to_coord", "link_pd_code"]

LIBHOMFLY_SOURCES = [
    "third_party/libhomfly/bound.c",
    "third_party/libhomfly/control.c",
    "third_party/libhomfly/dllink.c",
    "third_party/libhomfly/homfly.c",
    "third_party/libhomfly/knot.c",
    "third_party/libhomfly/model.c",
    "third_party/libhomfly/order.c",
    "third_party/libhomfly/poly.c",
]

SQLITE_SOURCES = [
    SQLITE / "sqlite3.c",
]


def split_command(value: str) -> list[str]:
    return shlex.split(value, posix=(os.name != "nt"))


def command_display(cmd: list[str]) -> str:
    return " ".join(shlex.quote(x) for x in cmd)


def run_quiet(cmd: list[str], timeout: int = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )


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

    platform_candidates = ["g++", "clang++", "c++"]
    if os.name == "nt":
        platform_candidates = ["g++.exe", "clang++.exe", "c++.exe", "g++", "clang++", "c++"]
    else:
        platform_candidates += ["x86_64-w64-mingw32-g++"]

    for name in platform_candidates:
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
        if "clang" in version.lower() or "g++" in version.lower() or "gcc" in version.lower():
            return candidate

    raise SystemExit("ERROR: no g++-style C++ compiler found; pass --cxx or set CXX")


def test_compile(cxx: list[str], flags: list[str], link_flags: list[str] | None = None, code: str = "int main(){return 0;}") -> bool:
    link_flags = link_flags or []
    with tempfile.TemporaryDirectory(prefix="hki_build_probe_") as tmp:
        src = Path(tmp) / "probe.cpp"
        out = Path(tmp) / ("probe" + EXE_SUFFIX)
        src.write_text(code, encoding="utf-8")
        cmd = cxx + flags + [str(src), "-o", str(out)] + link_flags
        try:
            proc = run_quiet(cmd, timeout=30)
        except (OSError, subprocess.SubprocessError):
            return False
        return proc.returncode == 0


def supported_flag(cxx: list[str], current_flags: list[str], flag: str, link_flags: list[str] | None = None) -> bool:
    return test_compile(cxx, current_flags + [flag], link_flags)


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
    flags += ["-DHKI_WITH_SQLITE", "-DSQLITE_THREADSAFE=1", "-DSQLITE_OMIT_LOAD_EXTENSION"]

    system = platform.system().lower()
    if system == "windows":
        flags += ["-DKH_THREAD_BACKEND_WIN32", "-DNOMINMAX"]
        link_flags: list[str] = ["-lws2_32", "-lshell32"]
    else:
        flags += ["-DKH_THREAD_BACKEND_STD"]
        link_flags = ["-pthread"]

    flags += [
        "-I", str(ROOT / "src" / "knot_indexer"),
        "-I", str(ROOT / "src" / "common"),
        "-I", str(ROOT / "src" / "che_to_coord"),
        "-I", str(ROOT / "src" / "link_pd_code"),
        "-I", str(ROOT / "third_party/libhomfly"),
        "-I", str(SQLITE),
        "-I", str(ROOT / "third_party" / "cpp-pd-code-simplify" / "include"),
    ]

    if not args.debug:
        for flag in ["-pipe"]:
            if supported_flag(cxx, flags, flag, link_flags):
                flags.append(flag)

        if not args.portable:
            for flag in ["-march=native", "-mtune=native"]:
                if supported_flag(cxx, flags, flag, link_flags):
                    flags.append(flag)

        if not args.no_lto and supported_flag(cxx, flags, "-flto", link_flags):
            flags.append("-flto")
            if supported_flag(cxx, flags, "-fuse-linker-plugin", link_flags):
                flags.append("-fuse-linker-plugin")

        if system not in ("windows", "darwin") and supported_flag(cxx, flags, "-fno-plt", link_flags):
            flags.append("-fno-plt")

    if args.static_runtime and system == "windows":
        link_flags += ["-static-libstdc++", "-static-libgcc"]

    for flag in args.extra_cxxflag:
        flags.append(flag)
    for flag in args.extra_ldflag:
        link_flags.append(flag)

    link_flags = detect_filesystem_link_flag(cxx, flags, link_flags)
    return flags, link_flags


def executable_name(target: str) -> str:
    return TARGET_OUTPUTS[target] + EXE_SUFFIX


def source_args(target: str) -> list[str]:
    args = [str(ROOT / src) for src in TARGET_SOURCES[target]]
    if target != "knot_indexer":
        return args

    args += ["-x", "c"]
    args += [str(src) for src in SQLITE_SOURCES]
    # libhomfly is C source, but this project compiles it as C++ so it can be
    # linked in one pass with the C++ executable and the local gc.h shim.
    args += ["-x", "c++"]
    args += [str(ROOT / src) for src in LIBHOMFLY_SOURCES]
    args += ["-x", "none"]
    return args


def copy_data_folder(output: Path) -> None:
    source = ROOT / "data"
    target = output.parent / "data"
    if not source.is_dir():
        raise SystemExit(f"ERROR: data folder not found: {source}")
    try:
        if source.resolve() == target.resolve():
            print(f"INFO: data folder already beside executable: {target}")
            return
    except OSError:
        pass
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target)
    print(f"INFO: copied data folder to {target}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the C++ knot tool executables with g++/clang++.")
    parser.add_argument("--target", choices=["all", *DEFAULT_TARGETS], default="all",
                        help="Executable to build. Default: all.")
    parser.add_argument("--cxx", help="C++ compiler command, e.g. g++, clang++, or /path/to/g++.")
    parser.add_argument("--build-dir", default=str(ROOT / "build"), help="Build output directory.")
    parser.add_argument("--output", help="Output executable path. Only valid with a single --target.")
    parser.add_argument("--debug", action="store_true", help="Build with -O0 -g.")
    parser.add_argument("--portable", "--no-native", action="store_true",
                        help="Disable -march=native/-mtune=native for portable binaries.")
    parser.add_argument("--no-lto", action="store_true", help="Disable link-time optimization.")
    parser.add_argument("--static-runtime", action="store_true",
                        help="On Windows, link libstdc++/libgcc statically.")
    parser.add_argument("--extra-cxxflag", action="append", default=[], help="Append an extra compiler flag.")
    parser.add_argument("--extra-ldflag", action="append", default=[], help="Append an extra linker flag.")
    parser.add_argument("--show-command", action="store_true", help="Print the full compiler command.")
    parser.add_argument("--clean", action="store_true", help="Remove the build directory before compiling.")
    return parser.parse_args()


def selected_targets(args: argparse.Namespace) -> list[str]:
    if args.target == "all":
        if args.output:
            raise SystemExit("ERROR: --output can only be used with --target knot_indexer, "
                             "--target che_to_coord, or --target link_pd_code")
        return list(DEFAULT_TARGETS)
    return [args.target]


def output_path(args: argparse.Namespace, build_dir: Path, target: str) -> Path:
    if args.output:
        return Path(args.output).resolve()
    return build_dir / executable_name(target)


def build_target(args: argparse.Namespace,
                 cxx: list[str],
                 flags: list[str],
                 link_flags: list[str],
                 target: str,
                 build_dir: Path) -> int:
    output = output_path(args, build_dir, target)
    output.parent.mkdir(parents=True, exist_ok=True)
    cmd = cxx + flags + source_args(target) + ["-o", str(output)] + link_flags

    print(f"INFO: target: {target}")
    print(f"INFO: output: {output}")
    if args.show_command:
        print(command_display(cmd))

    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        return proc.returncode

    if target == "knot_indexer":
        copy_data_folder(output)
    print(f"INFO: built {output}")
    return 0


def main() -> int:
    args = parse_args()
    cxx = find_compiler(args.cxx)
    version = compiler_version(cxx).splitlines()[0]
    targets = selected_targets(args)

    build_dir = Path(args.build_dir).resolve()
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    flags, link_flags = build_flags(args, cxx)

    print(f"INFO: compiler: {version}")
    print("INFO: optimization: " + ("debug" if args.debug else "release"))

    for target in targets:
        result = build_target(args, cxx, flags, link_flags, target, build_dir)
        if result != 0:
            return result
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
