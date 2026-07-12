# Runtime Data Manual

The server uses the upstream `cpp_knot_indexer` text invariant layout together
with a compact prime-knot PD table. SQLite, the pre-expanded PD_m database, and
generated invariant index build modes are not needed.

## Layout

A valid runtime data folder contains:

```text
data/homfly/sorted_HOMFLY-PT.txt
data/khovanov/sorted_khovanov.txt
data/knotname-reg/
data/name-pd/prime_knots_3-11.txt
```

`build.py` copies `data/` beside the generated server executable. You can also
pass an explicit folder at runtime:

```sh
build/knot_indexer_lab_server --data-folder /path/to/data
```

On Windows:

```powershell
.\build\knot_indexer_lab_server.exe --data-folder C:\path\to\data
```

## Lookup Behavior

For PD-code and 3D-coordinate requests, the server computes HOMFLY-PT and
Khovanov invariants, then searches the upstream text maps for candidate knot
names.

For name requests, the server normalizes each comma-separated factor and looks
up its non-mirror prime PD code. A leading `m` mirrors that factor by swapping
the second and fourth arc positions at each crossing. The C++ connected-sum
implementation cuts one oriented arc in each diagram, cross-connects the
endpoints, and canonically renumbers the combined PD code.

The table covers all standard prime knots with 3 through 11 crossings, which
matches the prime knots referenced by the bundled invariant maps. `K0a1`
resolves to the empty PD code. Composite knots can have a larger total crossing
number as long as they remain within `--max-crossing`.

## Crossing Limit

Candidate names are filtered by `--max-crossing`, which defaults to 14 and
accepts values up to 16. Composite names sum all comma-separated prime factor
crossing numbers. Mirror prefixes do not change the crossing number.

## Git Policy

The text invariant files and compact prime table are small enough to vendor
with the project. Large local PD_m and SQLite files are ignored and are not
used by this runtime mode.
