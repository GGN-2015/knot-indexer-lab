# 3D Coordinate Manual

The 3D converter accepts one ordered polygonal knot. Each row contains three
finite decimal numbers:

```text
x y z
x y z
...
```

At least three points are required. Do not repeat the first point at the end;
the server adds the closing edge from the final point back to the first point.
Brackets, commas, and semicolons are accepted as separators, so a copied list
such as `[0,0,0]; [1,0,0]; [0,1,0]` is also valid. An optional leading point
count is accepted when it equals the number of following rows.

## Trefoil Example

This 12-point polygon is a sampled `(2,3)` torus knot:

```text
3.000000 0.000000 0.000000
1.000000 1.732051 1.000000
-0.500000 0.866025 0.000000
-2.000000 0.000000 -1.000000
-1.500000 -2.598076 0.000000
1.000000 -1.732051 1.000000
1.000000 0.000000 0.000000
1.000000 1.732051 -1.000000
-1.500000 2.598076 0.000000
-2.000000 0.000000 1.000000
-0.500000 -0.866025 0.000000
1.000000 -1.732051 -1.000000
```

The direct conversion endpoint returns a three-crossing PD code. The full
index endpoint then computes HOMFLY-PT and Khovanov invariants and identifies
the candidate as `K3a1`.

## HTTP Requests

Use `POST /api/coord_3d2pd_code` for conversion only or
`POST /api/index_coord_3d` for conversion followed by invariant lookup. Both
accept JSON with one `coord_3d` string:

```json
{"coord_3d":"3 0 0\n1 1.732051 1\n..."}
```

The projection search is deterministic. It rejects endpoint intersections,
overlapping projected segments, tied strand heights, and repeated crossings at
one projected point. Invalid input returns a JSON error describing the first
problem. A valid no-crossing polygon returns `[]`, the server's unknot PD.
