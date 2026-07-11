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
--data-folder PATH   Runtime folder containing name-pd/ and knotname-reg/
--web-root PATH      Runtime web asset folder
--timeout SEC        Worker timeout, capped at 1200 seconds. Default: 1200
--build-sqlite       Import PD_m_3-16.sorted.txt into SQLite, then exit
--build-pd-index     Generate invariant records in SQLite or TSV fallback, then exit
--index-limit N      Limit newly imported or indexed records in build modes
--index-workers N    Parallel PD_m invariant build workers. Default: half of CPU cores
--index-batch-size N SQLite invariant rows per write transaction. Default: 256
--index-progress-seconds N
                     Progress/ETA refresh interval. Default: 5
--help, -h           Show help text
```

## Data Commands

Import the text name-to-PD database into SQLite:

```sh
build/knot_indexer_lab_server --build-sqlite
```

Build or extend the invariant index:

```sh
build/knot_indexer_lab_server --build-pd-index
```

Run a bounded smoke test:

```sh
build/knot_indexer_lab_server --build-pd-index --index-limit 100
```

Tune a large SQLite invariant build:

```sh
build/knot_indexer_lab_server --build-pd-index --index-workers 8 --index-batch-size 512
```

Each index worker launches HOMFLY-PT and Khovanov workers for the input PD
code. The batch index builder does not run PD simplification, so set
`--index-workers` according to available CPU and memory.

## Windows Notes

The server uses the upstream Unicode path helpers from `cpp_knot_indexer`. On
Windows, command line paths are read from the native Unicode command line, so
options such as `--data-folder` and `--web-root` can point at non-ASCII folders.
