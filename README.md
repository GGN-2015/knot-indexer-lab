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

## Citation

If you use this project in academic work, please cite it as:

```bibtex
@software{knot_indexer_lab_cpp,
  title        = {knot-indexer-lab: A Pure C++ Knot Retrieval Server},
  author       = {GGN-2015},
  year         = {2026},
  url          = {https://github.com/GGN-2015/knot-indexer},
  note         = {Pure C++ implementation with upstream text invariant data, HOMFLY-PT polynomial computation, and Khovanov homology computation}
}
```
