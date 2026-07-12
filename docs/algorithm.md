# Algorithm Manual

This manual describes the runtime behavior of the pure C++ server. User-facing
setup commands are documented in the [CLI Manual](cli.md), and data layout is
documented in the [Runtime Data Manual](runtime-data.md).

## Supported Operations

The site supports:

- lookup by PD code
- conversion from ordered 3D coordinates to PD code
- task monitoring with cancellation for running computations
- browser task-session recovery through a UUID cookie and WebSocket updates
- HOMFLY-PT polynomial calculation
- integral Khovanov homology calculation
- candidate knot-name lookup from HOMFLY-PT and Khovanov invariants

Knot Genus and Knot Volume are intentionally not computed.

## Invariant Pipeline

Each PD computation request runs isolated worker processes for HOMFLY-PT and
Khovanov. The server starts HOMFLY-PT and Khovanov on the original PD code,
runs PD simplification in parallel, and retries missing invariants on the
simplified PD code when useful.

The default worker timeout is 1200 seconds. A request can still use one
invariant if the other one fails or times out.

## Candidate Lookup

Candidate lookup compares computed invariants against the upstream text maps:

- `data/homfly/sorted_HOMFLY-PT.txt`
- `data/khovanov/sorted_khovanov.txt`

Candidate names are filtered by `--max-crossing`, which defaults to 14 and
accepts values up to 16. Prime factors use the number after `K`, and composite
names sum all comma-separated factors. Mirror prefixes do not change the
crossing number.

## Name Canonicalization

Knot names are normalized by the upstream `NameNormalizer`. Legacy names from
`name_pair.txt`, mirror rules from `need_mirror.txt`, and amphichiral entries
from `amphichiral_list.txt` are applied before composite factors are sorted and
deduplicated.

## Browser Task Recovery

The server assigns each browser a random `kil_client_id` UUID cookie. The main
page and task monitor subscribe to `/ws/tasks`, so refreshing the page keeps the
same browser task session and restores the latest submitted task state.

## Third-Party Code

The `third_party/cpp_knot_indexer` tree is vendored from
[`GGN-2015/cpp_knot_indexer`](https://github.com/GGN-2015/cpp_knot_indexer).
It includes the HOMFLY-PT, Khovanov, and PD-code simplification
implementations used by this server. This vendored copy was refreshed from
upstream commit `81ad3c3`.

The server no longer vendors or links SQLite.
