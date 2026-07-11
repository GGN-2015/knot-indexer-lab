# link_pd_code

Pure C++17 polygonal-link projection module derived from
`GGN-2015/link-pd-code` under the MIT license. It reads one or more ordered
closed 3D polygon components, projects them to a plane, rejects non-generic
projections, assigns strand labels along oriented components, and returns a PD
code.

The implementation keeps the upstream algorithmic shape:

- project 3D coordinates to a generic 2D diagram
- find projected segment intersections
- sort crossing occurrences along each component
- encode each crossing from the incoming under-strand and clockwise half-edge
  order

The local version fixes several brittle parts from the upstream code:

- no `assert`-based user-input failures
- deterministic multi-direction projection search instead of a small random
  retry loop
- exact segment parameters from the 2D line equations instead of distance ratios
- explicit rejection of endpoint crossings, overlapping projected segments, tied
  over/under heights, and multiple crossings at the same projected point
- active-sweep AABB and y-interval pruning before exact segment intersection

By default an isolated no-crossing component contributes no PD crossing, so an
unknot returns `[]`. Set `Options::encode_isolated_components` to reproduce the
upstream-style degenerate component encoding.

This module is intentionally not linked into the main knot indexer executable
yet. Include `link_pd_code.hpp` directly to use the header-only implementation.
`link_pd_code.cpp` contains the standalone command line interface.

Build it with:

```sh
python src/link_pd_code/build.py
```

See `docs/link-pd-code.md` for the command line contract.
