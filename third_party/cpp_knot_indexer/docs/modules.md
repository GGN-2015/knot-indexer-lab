# Modules

The repository is organized as an independent C++17 project. It does not import
Python modules from the older hybrid project and does not require SageMath at
runtime.

## Source Tree

`src/knot_indexer`

Main executable code. This includes PD parsing, worker process orchestration,
HOMFLY-PT integration, Khovanov integration, data lookup, timeout handling, and
interrupt handling.

Reusable code in this directory is exposed through `.hpp` headers. The main
`.cpp` file is the command line entrypoint.

`src/che_to_coord`

Standalone molecule-data-to-coordinate library. It parses molecule files with
`Atoms` and `Bonds` sections and returns validated ordered 3D coordinate
cycles for one or more link components. It also has its own `che_to_coord`
command line executable.

Use `#include "che_to_coord.hpp"` to consume the library directly. The matching
`.cpp` file contains only the command line interface.

`src/link_pd_code`

Standalone 3D-coordinate-to-PD-code library derived from
`GGN-2015/link-pd-code` under the MIT license. It supports one or more ordered
closed polygon components. This module is not linked into the main executable
yet, and it also has its own `link_pd_code` command line executable.

Use `#include "link_pd_code.hpp"` to consume the library directly. The matching
`.cpp` file contains only the command line interface.

`src/common`

Small shared helpers used by command line entrypoints. `path_utils.hpp` provides
native Unicode argv collection on Windows, UTF-8 diagnostic formatting, and the
SQLite filename conversion used by the main indexer. The public algorithm
headers do not require this directory unless an application wants to reuse the
same CLI path behavior.

`third_party/cppkh`

Vendored MIT-licensed Khovanov implementation from `GGN-2015/cppkh`.

`third_party/libhomfly`

Vendored public-domain HOMFLY implementation. It is compiled as C++ together
with the main executable sources.

`third_party/cpp-pd-code-simplify`

Vendored MIT-licensed header-only PD-code simplifier from
`GGN-2015/cpp-pd-code-simplify`.

`third_party/sqlite`

Vendored public-domain SQLite amalgamation used by the main executable when a
SQLite invariant data source is available.

`data`

Runtime lookup data. The data folder is copied beside the built executable by
`build.py`, but it is not embedded in the executable.

`docs`

Project documentation beyond the README QuickStart.

## Main Indexer Components

`database.hpp`

Loads invariant-to-name maps and normalizes knot names through
`knotname-reg`.

`sqlite_database.hpp`

Opens a read-only SQLite invariant database and queries the `invariants` table
by HOMFLY-PT, Khovanov, or the pair of both invariants.

`pd_code.hpp`

Parses and formats PD codes, validates labels, canonicalizes label numbering,
and provides the PD representation used by invariant workers.

`homfly_backend.hpp`

Converts parsed PD data into the form expected by the HOMFLY engine and formats
the resulting polynomial for database lookup.

`khovanov_backend.hpp`

Connects parsed PD data to the vendored `cppkh` computation path and formats
the resulting integral Khovanov invariant.

`pd_simplify_backend.hpp`

Connects parsed PD data to the vendored `cpp-pd-code-simplify` reducer and
formats the simplified PD code back into the project's canonical `[[...]]`
representation.

`process_runner.hpp`

Spawns worker processes, supports polling and cancellation, enforces pipeline
deadlines, captures stderr, and reports worker status to the main process.

`runtime_control.hpp`

Installs interrupt handlers and exposes the shared interrupted state used for
clean `Ctrl+C` shutdown.

`main.cpp`

Implements the public command line, default data folder resolution, worker
entrypoint, invariant lookup, and final stdout/stderr output contract.

## Auxiliary Coordinate Modules

`che_to_coord.hpp`

Public namespace: `cki::che_to_coord`.

Important API types and functions:

- `Point3`
- `OrderedPoint`
- `CoordinateLink`
- `ParseOptions`
- `ParseError`
- `parseCoordinateLoopText`
- `parseCoordinateLinkText`
- `readCoordinateLoopFile`
- `readCoordinateLinkFile`
- `positionsOnly`
- `formatCoordinateLoop`
- `formatCoordinateLink`
- `formatLinkCoordinateText`

`che_to_coord.cpp`

Standalone command line entrypoint for reading molecule data from a file or
stdin and writing ordered 3D coordinates.

`link_pd_code.hpp`

Public namespace: `cki::link_pd_code`.

Important API types and functions:

- `Point3`
- `Crossing`
- `Component`
- `Link`
- `Options`
- `ProjectionError`
- `parseLinkCoordinateText`
- `readLinkCoordinateFile`
- `computePDCode`
- `validatePDCode`
- `formatPDCode`

`link_pd_code.cpp`

Standalone command line entrypoint for reading ordered 3D link coordinates from
a file or stdin and writing a PD code.

## Citation

If you use this project in academic work, please cite it as software:

```bibtex
@software{cpp_knot_indexer,
  title        = {Pure C++ Knot Indexer},
  author       = {{GGN-2015}},
  year         = {2026},
  url          = {https://github.com/GGN-2015/cpp_knot_indexer},
  note         = {C++17 software for PD-code knot lookup using HOMFLY-PT and Khovanov invariants}
}
```
