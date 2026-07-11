#!/usr/bin/env python3
"""Smoke/regression tests for the pure C++ knot indexer."""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
DEFAULT_EXE = ROOT / "build" / f"cpp_knot_indexer{EXE_SUFFIX}"
CHE_TO_COORD_EXE = f"che_to_coord{EXE_SUFFIX}"
LINK_PD_CODE_EXE = f"link_pd_code{EXE_SUFFIX}"


CASES = [
    {
        "name": "unknot-empty-pd",
        "pd": "[]",
        "expected": "K0a1",
        "must_contain": ["HOMFLY-PT result: 1", "Khovanov result: q^-1*t^0*Z[0] + q^1*t^0*Z[0]"],
    },
    {
        "name": "unknot-r1-simplification",
        "pd": "[[1,2,2,1]]",
        "expected": "K0a1",
        "must_contain": ["HOMFLY-PT result: 1", "Khovanov result: q^-1*t^0*Z[0] + q^1*t^0*Z[0]"],
    },
    {
        "name": "trefoil",
        "pd": "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]",
        "expected": "K3a1",
        "must_contain": [
            "HOMFLY-PT result: -L^4 + L^2*M^2 - 2*L^2",
            "Khovanov result: q^1*t^0*Z[0] + q^3*t^0*Z[0]",
        ],
    },
    {
        "name": "trefoil-reversed-orientation",
        "pd": "[[2,4,1,5],[4,6,3,1],[6,2,5,3]]",
        "expected": "K3a1",
        "must_contain": ["HOMFLY-PT result: -L^4 + L^2*M^2 - 2*L^2"],
    },
    {
        "name": "trefoil-nonconsecutive-labels",
        "pd": "[[10,50,20,40],[30,10,40,60],[50,30,60,20]]",
        "expected": "K3a1",
        "must_contain": ["HOMFLY-PT result: -L^4 + L^2*M^2 - 2*L^2"],
    },
    {
        "name": "cin-input",
        "pd": "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]",
        "expected": "K3a1",
        "stdin": True,
    },
]


def run(cmd: list[str], *, stdin: str | None = None, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        input=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def build_if_needed(exe: Path, cxx: str | None, rebuild: bool, portable: bool) -> None:
    if exe.exists() and not rebuild:
        return
    cmd = [sys.executable, str(ROOT / "build.py"), "--target", "knot_indexer", "--output", str(exe)]
    if cxx:
        cmd += ["--cxx", cxx]
    if portable:
        cmd += ["--portable"]
    proc = run(cmd, timeout=240)
    if proc.returncode != 0:
        print(proc.stdout, end="")
        print(proc.stderr, end="", file=sys.stderr)
        raise SystemExit(proc.returncode)


def assert_data_folder(folder: Path) -> None:
    required = [
        folder / "homfly" / "sorted_HOMFLY-PT.txt",
        folder / "khovanov" / "sorted_khovanov.txt",
        folder / "knotname-reg",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise AssertionError(f"missing copied data folder entries: {missing}")


def assert_case(exe: Path, case: dict[str, object]) -> None:
    pd = str(case["pd"])
    expected = str(case["expected"])
    if case.get("stdin"):
        proc = run([str(exe), "--timeout", "10"], stdin=pd, timeout=30)
    else:
        proc = run([str(exe), "--pd-code", pd, "--timeout", "10", "--verbose"], timeout=30)

    if proc.returncode != 0:
        raise AssertionError(f"{case['name']} failed with exit {proc.returncode}\nstderr:\n{proc.stderr}")

    stdout_lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if "HOMFLY" in proc.stdout or "Khovanov" in proc.stdout:
        raise AssertionError(f"{case['name']} wrote verbose diagnostics to stdout:\n{proc.stdout}")
    if expected not in stdout_lines:
        raise AssertionError(
            f"{case['name']} expected {expected!r} in stdout, got {stdout_lines!r}\nstderr:\n{proc.stderr}"
        )

    combined = proc.stdout + proc.stderr
    for needle in case.get("must_contain", []):
        if str(needle) not in combined:
            raise AssertionError(f"{case['name']} missing {needle!r}\ncombined output:\n{combined}")
    if not case.get("stdin") and "Simplify" not in proc.stderr:
        raise AssertionError(f"{case['name']} did not report simplify diagnostics in verbose mode\nstderr={proc.stderr}")


def assert_custom_data_folder(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cki_data_") as tmp:
        custom = Path(tmp) / "not_named_data"
        shutil.copytree(ROOT / "data", custom)
        proc = run([
            str(exe),
            "--pd-code",
            str(CASES[2]["pd"]),
            "--timeout",
            "10",
            "--data-folder",
            str(custom),
        ], timeout=30)
        if proc.returncode != 0:
            raise AssertionError(f"custom data folder failed\nstderr:\n{proc.stderr}")
        stdout_lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
        if "K3a1" not in stdout_lines:
            raise AssertionError(f"custom data folder lookup failed: {stdout_lines!r}")

        invalid = Path(tmp) / "invalid"
        invalid.mkdir()
        bad = run([
            str(exe),
            "--pd-code",
            str(CASES[2]["pd"]),
            "--timeout",
            "10",
            "--data-folder",
            str(invalid),
        ], timeout=30)
        if bad.returncode == 0 or "invalid --data-folder" not in bad.stderr:
            raise AssertionError(f"invalid data folder should fail\nstdout={bad.stdout}\nstderr={bad.stderr}")


def create_sqlite_invariant_db(path: Path, name: str) -> None:
    import sqlite3

    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(path)
    try:
        conn.execute(
            "CREATE TABLE invariants("
            "name TEXT PRIMARY KEY,"
            "canonical_pd TEXT NOT NULL,"
            "homfly TEXT NOT NULL,"
            "khovanov TEXT NOT NULL)"
        )
        conn.execute("CREATE INDEX idx_invariants_homfly ON invariants(homfly)")
        conn.execute("CREATE INDEX idx_invariants_khovanov ON invariants(khovanov)")
        conn.execute("CREATE INDEX idx_invariants_pair ON invariants(homfly, khovanov)")
        conn.execute(
            "INSERT INTO invariants(name, canonical_pd, homfly, khovanov) VALUES (?, ?, ?, ?)",
            (
                name,
                str(CASES[2]["pd"]),
                "-L^4 + L^2*M^2 - 2*L^2",
                "not-a-khovanov-match",
            ),
        )
        conn.commit()
    finally:
        conn.close()


def assert_sqlite_data_source(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cki_sqlite_") as tmp:
        tmp_path = Path(tmp)
        data = tmp_path / "data"
        shutil.copytree(ROOT / "data", data)

        auto_db = data / "name-pd" / "PD_m_3-16.sqlite"
        create_sqlite_invariant_db(auto_db, "SQLiteTrefoilAuto")
        proc = run([
            str(exe),
            "--pd-code",
            str(CASES[2]["pd"]),
            "--timeout",
            "10",
            "--data-folder",
            str(data),
            "--verbose",
        ], timeout=30)
        if proc.returncode != 0:
            raise AssertionError(f"sqlite auto data source failed\nstdout={proc.stdout}\nstderr={proc.stderr}")
        stdout_lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
        if stdout_lines != ["SQLiteTrefoilAuto"]:
            raise AssertionError(f"sqlite auto lookup returned {stdout_lines!r}\nstderr={proc.stderr}")
        if "SQLite candidate count: 1" not in proc.stderr:
            raise AssertionError(f"sqlite verbose diagnostics missing\nstderr={proc.stderr}")

        explicit_db = tmp_path / "explicit.sqlite"
        create_sqlite_invariant_db(explicit_db, "SQLiteTrefoilExplicit")
        explicit = run([
            str(exe),
            "--pd-code",
            str(CASES[2]["pd"]),
            "--timeout",
            "10",
            "--data-folder",
            str(data),
            "--sqlite-db",
            str(explicit_db),
        ], timeout=30)
        if explicit.returncode != 0:
            raise AssertionError(f"explicit sqlite data source failed\nstdout={explicit.stdout}\nstderr={explicit.stderr}")
        explicit_lines = [line.strip() for line in explicit.stdout.splitlines() if line.strip()]
        if explicit_lines != ["SQLiteTrefoilExplicit"]:
            raise AssertionError(f"explicit sqlite lookup returned {explicit_lines!r}\nstderr={explicit.stderr}")

        missing = run([
            str(exe),
            "--pd-code",
            str(CASES[2]["pd"]),
            "--timeout",
            "10",
            "--data-folder",
            str(data),
            "--sqlite-db",
            str(tmp_path / "missing.sqlite"),
        ], timeout=30)
        if missing.returncode == 0 or "invalid --sqlite-db" not in missing.stderr:
            raise AssertionError(f"missing sqlite db should fail\nstdout={missing.stdout}\nstderr={missing.stderr}")


def assert_missing_default_data_fails(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cki_no_data_") as tmp:
        isolated_dir = Path(tmp) / "bin"
        isolated_dir.mkdir()
        isolated_exe = isolated_dir / exe.name
        shutil.copy2(exe, isolated_exe)
        proc = run([str(isolated_exe), "--pd-code", "[]", "--timeout", "10"], timeout=30)
        if proc.returncode == 0 or "cannot locate default data folder" not in proc.stderr:
            raise AssertionError(f"missing default data should fail\nstdout={proc.stdout}\nstderr={proc.stderr}")


def assert_timeout_degrades(exe: Path) -> None:
    # A zero-second timeout should kill both workers before useful work and
    # return a controlled failure rather than hanging.
    proc = run([str(exe), "--pd-code", str(CASES[2]["pd"]), "--timeout", "0", "--verbose"], timeout=30)
    if proc.returncode != 0:
        # --timeout 0 means "no timeout" in the CLI, so this must still succeed.
        raise AssertionError(f"timeout=0 should disable timeouts, got {proc.returncode}\nstderr:\n{proc.stderr}")

    # Extremely small process timeout cannot be expressed by the public integer
    # option; keep this as a CLI contract check rather than a timing race.
    bad = run([str(exe), "--pd-code", str(CASES[2]["pd"]), "--timeout", "-1"], timeout=30)
    if bad.returncode == 0 or "--timeout must be >= 0" not in bad.stderr:
        raise AssertionError("negative timeout validation failed")


def assert_simplify_worker(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cki_simplify_") as tmp:
        tmp_path = Path(tmp)
        input_path = tmp_path / "input.txt"
        output_path = tmp_path / "output.txt"
        input_path.write_text("[[1,2,2,1]]", encoding="utf-8")
        proc = run([
            str(exe),
            "--worker",
            "simplify",
            "--input",
            str(input_path),
            "--output",
            str(output_path),
        ], timeout=30)
        if proc.returncode != 0:
            raise AssertionError(f"simplify worker failed\nstdout={proc.stdout}\nstderr={proc.stderr}")
        simplified = output_path.read_text(encoding="utf-8").strip()
        if simplified != "[]":
            raise AssertionError(f"simplify worker expected [], got {simplified!r}")


def load_build_module():
    spec = importlib.util.spec_from_file_location("cki_build", ROOT / "build.py")
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load build.py helpers")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def auxiliary_compile_command(cxx: list[str], source: Path, output: Path, extra_link: list[str] | None = None) -> list[str]:
    extra_link = extra_link or []
    return cxx + [
        "-std=c++17",
        "-O2",
        "-I", str(ROOT / "src" / "che_to_coord"),
        "-I", str(ROOT / "src" / "link_pd_code"),
        str(source),
        "-o", str(output),
    ] + extra_link


def assert_auxiliary_modules(cxx_arg: str | None) -> None:
    build_mod = load_build_module()
    cxx = build_mod.find_compiler(cxx_arg)
    with tempfile.TemporaryDirectory(prefix="cki_aux_") as tmp:
        tmp_path = Path(tmp)
        source = tmp_path / "auxiliary_test.cpp"
        output = tmp_path / ("auxiliary_test" + EXE_SUFFIX)
        source.write_text(r'''
#include "che_to_coord.hpp"
#include "link_pd_code.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    const char* molecule = R"(
LAMMPS data file

4 atoms
4 bonds

Atoms

1 0 0 0
2 1 0 0
3 1 1 0
4 0 1 0

Bonds

1 1 1 2
2 1 2 3
3 1 3 4
4 1 4 1
)";
    auto loop = cki::che_to_coord::parseCoordinateLoopText(molecule);
    if (loop.size() != 4 || loop[0].atom_id != 1 || loop[1].atom_id != 2) {
        std::cerr << "unexpected coordinate loop\n";
        return 1;
    }

    const char* broken = R"(
3 atoms
2 bonds
Atoms
1 0 0 0
2 1 0 0
3 0 1 0
Bonds
1 1 1 2
2 1 2 3
)";
    bool rejected = false;
    try {
        (void)cki::che_to_coord::parseCoordinateLoopText(broken);
    } catch (const cki::che_to_coord::ParseError&) {
        rejected = true;
    }
    if (!rejected) {
        std::cerr << "broken molecule was accepted\n";
        return 2;
    }

    std::vector<cki::link_pd_code::Point3> crossing = {
        {-1.0, -1.0, 0.0},
        { 1.0,  1.0, 0.0},
        { 1.0, -1.0, 1.0},
        {-1.0,  1.0, 1.0},
    };
    cki::link_pd_code::Options options;
    options.direction = cki::link_pd_code::Point3{0.0, 0.0, 1.0};
    options.prefer_min_crossings = false;
    auto pd = cki::link_pd_code::computePDCode(crossing, options);
    if (pd.size() != 1 || !cki::link_pd_code::validatePDCode(pd)) {
        std::cerr << "unexpected PD code: " << cki::link_pd_code::formatPDCode(pd) << "\n";
        return 3;
    }

    std::vector<cki::link_pd_code::Point3> square = {
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {1.0, 1.0, 0.0},
        {0.0, 1.0, 0.0},
    };
    auto empty = cki::link_pd_code::computePDCode(square, options);
    if (!empty.empty()) {
        std::cerr << "planar square should have no PD crossings\n";
        return 4;
    }

    auto parsed_link = cki::link_pd_code::parseLinkCoordinateText(R"(
1
4
0 0 0
1 0 0
1 1 0
0 1 0
)");
    auto parsed_empty = cki::link_pd_code::computePDCode(parsed_link, options);
    if (!parsed_empty.empty()) {
        std::cerr << "parsed planar component should have empty PD code\n";
        return 5;
    }
    options.encode_isolated_components = true;
    auto degenerate = cki::link_pd_code::computePDCode(parsed_link, options);
    if (cki::link_pd_code::formatPDCode(degenerate) != "[[1,1,2,2]]" ||
        !cki::link_pd_code::validatePDCode(degenerate)) {
        std::cerr << "isolated component encoding failed: "
                  << cki::link_pd_code::formatPDCode(degenerate) << "\n";
        return 6;
    }

    cki::link_pd_code::Options robust_options;
    robust_options.direction = cki::link_pd_code::Point3{0.0, 0.0, 1.0};
    robust_options.prefer_min_crossings = false;

    std::vector<cki::link_pd_code::Point3> endpoint_touch = {
        {0.0,  0.0, 0.0},
        {1.0,  0.0, 0.0},
        {1.0, -1.0, 1.0},
        {1.0,  1.0, 1.0},
    };
    bool endpoint_rejected = false;
    try {
        (void)cki::link_pd_code::computePDCode(endpoint_touch, robust_options);
    } catch (const cki::link_pd_code::ProjectionError&) {
        endpoint_rejected = true;
    }
    if (!endpoint_rejected) {
        std::cerr << "endpoint projection crossing was accepted\n";
        return 7;
    }

    std::vector<cki::link_pd_code::Point3> overlap = {
        {0.0, 0.0, 0.0},
        {2.0, 0.0, 0.0},
        {1.0, 0.0, 1.0},
        {3.0, 0.0, 1.0},
    };
    bool overlap_rejected = false;
    try {
        (void)cki::link_pd_code::computePDCode(overlap, robust_options);
    } catch (const cki::link_pd_code::ProjectionError&) {
        overlap_rejected = true;
    }
    if (!overlap_rejected) {
        std::cerr << "overlapping projected segments were accepted\n";
        return 8;
    }

    return 0;
}
''', encoding="utf-8")

        cmd = auxiliary_compile_command(cxx, source, output)
        proc = run(cmd, timeout=120)
        if proc.returncode != 0 and os.name != "nt":
            cmd = auxiliary_compile_command(cxx, source, output, ["-lstdc++fs"])
            proc = run(cmd, timeout=120)
        if proc.returncode != 0:
            raise AssertionError(f"auxiliary module compile failed\nstdout={proc.stdout}\nstderr={proc.stderr}")

        result = run([str(output)], timeout=30)
        if result.returncode != 0:
            raise AssertionError(
                f"auxiliary module runtime failed with {result.returncode}\n"
                f"stdout={result.stdout}\nstderr={result.stderr}"
            )


def assert_default_builds_all_tools(cxx_arg: str | None, portable: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="cki_build_all_") as tmp:
        build_dir = Path(tmp) / "build"
        cmd = [sys.executable, str(ROOT / "build.py"), "--build-dir", str(build_dir)]
        if cxx_arg:
            cmd += ["--cxx", cxx_arg]
        if portable:
            cmd += ["--portable"]
        proc = run(cmd, timeout=480)
        if proc.returncode != 0:
            raise AssertionError(f"default all-target build failed\nstdout={proc.stdout}\nstderr={proc.stderr}")

        knot_exe = build_dir / f"cpp_knot_indexer{EXE_SUFFIX}"
        che_exe = build_dir / CHE_TO_COORD_EXE
        link_exe = build_dir / LINK_PD_CODE_EXE
        missing = [str(path) for path in [knot_exe, che_exe, link_exe] if not path.exists()]
        if missing:
            raise AssertionError(f"default build did not create all executables: {missing}")
        assert_data_folder(build_dir / "data")

        molecule = r'''
LAMMPS data file

4 atoms
4 bonds

Atoms

1 0 0 0
2 1 0 0
3 1 1 0
4 0 1 0

Bonds

1 1 1 2
2 1 2 3
3 1 3 4
4 1 4 1
'''
        che = run([str(che_exe)], stdin=molecule, timeout=30)
        if che.returncode != 0:
            raise AssertionError(f"che_to_coord executable failed\nstdout={che.stdout}\nstderr={che.stderr}")
        che_lines = [line.strip() for line in che.stdout.splitlines() if line.strip()]
        if len(che_lines) != 6 or che_lines[0] != "1" or che_lines[1] != "4":
            raise AssertionError(f"unexpected che_to_coord link output: {che.stdout!r}")

        atom_id = run([str(che_exe), "--format", "atom-id"], stdin=molecule, timeout=30)
        if atom_id.returncode != 0 or not atom_id.stdout.lstrip().startswith("1 "):
            raise AssertionError(
                f"che_to_coord atom-id output failed\nstdout={atom_id.stdout}\nstderr={atom_id.stderr}"
            )

        square_pd = run([str(link_exe), "--direction", "0", "0", "1"], stdin=che.stdout, timeout=30)
        if square_pd.returncode != 0 or square_pd.stdout.strip() != "[]":
            raise AssertionError(
                f"link_pd_code planar square failed\nstdout={square_pd.stdout}\nstderr={square_pd.stderr}"
            )

        crossing_link = """1
4
-1 -1 0
1 1 0
1 -1 1
-1 1 1
"""
        crossing_pd = run(
            [str(link_exe), "--direction", "0", "0", "1", "--first-generic"],
            stdin=crossing_link,
            timeout=30,
        )
        if crossing_pd.returncode != 0 or not crossing_pd.stdout.strip().startswith("[["):
            raise AssertionError(
                f"link_pd_code crossing output failed\nstdout={crossing_pd.stdout}\nstderr={crossing_pd.stderr}"
            )

        wrapper_dir = Path(tmp) / "wrapper-build"
        for script, exe_name in [
            (ROOT / "src" / "che_to_coord" / "build.py", CHE_TO_COORD_EXE),
            (ROOT / "src" / "link_pd_code" / "build.py", LINK_PD_CODE_EXE),
        ]:
            wrapper_cmd = [sys.executable, str(script), "--build-dir", str(wrapper_dir)]
            if cxx_arg:
                wrapper_cmd += ["--cxx", cxx_arg]
            if portable:
                wrapper_cmd += ["--portable"]
            wrapper = run(wrapper_cmd, timeout=240)
            if wrapper.returncode != 0:
                raise AssertionError(
                    f"module wrapper build failed for {script}\n"
                    f"stdout={wrapper.stdout}\nstderr={wrapper.stderr}"
                )
            if not (wrapper_dir / exe_name).exists():
                raise AssertionError(f"module wrapper did not create {wrapper_dir / exe_name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run pure C++ indexer smoke tests.")
    parser.add_argument("--exe", default=str(DEFAULT_EXE), help="Executable to test.")
    parser.add_argument("--cxx", help="Compiler to pass to build.py when building.")
    parser.add_argument("--rebuild", action="store_true", help="Force rebuilding before tests.")
    parser.add_argument("--no-build", action="store_true", help="Do not build missing executable.")
    parser.add_argument("--portable", action="store_true", help="Build with build.py --portable.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    exe = Path(args.exe).resolve()
    if not args.no_build:
        build_if_needed(exe, args.cxx, args.rebuild, args.portable)
    if not exe.exists():
        raise SystemExit(f"ERROR: executable not found: {exe}")
    assert_data_folder(exe.parent / "data")

    for case in CASES:
        assert_case(exe, case)
        print(f"PASS {case['name']}")
    assert_custom_data_folder(exe)
    print("PASS data-folder")
    assert_sqlite_data_source(exe)
    print("PASS sqlite-data-source")
    assert_missing_default_data_fails(exe)
    print("PASS missing-default-data")
    assert_timeout_degrades(exe)
    print("PASS timeout-cli-contract")
    assert_simplify_worker(exe)
    print("PASS simplify-worker")
    assert_auxiliary_modules(args.cxx)
    print("PASS auxiliary-modules")
    assert_default_builds_all_tools(args.cxx, args.portable)
    print("PASS default-build-all-tools")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
