# Runtime Data Manual

The server now uses the upstream `cpp_knot_indexer` text data layout. SQLite,
PD_m name-to-PD data, and generated invariant index build modes are not
supported in this version.

## Layout

A valid runtime data folder contains:

```text
data/homfly/sorted_HOMFLY-PT.txt
data/khovanov/sorted_khovanov.txt
data/knotname-reg/
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

Name-to-PD lookup is unavailable in this data mode because the upstream text
data does not include a PD-code table. The related HTTP endpoint returns a
clear error instead of falling back to PD_m or SQLite.

## Crossing Limit

Candidate names are filtered by `--max-crossing`, which defaults to 14 and
accepts values up to 16. Composite names sum all comma-separated prime factor
crossing numbers. Mirror prefixes do not change the crossing number.

## Git Policy

The upstream text files are small enough to vendor with the project. Large
local PD_m and SQLite files are ignored and are not used by this runtime mode.
