# che_to_coord

`che_to_coord` converts a molecule data file into an ordered closed 3D
coordinate loop. The implementation is pure C++17 and can be used either as a
standalone executable or as a header-only library through
`src/che_to_coord/che_to_coord.hpp`.

## Build

Build all project executables from the repository root:

```sh
python build.py
```

Build only this tool:

```sh
python build.py --target che_to_coord
```

Or use the module wrapper:

```sh
python src/che_to_coord/build.py
```

The output is `build/che_to_coord` on Unix-like systems and
`build/che_to_coord.exe` on Windows.

## Command Line

```sh
build/che_to_coord --input molecule.data --output coords.txt
```

If `--input` is omitted, the tool reads from stdin. If `--output` is omitted,
the tool writes to stdout.

Options:

- `--input, -i PATH`: read molecule data from `PATH`.
- `--output, -o PATH`: write coordinates to `PATH`.
- `--format link|atom-id`: choose the output format. The default is `link`.
- `--no-prefer-atom-one`: start the ordered cycle at the lowest atom id instead
  of preferring atom id `1`.
- `--help, -h`: print usage.

## Input

The parser expects `Atoms` and `Bonds` sections. It accepts common LAMMPS atom
row layouts, including:

- `id x y z`
- `id type x y z`
- `id mol type x y z`
- `id mol type q x y z`

The bond graph must be one connected cycle. Every atom must have degree `2`.
Malformed rows, duplicate atoms, duplicate bonds, unknown bond endpoints, and
non-cycle graphs fail with a line-aware error where possible.

## Output

The default `link` format is designed to pipe directly into `link_pd_code`:

```text
1
4
0 0 0
1 0 0
1 1 0
0 1 0
```

The first line is the component count. The second line is the point count for
the component. Each remaining line is `x y z`.

Use `--format atom-id` to preserve atom ids:

```text
1 0 0 0
2 1 0 0
3 1 1 0
4 0 1 0
```

## Library API

Include the header directly:

```cpp
#include "che_to_coord.hpp"
```

Important symbols are in `cki::che_to_coord`:

- `Point3`
- `OrderedPoint`
- `ParseOptions`
- `ParseError`
- `parseCoordinateLoopText`
- `readCoordinateLoopFile`
- `positionsOnly`
- `formatCoordinateLoop`
