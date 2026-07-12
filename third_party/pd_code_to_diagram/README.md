Vendored pd-code-to-diagram
===========================

This directory vendors the C++ layout headers from `pd-code-to-diagram` 0.1.8:

https://pypi.org/project/pd-code-to-diagram/

The upstream package is MIT licensed. The upstream license is copied in
`LICENSE`, and the upstream README is copied in `UPSTREAM_README.md`.

The server uses the upstream C++ matrix layout algorithm and renders that
matrix directly as SVG. It does not use the upstream Python or PNG rendering
path at runtime.
