# CLI Manual

This project is built and operated with Python and the generated C++ server
binary. All commands below are run from the repository root unless noted
otherwise.

## Build Script

Build the pure C++ server:

```sh
python build.py
```

The script looks for a g++-style compiler such as `g++`, `clang++`, or `c++`.
It writes the server executable to:

```text
build/knot_indexer_lab_server
```

On Windows:

```text
build/knot_indexer_lab_server.exe
```

The build script also copies `data/` and `web/` beside the executable.

Useful build options:

```sh
python build.py --clean
python build.py --debug
python build.py --cxx clang++
python build.py --show-command
python build.py --no-lto
python build.py --static-runtime
```

Full build options:

```text
--cxx CXX             C++ compiler command, for example g++ or clang++.
--build-dir DIR       Build output directory.
--output PATH         Output executable path.
--debug               Build with -O0 -g.
--native              Enable native CPU tuning when supported.
--no-lto              Disable link-time optimization.
--static-runtime      On Windows, link libstdc++ and libgcc statically.
--extra-cxxflag FLAG  Append an extra compiler flag.
--extra-ldflag FLAG   Append an extra linker flag.
--show-command        Print the compiler command.
--clean               Remove the build directory before compiling.
--skip-assets         Do not copy data/ and web/ after building.
```

## Server

Run the server:

```sh
build/knot_indexer_lab_server
```

On Windows:

```powershell
.\build\knot_indexer_lab_server.exe
```

Open:

```text
http://127.0.0.1:5000
```

Server options:

```text
--host ADDRESS       IPv4 address to bind. Default: 0.0.0.0
--port PORT          TCP port. Default: 5000
--data-folder PATH   Runtime folder containing homfly/, khovanov/, and knotname-reg/
--web-root PATH      Runtime web asset folder
--timeout SEC        Worker timeout, capped at 1200 seconds. Default: 1200
--max-crossing N     Maximum total crossing number. Default: 14, max: 16
--help, -h           Show help text
```

## PD SVG Generation

The server binary can also render a PD code to an SVG file without loading the
runtime invariant data or starting the HTTP server:

```sh
build/knot_indexer_lab_server --render-pd-svg --input code.txt --output diagram.svg
```

On Windows:

```powershell
.\build\knot_indexer_lab_server.exe --render-pd-svg --input code.txt --output diagram.svg
```

Inline PD input is also supported:

```sh
build/knot_indexer_lab_server --render-pd-svg --pd "[[4,2,5,1],[2,6,3,5],[6,4,1,3]]" --output trefoil.svg
```

SVG CLI options:

```text
--render-pd-svg      Render a PD code as SVG and exit.
--pd TEXT            Inline PD code. Mutually exclusive with --input.
--input PATH         File containing one PD code. Mutually exclusive with --pd.
--output PATH        SVG output path. If omitted, SVG is written to stdout.
```

The SVG renderer uses only the PD-code diagram layout path. It does not compute
HOMFLY-PT, Khovanov homology, candidate names, or simplification.

## Web 3D Coordinate Input

The server UI and HTTP API accept an ordered closed polygon as `x y z` rows.
At least three points are required; the closing edge is added automatically.
For example, a 12-point polygonal sample of the standard torus-knot
parameterization can be submitted directly in the 3D Coordinates panel. The
server performs projection and PD conversion in C++ before running the normal
invariant pipeline. See the API and algorithm manuals for accepted separators
and projection rejection rules, or use the complete sample in the
[3D Coordinate Manual](coordinates.md).

## Runtime Data

The runtime data folder uses the upstream text layout:

```text
homfly/sorted_HOMFLY-PT.txt
khovanov/sorted_khovanov.txt
knotname-reg/
name-pd/prime_knots_3-11.txt
```

SQLite, PD_m, and generated invariant index commands are not supported in this
version. Name lookup uses the prime PD table plus pure C++ mirror and connected
sum operations. PD-code and 3D-coordinate lookup compute invariants and search
the upstream text maps as before.

## Windows Notes

The server uses the upstream Unicode path helpers from `cpp_knot_indexer`. On
Windows, command line paths are read from the native Unicode command line, so
options such as `--data-folder` and `--web-root` can point at non-ASCII folders.
