# che_to_coord

Pure C++17 parser for the molecule data files used by the old
`che_data_to_spatial_coord` path. The module reads `Atoms` and `Bonds` sections,
validates that the bond graph is one connected degree-2 cycle, and returns 3D
points ordered along that cycle.

The parser accepts atom rows in these forms:

- `id x y z`
- old LAMMPS-style rows where `x y z` are fields 4-6

Malformed rows, duplicate atoms, duplicate bonds, unknown bond endpoints, and
non-cycle graphs raise `cki::che_to_coord::ParseError` with a line-aware message
where possible.

This module is intentionally not linked into the main knot indexer executable
yet. Include `che_to_coord.hpp` directly to use the header-only implementation.
`che_to_coord.cpp` contains the standalone command line interface.

Build it with:

```sh
python src/che_to_coord/build.py
```

See `docs/che-to-coord.md` for the command line contract.
