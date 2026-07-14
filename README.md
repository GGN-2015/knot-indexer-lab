# knot-indexer-lab

`knot-indexer-lab` is a pure C++17 web server for knot lookup. The web UI keeps
the same Bootstrap/Vue layout as the earlier `knot-indexer-lab`, while the
calculation path uses the pure C++ implementation from
[`TopologicalKnotIndexer/cpp_knot_indexer`](https://github.com/TopologicalKnotIndexer/cpp_knot_indexer).

Knot Genus and Knot Volume are intentionally not present in this version.

The server supports lookup by knot name or PD code and converts ordered 3D
polygon coordinates to PD notation with its built-in C++ projection engine.
The web interface is responsive on desktop and narrow mobile screens, including
stacked input controls, readable long invariants, card-style task history, and
two-finger pinch zoom in the PD diagram viewer.

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

Run the integration tests:

```sh
python test.py --rebuild
```

Run the 22 coordinate cases from `hybrid_knot_indexer`:

```sh
python tests/hybrid_knot_indexer.py --rebuild
```

The pinned upstream test data is included in this repository, so this command
does not require a network connection. See the [Testing Manual](docs/testing.md)
for local-dataset, selected-case, alternate-upstream, remote-server, timeout,
and JSON report options.

Open Task Monitor from the navigation bar. The lookup interface and task
monitor are two views of the same root page, so the browser URL remains `/`.

Compute requests use a memory-aware FIFO queue. The server detects host and
Linux cgroup limits, reserves memory for itself and the operating system, and
applies a hard limit to each isolated compute worker. A computation can fail
with `resource_exhausted`, but the server remains available and records the
failure in its persistent task history.

Generate an SVG diagram from a PD code without starting the server:

```sh
build/knot_indexer_lab_server --render-pd-svg --input code.txt --output diagram.svg
```

On Windows:

```powershell
.\build\knot_indexer_lab_server.exe --render-pd-svg --input code.txt --output diagram.svg
```

Runtime lookup data uses the upstream `cpp_knot_indexer` text layout:

```text
data/homfly/sorted_HOMFLY-PT.txt
data/khovanov/sorted_khovanov.txt
data/knotname-reg/
data/name-pd/prime_knots_3-11.txt
```

`build.py` copies this `data/` folder beside the executable. Name-to-PD lookup
loads the compact prime-knot table in C++, applies mirror transformations for
`mK...` factors, and constructs comma-separated composite knots with connected
sums. SQLite and the large pre-expanded PD_m database are not needed. PD and
3D-coordinate lookup continue to retrieve candidate names from the HOMFLY-PT
and Khovanov text tables.

On Windows, replace `build/knot_indexer_lab_server` with
`.\build\knot_indexer_lab_server.exe`.

## Manuals

- [CLI Manual](docs/cli.md): build script and server options.
- [Runtime Data Manual](docs/runtime-data.md): upstream text data layout.
- [Algorithm Manual](docs/algorithm.md): lookup pipeline, SVG diagram generation, timeout model, name canonicalization, task recovery, and third-party code.
- [API Manual](docs/api.md): HTTP endpoints, WebSocket updates, and response shapes.
- [3D Coordinate Manual](docs/coordinates.md): coordinate format, trefoil sample, projection behavior, and API usage.
- [Testing Manual](docs/testing.md): offline integration tests and the `hybrid_knot_indexer` coordinate dataset runner.

## Citation

If you use this project in academic work, please cite it as:

```bibtex
@software{knot_indexer_lab_cpp,
  title        = {knot-indexer-lab: A Pure C++ Knot Retrieval Server},
  author = {{GGN\_2015}},
  year         = {2026},
  url          = {https://github.com/TopologicalKnotIndexer/knot-indexer-lab},
  note         = {Pure C++ implementation with upstream text invariant data, HOMFLY-PT polynomial computation, and Khovanov homology computation}
}
```
