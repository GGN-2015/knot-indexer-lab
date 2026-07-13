# Testing Manual

## Local Integration Test

Build the server and run the compact, offline integration suite:

```sh
python test.py --rebuild
```

This suite checks invariant computation, worker memory failure isolation,
persistent task history, the root-only web application, and both ordered-row
and CHE/LAMMPS coordinate parsing.

## hybrid_knot_indexer Dataset

The dedicated runner uses the coordinate dataset from
[`TopologicalKnotIndexer/hybrid_knot_indexer`](https://github.com/TopologicalKnotIndexer/hybrid_knot_indexer).
It sends every case through `POST /api/index_coord_3d`, so each test covers CHE
parsing, 3D projection, PD simplification, HOMFLY-PT and Khovanov computation,
and candidate-name lookup.

Run the pinned 22-case dataset with:

```sh
python tests/hybrid_knot_indexer.py --rebuild
```

The 22 input files from commit
`b959fda15f76ab7bf6eb02571a5dbd237024b65b` are committed under
`tests/data/hybrid_knot_indexer/che_data/`. The default test is therefore
offline and deterministic. Each case has a maximum computation time of 20
minutes. Upstream provenance is recorded in
`tests/data/hybrid_knot_indexer/UPSTREAM.md`.

The expected name is taken from the case directory. A case passes when that
name is present in the returned candidates; additional candidates are allowed
because HOMFLY-PT and Khovanov invariants do not uniquely distinguish all
knots.

## Local Upstream Checkout

Use an existing checkout instead of downloading the pinned archive:

```sh
python tests/hybrid_knot_indexer.py --source ../hybrid_knot_indexer
```

`--source` may point to the repository root, its `src/che_data` directory, or a
directory with the same expected-name subdirectory layout.

## Selected Cases

List the available cases:

```sh
python tests/hybrid_knot_indexer.py --list
```

Run one or more selected names:

```sh
python tests/hybrid_knot_indexer.py --case K3a1 --case mK8a7
```

Download and test the pinned upstream revision instead of the built-in copy:

```sh
python tests/hybrid_knot_indexer.py --upstream
```

Test another upstream revision:

```sh
python tests/hybrid_knot_indexer.py --upstream --revision main --refresh
```

Downloaded revisions are cached below `.cache/hybrid_knot_indexer/` and are
not committed automatically.

## Running Server

Test a server that is already running locally or remotely:

```sh
python tests/hybrid_knot_indexer.py --server-url http://127.0.0.1:5000
```

The selected server must expose the coordinate lookup API and accept
CHE/LAMMPS input. `--timeout` controls the per-case HTTP timeout. When the test
runner starts a local server, the same value is passed to the server's
computation timeout option.

## JSON Report

Write a report suitable for later comparison or automation:

```sh
python tests/hybrid_knot_indexer.py --json-report build/hybrid-test-report.json
```

The process exits with `0` when all selected cases pass, `1` when at least one
case fails, and `2` for setup, download, build, or server startup errors.
