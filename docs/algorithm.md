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

## 3D Coordinates To PD Code

The 3D input is an ordered polygonal knot with one finite `x y z` point per
row. The closing segment from the last point to the first is implicit. The
pure C++ projection engine tests deterministic view directions, rejects
non-generic projections, and normally chooses a valid diagram with the fewest
crossings. It then sorts crossing events along the oriented polygon and emits
the four incident arc labels for each crossing.

The projection is rejected when a crossing lands on a segment endpoint, two
projected segments overlap, over/under heights tie, or multiple crossings
occupy one projected point. A no-crossing polygon produces `[]`, representing
the unknot in the server pipeline.
The [3D Coordinate Manual](coordinates.md) includes a tested trefoil polygon
and direct/full HTTP request details.

## Invariant Pipeline

Each PD computation request enters a FIFO admission queue. Once admitted, the
server runs PD simplification, HOMFLY-PT, and Khovanov in isolated child
processes, with no more than one compute child active at a time. A useful
simplified PD code is attempted before the original code. This sequential
pipeline deliberately trades throughput for a bounded memory peak.

The adaptive memory controller reads physical availability on Windows and
macOS, `/proc/meminfo` on Linux, and cgroup v1/v2 usage and limits when
present. It keeps the larger of 512 MiB or 20 percent available for the server
and operating system. Worker address space is limited with `RLIMIT_AS` on
POSIX systems and a Windows Job Object on Windows. Linux workers raise their
own `oom_score_adj`, so the kernel prefers terminating a compute child over
the server if host-wide pressure still reaches the OOM killer. At startup the
server also attempts to lower its own score to `-250`; Linux permits this when
the process has sufficient privilege and otherwise leaves the score unchanged.
The server also stops an active worker if free memory falls below half of its
reserve.

The default 1200-second deadline covers queue waiting, simplification, and all
invariant attempts. Memory allocation failure, a memory-limit kill, or an
unavailable memory budget produces `resource_exhausted`. Other successful
invariants remain usable when possible.

The result cache is an LRU bounded to 64 MiB by default and does not retain SVG
payloads. Queued and running tasks stay in memory; completed tasks are appended
to a framed disk history and removed from memory. The history reader validates
record boundaries, repairs an incomplete final record after a crash, and pages
backward without loading the full file. History responses are capped at 16 MiB.
The FIFO holds at most 16 waiting computations, Task Monitor stores at most
256 KiB of each displayed input, and HTTP/WebSocket connections are capped at
128 to prevent request traffic from becoming an independent memory spike.

The web application has one root URL. Lookup and Task Monitor remain mounted
as Vue views and the navigation bar switches their visibility without browser
navigation. Task Monitor loads completed pages through `/api`, receives active
task changes through `/ws/tasks`, and shows a spinner while a task is actively
computing.

## Candidate Lookup

Candidate lookup compares computed invariants against the upstream text maps:

- `data/homfly/sorted_HOMFLY-PT.txt`
- `data/khovanov/sorted_khovanov.txt`

Candidate names are filtered by `--max-crossing`, which defaults to 14 and
accepts values up to 16. Prime factors use the number after `K`, and composite
names sum all comma-separated factors. Mirror prefixes do not change the
crossing number.

## Name-To-PD Lookup

The runtime prime table contains all 801 standard prime knots with 3 through
11 crossings. The C++ loader validates every PD record and stores only the
non-mirror representative.

For each normalized factor, a leading `m` mirrors the prime diagram by swapping
the second and fourth entries of every PD crossing. To combine factors, the
server follows an oriented component cycle, cuts one arc in each diagram,
cross-connects the two pairs of endpoints, and renumbers the resulting single
component. Repeating this operation constructs the requested connected sum
without pre-generating a combinatorial database of composite knots.

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

On touch screens, opening the diagram viewer enables one-finger panning and
two-finger pinch zoom. Moving the pinch center pans while the distance between
the two pointers controls zoom from 25% through 800%. Desktop wheel zoom and
the viewer toolbar use the same bounds.

## Name Canonicalization

Knot names are normalized by the upstream `NameNormalizer`. Legacy names from
`name_pair.txt`, mirror rules from `need_mirror.txt`, and amphichiral entries
from `amphichiral_list.txt` are applied before composite factors are sorted.

## Browser Task Recovery

The server assigns each browser a random `kil_client_id` UUID cookie. The main
page and task monitor subscribe to `/ws/tasks`, so refreshing the page keeps the
same browser task session and restores the latest submitted task state.

## Third-Party Code

The `third_party/cpp_knot_indexer` tree is vendored from
[`GGN-2015/cpp_knot_indexer`](https://github.com/GGN-2015/cpp_knot_indexer).
It includes the HOMFLY-PT, Khovanov, and PD-code simplification
implementations used by this server. This vendored copy was refreshed from
upstream commit `e0daec4e0f9163065f9df004e7c6e926a92ebae7`.

The server no longer vendors or links SQLite.
