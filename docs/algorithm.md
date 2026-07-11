# Algorithm Manual

This manual describes the runtime behavior of the pure C++ server. User-facing
setup commands are documented in the [CLI Manual](cli.md), and data layout is
documented in the [Runtime Data Manual](runtime-data.md).

## Supported Operations

The site supports:

- lookup by knot name when a name-to-PD database is available
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

Candidate lookup compares computed invariants against generated PD_m invariant
records. SQLite is preferred when `PD_m_3-16.sqlite` exists. A TSV fallback is
available when SQLite is not present.

The SQLite invariant builder is optimized for large PD_m data:

- one producer pages unindexed records from SQLite
- multiple compute workers run invariant calculations in parallel
- one writer batches successful rows into SQLite transactions
- progress output includes processed rows, written rows, failures, rate,
  elapsed time, and `ETA HH:MM:SS`
- secondary invariant indexes are rebuilt after a bulk build

The builder does not derive Khovanov results from mirror knots.

## Name Canonicalization

Knot names are canonicalized before lookup. Legacy names from `name_pair.txt`
are mapped to modern `K...` names. Mirror names keep an `m` prefix unless the
prime knot is listed in `amphichiral_list.txt`, in which case the `m` prefix is
removed automatically. Composite knot factors are normalized and sorted.

## Browser Task Recovery

The server assigns each browser a random `kil_client_id` UUID cookie. The main
page and task monitor subscribe to `/ws/tasks`, so refreshing the page keeps the
same browser task session and restores the latest submitted task state.

## Third-Party Code

The `third_party/cpp_knot_indexer` tree is vendored from
[`GGN-2015/cpp_knot_indexer`](https://github.com/GGN-2015/cpp_knot_indexer).
It includes the HOMFLY-PT, Khovanov, and PD-code simplification
implementations used by this server. This vendored copy was refreshed from
upstream commit `b1c606ddf01a6a0d733e050dd1059a65694d179f`.

SQLite is vendored from the official amalgamation package and is compiled into
the server. See the vendored licenses for details.
