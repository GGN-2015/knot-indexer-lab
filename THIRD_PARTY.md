# Third-Party Components

This repository is self-contained and uses no Git submodules. Its build inputs
are committed as ordinary files so the server can be built without cloning
nested repositories.

- `third_party/cpp_knot_indexer` is an ordinary-file snapshot of
  [`TopologicalKnotIndexer/cpp_knot_indexer`](https://github.com/TopologicalKnotIndexer/cpp_knot_indexer)
  commit `e0daec4e0f9163065f9df004e7c6e926a92ebae7`, including the licenses and
  required backend source snapshots used by that project.
- `third_party/pd_code_to_diagram` contains the MIT-licensed C++ renderer from
  `pd-code-to-diagram` 0.1.8. This copy includes the local correction that makes
  the two-dimensional dot product add both coordinate products; the same fix is
  maintained in the independent
  [`TopologicalKnotIndexer/pd_code_to_diagram`](https://github.com/TopologicalKnotIndexer/pd_code_to_diagram)
  repository.
- `web/static/bootstrap-5.3.2` and `web/static/vue3` contain the browser assets
  used by the offline web interface. Their upstream license notices remain in
  the distributed source files.

The pinned `hybrid_knot_indexer` coordinate fixtures are test data rather than
runtime code; their exact upstream commit is documented in
`tests/data/hybrid_knot_indexer/UPSTREAM.md`.
