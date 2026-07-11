# link_pd_code

`link_pd_code` converts ordered closed 3D polygonal link components into a PD
code. The implementation is pure C++17, derived from
`GGN-2015/link-pd-code`, and is available as both a standalone executable and a
header-only library through `src/link_pd_code/link_pd_code.hpp`.

## Build

Build all project executables from the repository root:

```sh
python build.py
```

Build only this tool:

```sh
python build.py --target link_pd_code
```

Or use the module wrapper:

```sh
python src/link_pd_code/build.py
```

The output is `build/link_pd_code` on Unix-like systems and
`build/link_pd_code.exe` on Windows.

## Command Line

```sh
build/link_pd_code --input coords.txt --output pd.txt
```

If `--input` is omitted, the tool reads from stdin. If `--output` is omitted,
the tool writes to stdout.

Options:

- `--input, -i PATH`: read coordinates from `PATH`.
- `--output, -o PATH`: write the PD code to `PATH`.
- `--max-attempts N`: number of projection directions to try. Default: `256`.
- `--epsilon VALUE`: numeric tolerance. Default: `1e-10`.
- `--direction X Y Z`: use one explicit projection direction.
- `--first-generic`: stop after the first successful generic projection.
- `--prefer-min-crossings`: search all attempts and keep a projection with
  fewer crossings. This is the default.
- `--encode-isolated-components`: encode a no-crossing component as a
  degenerate PD crossing instead of returning no crossing for it.
- `--help, -h`: print usage.

## Input

The coordinate input format is:

```text
component_count
point_count_for_component_0
x y z
...
point_count_for_component_1
x y z
...
```

Each component is treated as a closed polygon: the final point is connected
back to the first point. Components must contain at least three finite points,
and zero-length segments are rejected.

## Output

The output is a PD code in the same bracketed format accepted by
`cpp_knot_indexer`:

```text
[[1,2,2,1]]
```

An isolated unknot component returns `[]` by default. Use
`--encode-isolated-components` when an upstream-style degenerate encoding is
needed.

## Projection Strategy

The tool projects 3D coordinates to a plane, detects segment intersections,
rejects non-generic diagrams, assigns strand labels along oriented components,
and formats crossings from the incoming under-strand. The implementation uses
bounding-box pruning, interval checks, and exact integer fallback for brittle
2D segment cases such as near-parallel segments and endpoint intersections.

By default it tries deterministic candidate directions and keeps a successful
projection with fewer crossings. `--direction` is useful for reproducible tests
or when the caller already knows a good generic projection direction.

## Library API

Include the header directly:

```cpp
#include "link_pd_code.hpp"
```

Important symbols are in `cki::link_pd_code`:

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
