# Pure C++ Knot Indexer

## QuickStart

This is an independent C++17 code tree for looking up knots from PD codes. It
computes HOMFLY-PT and integral Khovanov invariants in isolated worker
processes, races them against the same invariants computed after PD-code
simplification, and then looks up candidate knot names from external data
files or SQLite. It does not require SageMath at runtime.

Build everything from the repository root:

```sh
python build.py
```

This creates `cpp_knot_indexer`, `che_to_coord`, and `link_pd_code` under
`build/`, and copies the external `data/` folder next to `cpp_knot_indexer`.

Run a lookup:

```sh
build/cpp_knot_indexer --pd-code "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]" --timeout 60
```

On Windows, the default executable path is:

```sh
build/cpp_knot_indexer.exe --pd-code "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]" --timeout 60
```

Run the regression tests:

```sh
python test.py --rebuild
```

Manuals:

- [Command Line](docs/command-line.md): user-facing options and output contract.
- [Algorithms](docs/algorithms.md): implementation details for normalization,
  simplification, invariant racing, lookup, and coordinate conversion.
- [Packaging](docs/packaging.md): Python build script, compiler flags, and
  runtime data packaging.
- [Modules](docs/modules.md): source tree and public `.hpp` entrypoints.
- [che_to_coord](docs/che-to-coord.md): molecule data to ordered coordinates.
- [link_pd_code](docs/link-pd-code.md): ordered 3D coordinates to PD code.
- [Citation](docs/modules.md#citation): BibTeX entry for academic use.
