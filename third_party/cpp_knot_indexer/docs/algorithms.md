# Algorithms

This document describes the algorithms currently present in the code tree. The
main executable accepts PD codes, computes HOMFLY-PT and integral Khovanov
invariants, optionally uses a simplified equivalent PD code to race those
invariant computations, and then looks up candidate knot names. The coordinate
conversion modules are standalone C++17 tools and libraries.

The implementation is intentionally process-oriented. Expensive or fragile
work runs in child processes so the parent process can enforce deadlines,
cancel losing workers, and keep `Ctrl+C` shutdown predictable on Windows,
Linux, and macOS.

## PD Code Normalization

The main indexer parses PD codes from text, validates integer labels, and
renumbers nonconsecutive labels into a compact canonical range. This lets the
lookup path accept equivalent PD codes that use arbitrary positive label names.

Accepted input may be a compact list form such as:

```text
[[1,5,2,4],[3,1,4,6],[5,3,6,2]]
```

or a `PD[X[...], ...]` style string. The parser extracts integers, groups them
into crossing quadruples, and rejects labels that are nonpositive or do not
appear exactly twice. The formatter emits one canonical list representation
used by all worker processes.

The HOMFLY-PT path rebuilds component traversal from the PD graph before
calling the polynomial engine. This avoids SageMath-style failures where a
consistently reversed knot orientation can produce the mirror polynomial or an
orientation-sensitive error.

## HOMFLY-PT

The HOMFLY-PT worker uses the vendored public-domain Jenkins/Marco HOMFLY
implementation under `third_party/libhomfly`.

Local integration details:

- The C sources are compiled as C++ in one compiler invocation.
- A local `gc.h` shim avoids requiring Boehm GC at runtime.
- Each computation runs in a worker process, so temporary allocations are
  process-local and released when the worker exits.
- The formatter mirrors the `L` exponent and orders terms to match the database
  strings used by this project.

The worker output is a normalized polynomial string. The main process uses that
string to query `data/homfly/sorted_HOMFLY-PT.txt`.

The HOMFLY-PT worker receives a canonical PD string from the parent process,
reparses it, applies the local PD simplification used by the HOMFLY adapter,
builds the input expected by `libhomfly`, and writes exactly one normalized
polynomial string to its output file.

## Khovanov Homology

The Khovanov worker uses the vendored MIT-licensed `GGN-2015/cppkh`
implementation under `third_party/cppkh`, which follows the JavaKh-style
integer Khovanov computation path.

The worker output is a normalized integral Khovanov string. The main process
uses that string to query `data/khovanov/sorted_khovanov.txt`.

The Khovanov worker also receives canonical PD text from the parent process.
It calls the vendored `cppkh` backend through the local C ABI wrapper and
writes exactly one normalized Khovanov string to its output file.

## PD-Code Simplification

The simplification worker uses the vendored MIT-licensed
`GGN-2015/cpp-pd-code-simplify` header under
`third_party/cpp-pd-code-simplify`.

The simplifier runs the upstream reducer through
`pdcode_simplify::reduce_pd_code`. Its main stages are:

1. Remove local R1 moves, true R2 bigons, and nugatory crossings.
2. Search for mid-simplification witnesses.
3. Apply accepted witnesses and repeat until stable, resource-limited, or
   cancelled.
4. Canonicalize the final display PD code.

The worker converts the final `PD[X[...], ...]` text back into this project's
canonical `[[...]]` representation before returning it to the parent process.
If the simplified PD is identical to the original canonical PD, no simplified
invariant workers are started.

## Simplification-Aware Invariant Race

Lookup starts three worker processes in parallel:

- HOMFLY-PT on the original canonical PD code
- Khovanov on the original canonical PD code
- PD-code simplification using the vendored `cpp-pd-code-simplify` backend

When the user passes `--ban-simplify`, the third worker is not started. In that
mode the lookup path computes HOMFLY-PT and Khovanov only from the original
input PD code and never starts simplified-PD invariant workers.

If simplification returns a different PD code, the main process starts
additional HOMFLY-PT and Khovanov workers on the simplified PD code for any
invariant type that has not already succeeded.

For each invariant type, original and simplified computations race. The first
successful HOMFLY-PT worker wins and any other running HOMFLY-PT worker is
terminated. Khovanov uses the same rule. This preserves correctness because
simplification is used only to obtain an equivalent, cheaper PD input for the
same invariant calculation; the lookup still uses HOMFLY-PT and Khovanov
strings as before.

The parent process implements the scheduler with explicit worker handles:

```text
start original_homfly(original_pd)
start original_khovanov(original_pd)
start simplify(original_pd)

while work remains:
  if simplify finishes with a different PD:
    start simplified_homfly(simplified_pd) unless HOMFLY-PT already won
    start simplified_khovanov(simplified_pd) unless Khovanov already won

  if any HOMFLY-PT worker succeeds:
    keep that result
    terminate any other HOMFLY-PT worker

  if any Khovanov worker succeeds:
    keep that result
    terminate any other Khovanov worker

  if both invariant types have succeeded:
    terminate simplify if it is still running
```

Failure is not contagious across invariant types. A failed original HOMFLY-PT
attempt does not prevent a simplified HOMFLY-PT attempt from being used later,
and a failed HOMFLY-PT attempt does not affect Khovanov. Cancelled losing
workers are diagnostics only; they are not treated as computation failures.

## Timeout and Failure Degradation

The public `--timeout SEC` value is a deadline for the full lookup pipeline.
When the deadline expires, all still-running workers are terminated. Results
that completed before the deadline remain usable.

If one invariant type fails or times out and the other succeeds, the main
process still performs lookup using the successful invariant. If both invariant
types fail, the lookup fails. `--timeout 0` disables the deadline.

This design keeps slow or pathological invariant computations from blocking the
whole command indefinitely.

`Ctrl+C` sets a shared interrupted flag in the parent process. The parent
terminates active workers and exits with status `130`. Worker processes install
the same signal/console handlers so cooperative cancellation can also stop
long-running simplification loops.

## Candidate Lookup

The database layer maps invariant strings to candidate knot names using the
text files in `data/homfly/` and `data/khovanov/`. Name normalization uses
`data/knotname-reg/` to account for mirrors and naming equivalences in the
dataset.

When both invariants succeed, the final result is the intersection of the
HOMFLY-PT candidate set and the Khovanov candidate set. If only one invariant
succeeds, the final result is the unique candidate set from that invariant.

The lookup order is:

1. Query the Khovanov text map when Khovanov is available.
2. Query the HOMFLY-PT text map when HOMFLY-PT is available.
3. Intersect the two candidate sets when both invariants are available;
   otherwise use the single successful invariant set.

Names pass through `NameNormalizer`, which applies the `knotname-reg` mirror
and name-pair data before final sorting and de-duplication.

## Molecule Data to Coordinates

`src/che_to_coord` implements the molecule data parser. It reads `Atoms` and
`Bonds` sections, accepts compact `id x y z` rows and common LAMMPS atom row
layouts, then validates that every connected component of the bond graph is a
degree-2 cycle.

The output is an ordered coordinate link. Single-component callers can keep
using the loop API; multi-component callers use `CoordinateLink` and the link
format accepted by `link_pd_code`. Invalid input produces
`cki::che_to_coord::ParseError` instead of assertion failures.

## Coordinates to PD Code

`src/link_pd_code` is derived from `GGN-2015/link-pd-code` under the MIT
license and refactored as standalone C++17 library code.

The algorithmic path is:

1. Project one or more ordered 3D polygon components to a candidate 2D plane.
2. Reject non-generic projections.
3. Find projected segment intersections with an active sweep over AABB ranges
   and y-interval pruning.
4. Sort crossing occurrences along each oriented component.
5. Assign strand labels.
6. Encode each crossing from the incoming under-strand and clockwise half-edge
   order.

Local fixes compared with the upstream project:

- deterministic multi-direction projection search instead of a short random
  retry loop
- explicit exceptions instead of user-facing `assert` failures
- endpoint crossings, overlapping projected segments, tied over/under heights,
  and multiple crossings at one projected point are treated as non-generic
  projection failures
- segment parameters are computed from the 2D line equations instead of using
  distance ratios
- interval arithmetic filters determinant, range, and height calculations before
  accepting floating-point decisions
- exact scaled rational arithmetic is used as a fallback for near-degenerate
  projected segment intersections, so endpoint and overlap classification does
  not depend only on raw floating-point comparisons
- isolated no-crossing components return no PD crossing by default, with an
  option to reproduce upstream-style degenerate isolated component encoding

The exact rational fallback is intentionally not used for every segment pair.
The common path still uses active-sweep AABB pruning, y-interval filtering, and
double arithmetic for clearly separated cases. Exact arithmetic is invoked only
when the interval filter marks the determinant or unit-interval boundary
classification as uncertain.
