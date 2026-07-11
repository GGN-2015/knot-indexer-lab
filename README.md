# knot-indexer-lab

`knot-indexer-lab` is a pure C++17 web server for knot lookup. The web UI keeps
the same Bootstrap/Vue layout as the earlier `knot-indexer-lab`, while the
calculation path uses the pure C++ implementation from
[`GGN-2015/cpp_knot_indexer`](https://github.com/GGN-2015/cpp_knot_indexer).

Knot Genus and Knot Volume are intentionally not present in this version.

## Quick Start

Build from the repository root:

```sh
python build.py
```

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

The task monitor is available from the main page and directly at:

```text
http://127.0.0.1:5000/tasks.html
```

For full name lookup, place the PD database here before building or running:

```text
data/name-pd/PD_m_3-16.sorted.txt
```

Then import it into SQLite:

```sh
build/knot_indexer_lab_server --build-sqlite
```

Build the invariant index for candidate lookup:

```sh
build/knot_indexer_lab_server --build-pd-index
```

By default this indexes knots with total crossing number <= 14, including
composite knots whose factor crossing numbers sum to <= 14. Use
`--max-crossing N` to choose a positive limit up to 16.

For a small smoke test:

```sh
build/knot_indexer_lab_server --build-pd-index --index-limit 100
```

On Windows, replace `build/knot_indexer_lab_server` with
`.\build\knot_indexer_lab_server.exe`.

## Manuals

- [CLI Manual](docs/cli.md): build script options, server options, and indexing commands.
- [Runtime Data Manual](docs/runtime-data.md): PD_m data layout, SQLite import, and invariant database generation.
- [Algorithm Manual](docs/algorithm.md): lookup pipeline, timeout model, name canonicalization, task recovery, and third-party code.
- [API Manual](docs/api.md): HTTP endpoints, WebSocket updates, and response shapes.

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
