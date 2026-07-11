# knot-indexer-lab

`knot-indexer-lab` is a pure C++17 web server for knot lookup. The web UI keeps
the same Bootstrap/Vue layout as the earlier `knot-indexer-lab`, while the
calculation path uses the pure C++ implementation from
[`GGN-2015/cpp_knot_indexer`](https://github.com/GGN-2015/cpp_knot_indexer).

The site supports:

- lookup by knot name when a name-to-PD database is available
- lookup by PD code
- conversion from ordered 3D coordinates to PD code
- in-memory task monitoring with cancellation for running computations
- browser task-session recovery through a UUID cookie and WebSocket updates
- HOMFLY-PT polynomial calculation
- integral Khovanov homology calculation
- candidate knot-name lookup from HOMFLY-PT and Khovanov invariants

Knot Genus and Knot Volume are intentionally not present in this version.

## Build

Use the Python build script from the repository root:

```sh
python build.py
```

The script looks for a g++-style compiler (`g++`, `clang++`, or `c++`) and
builds:

```text
build/knot_indexer_lab_server
```

On Windows the executable is:

```text
build/knot_indexer_lab_server.exe
```

The build script also copies `data/` and `web/` beside the executable.

Useful options:

```sh
python build.py --clean
python build.py --debug
python build.py --cxx clang++
python build.py --show-command
```

## Run

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

The task monitor is available from the main page and directly at:

```text
http://127.0.0.1:5000/tasks.html
```

The server assigns each browser a random `kil_client_id` UUID cookie. The main
page and task monitor subscribe to `/ws/tasks`, so refreshing the page keeps the
same browser task session and restores the latest submitted task.

Server options:

```text
--host ADDRESS       IPv4 address to bind. Default: 0.0.0.0
--port PORT          TCP port. Default: 5000
--data-folder PATH   Runtime folder containing name-pd/ and knotname-reg/
--web-root PATH      Runtime web asset folder
--timeout SEC        Worker timeout, capped at 1200 seconds
--build-sqlite       Import PD_m_3-16.sorted.txt into SQLite, then exit
--build-pd-index     Generate invariant records in SQLite or TSV fallback, then exit
--index-limit N      Limit newly imported or indexed records in build modes
--index-workers N    Parallel PD_m invariant build workers. Default: half of CPU cores
--index-batch-size N SQLite invariant rows per write transaction. Default: 256
--index-progress-seconds N
                     Progress/ETA refresh interval. Default: 5
```

## Runtime Data

The retrieval database is `PD_m_3-16.sorted.txt`, not the original
`third_party/cpp_knot_indexer/data` folder. The vendored
`cpp_knot_indexer` project is used for the HOMFLY-PT, Khovanov, PD parsing,
and coordinate-to-PD algorithms only.

Install the PD database at:

```text
data/name-pd/PD_m_3-16.sorted.txt
```

`build.py` copies this local `data/` folder to `build/data/` incrementally. The
server includes a tiny fallback for `K0a1` and `K3a1`; broader name lookup needs
`PD_m_3-16.sorted.txt` or the generated SQLite database.

For efficient lookup, first import the text database into SQLite:

```sh
build/knot_indexer_lab_server --build-sqlite
```

On Windows:

```powershell
.\build\knot_indexer_lab_server.exe --build-sqlite
```

This writes:

```text
data/name-pd/PD_m_3-16.sqlite
```

The server prefers SQLite when it exists. Without SQLite, it falls back to a
streaming text-file index.

Candidate lookup by PD code uses generated invariant records:

```sh
build/knot_indexer_lab_server --build-pd-index
```

On Windows:

```powershell
.\build\knot_indexer_lab_server.exe --build-pd-index
```

For smoke tests or batched generation, use:

```sh
build/knot_indexer_lab_server --build-pd-index --index-limit 100
```

SQLite invariant builds are parallel by default. The builder streams unindexed
records through a bounded work queue, computes HOMFLY-PT and Khovanov in
multiple worker lanes, writes successful rows in SQLite transactions, and prints
periodic progress with `ETA HH:MM:SS`.

Useful tuning options:

```sh
build/knot_indexer_lab_server --build-pd-index --index-workers 8 --index-batch-size 512
```

Each build worker can launch several child processes for the invariant
pipeline, so set `--index-workers` according to available CPU and memory.

When `PD_m_3-16.sqlite` is present, invariant records are written into its
`invariants` table. Otherwise, the fallback writer creates
`PD_m_3-16.invariants.tsv` beside `PD_m_3-16.sorted.txt`. Each HOMFLY-PT and
Khovanov worker is capped at 1200 seconds.

Knot names are canonicalized before lookup. Legacy names from `name_pair.txt`
are mapped to modern `K...` names; mirror names keep an `m` prefix unless the
prime knot is listed in `amphichiral_list.txt`, in which case the `m` prefix is
removed automatically. Composite knot factors are normalized and sorted.

## Timeout Model

Each PD computation request runs HOMFLY-PT and Khovanov in isolated worker
processes. Workers run concurrently and each worker is capped at 1200 seconds
by default. A request can still use one invariant if the other one fails or
times out.

## API

The web UI uses these endpoints:

```text
GET  /api/index_knot_name/<base64-name>
GET  /api/index_pd_code/<base64-pd-code>
POST /api/index_coord_3d
GET  /api/tasks
POST /api/tasks/<task-id>/cancel
WS   /ws/tasks
GET  /api/knot_name2pd_code/<base64-name>
GET  /api/pd_code2homflypt/<base64-pd-code>
GET  /api/pd_code2khovanov/<base64-pd-code>
GET  /api/pd_code2knot_name/<base64-pd-code>
POST /api/coord_3d2pd_code
GET  /api/last_build_info
```

JSON responses use:

```json
{"status":"success","message":"..."}
```

or:

```json
{"status":"error","message":"..."}
```

## Third-Party Code

The `third_party/cpp_knot_indexer` tree is vendored from
[`GGN-2015/cpp_knot_indexer`](https://github.com/GGN-2015/cpp_knot_indexer).
It includes the HOMFLY-PT, Khovanov, and PD-code simplification
implementations used by this server. This vendored copy was refreshed from
upstream commit `b1c606ddf01a6a0d733e050dd1059a65694d179f`.

The server also uses the upstream Unicode path helpers. On Windows, command
line paths are read from the native Unicode command line, so options such as
`--data-folder` and `--web-root` can point at non-ASCII folders.

The invariant pipeline now follows the upstream strategy: start HOMFLY-PT and
Khovanov on the original PD code, run PD simplification in parallel, and retry
missing invariants on the simplified PD code when useful. SQLite is vendored
from the official amalgamation package and is compiled into the server. See the
vendored licenses for details.

## Citation

If you use this project in academic work, please cite it as:

```bibtex
@software{knot_indexer_lab_cpp,
  title        = {knot-indexer-lab: A Pure C++ Knot Retrieval Server},
  author       = {GGN-2015},
  year         = {2026},
  url          = {https://github.com/GGN-2015/knot-indexer},
  note         = {Pure C++ implementation with PD_m retrieval data, SQLite indexing, HOMFLY-PT polynomial computation, and Khovanov homology computation}
}
```
