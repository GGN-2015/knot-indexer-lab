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

## Runtime Data

The runtime data folder uses the upstream text layout:

```text
homfly/sorted_HOMFLY-PT.txt
khovanov/sorted_khovanov.txt
knotname-reg/
```

SQLite, PD_m, and generated invariant index commands are not supported in this
version. PD-code and 3D-coordinate lookup still compute invariants and search
the upstream text maps. Name-to-PD lookup is unavailable in this data mode.

## Windows Notes

The server uses the upstream Unicode path helpers from `cpp_knot_indexer`. On
Windows, command line paths are read from the native Unicode command line, so
options such as `--data-folder` and `--web-root` can point at non-ASCII folders.
