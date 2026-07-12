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

## SVG Diagram Generation

Every successful lookup response includes an SVG diagram generated from the
returned PD code. The same renderer is available from the command line with
`--render-pd-svg`.

The renderer follows the `pd-code-to-diagram` matrix model:

- positive matrix cells are strand arc numbers
- `-1` means the vertical strand passes under the horizontal strand
- `-2` means the horizontal strand passes under the vertical strand
- `0` is empty space

The server tries multiple deterministic layout seeds and keeps the compact
candidate with the lowest layout score. The score penalizes area, blank cells,
extra turns, unsupported local topology, and open endpoints. This keeps the
output stable while avoiding a visibly worse first layout when a better seed is
available.

SVG strands are drawn as vector paths on a white background. Positive matrix
cells are first traced into continuous arc paths, so a strand is emitted as one
`M/L/A` SVG path instead of many disconnected tile fragments. Straight runs use
`L` commands. Every 90-degree turn uses an SVG circular `A` command with radius
equal to half a tile, which makes the arc tangent to the adjacent horizontal and
vertical straight segments. Crossings are drawn in two layers: the under-strand
is drawn first, a white gap masks the crossing center, and the over-strand is
drawn on top.

Each crossing also renders the four adjacent arc numbers around the crossing.
The labels are taken directly from the neighboring positive matrix cells: top,
right, bottom, and left. Labels are drawn last with a white stroke behind the
red text so they remain readable over strands.

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
