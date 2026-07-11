# Runtime Data Manual

The retrieval database is `PD_m_3-16.sorted.txt`, not the original
`third_party/cpp_knot_indexer/data` folder. The vendored `cpp_knot_indexer`
project is used for HOMFLY-PT, Khovanov, PD parsing, PD simplification, and
coordinate-to-PD algorithms.

## Layout

Install the PD database at:

```text
data/name-pd/PD_m_3-16.sorted.txt
```

Expected record format:

```text
[K3a1|[[1,5,2,4],[3,1,4,6],[5,3,6,2]]]
```

`build.py` copies this local `data/` folder to `build/data/` incrementally. The
server includes a tiny fallback for `K0a1` and `K3a1`; broader name lookup needs
`PD_m_3-16.sorted.txt` or the generated SQLite database.

## SQLite Name Lookup

For efficient lookup, first import the text database into SQLite:

```sh
build/knot_indexer_lab_server --build-sqlite
```

This creates:

```text
data/name-pd/PD_m_3-16.sqlite
```

The server prefers SQLite when it exists. Without SQLite, it falls back to a
streaming text-file index.

## Invariant Index

Candidate lookup by PD code uses generated invariant records:

```sh
build/knot_indexer_lab_server --build-pd-index
```

When `PD_m_3-16.sqlite` is present, invariant records are written into its
`invariants` table. Otherwise, the fallback writer creates:

```text
data/name-pd/PD_m_3-16.invariants.tsv
```

SQLite invariant indexing is parallel by default. The builder streams
unindexed records through a bounded work queue, computes HOMFLY-PT and Khovanov
in multiple worker lanes, writes successful rows in SQLite transactions, and
prints periodic progress with `ETA HH:MM:SS`.

The batch index builder computes invariants directly from the stored PD_m PD
code. It does not run PD simplification because the PD_m records are expected
to be already minimized for this build stage.

Useful tuning options:

```sh
build/knot_indexer_lab_server --build-pd-index --index-workers 8 --index-batch-size 512
```

For smoke tests or partial batches:

```sh
build/knot_indexer_lab_server --build-pd-index --index-limit 100
```

## Git Policy

The full PD_m data files are intentionally ignored by Git:

```text
data/name-pd/PD_m_3-16.sorted.txt
data/name-pd/PD_m_3-16.sqlite
data/name-pd/PD_m_3-16.invariants.tsv
third_party/cpp_knot_indexer/data/
```

Keep these files local unless a separate large-file distribution process is
used.
