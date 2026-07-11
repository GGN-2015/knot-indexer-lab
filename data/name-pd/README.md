# PD_m Name-To-PD Database

Place `PD_m_3-16.sorted.txt` here:

```text
data/name-pd/PD_m_3-16.sorted.txt
```

Expected record format is:

```text
[K3a1|[[1,5,2,4],[3,1,4,6],[5,3,6,2]]]
```

`build.py` copies this folder to `build/data/name-pd/` incrementally.

For fast runtime lookup, import the text file into SQLite:

```sh
build/knot_indexer_lab_server --build-sqlite
```

This creates:

```text
data/name-pd/PD_m_3-16.sqlite
```

Candidate lookup by invariant uses generated records in SQLite when that file
exists. Generate or extend those records with:

```sh
build/knot_indexer_lab_server --build-pd-index
```

SQLite invariant indexing uses parallel compute workers, batched SQLite
transactions, and periodic progress output with `ETA HH:MM:SS`. Tune large
builds with:

```sh
build/knot_indexer_lab_server --build-pd-index --index-workers 8 --index-batch-size 512
```

Invariant indexing is limited by `--max-crossing`. The default is total
crossing number <= 14, and the largest accepted value is 16. Composite names
are still supported: the total is the sum of the crossing numbers of all
comma-separated prime factors.

If SQLite is not present, `--build-pd-index` writes the fallback TSV file:

```text
data/name-pd/PD_m_3-16.invariants.tsv
```

Without `PD_m_3-16.sorted.txt`, the server only has the built-in `K0a1` and
`K3a1` name fallbacks.
