# SQLite Amalgamation

This directory contains the official SQLite amalgamation package used by
`cpp_knot_indexer` for optional read-only invariant lookup.

Source package:

```text
sqlite-amalgamation-3530300.zip
```

SQLite is used only as an embedded storage engine. Runtime data remains
external to the executable and can be replaced with `--data-folder` or
`--sqlite-db`.
