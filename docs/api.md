# API Manual

The web UI uses HTTP endpoints and a WebSocket task stream. Payloads embedded
in the URL are base64-encoded UTF-8 strings and then URL encoded.

## Lookup Endpoints

```text
GET  /api/index_knot_name/<base64-name>
GET  /api/index_pd_code/<base64-pd-code>
POST /api/index_coord_3d
```

These endpoints create a computation task and return a lookup result object.
The coordinate endpoint accepts JSON with a `coord_3d` string.

`coord_3d` contains either at least three ordered `x y z` rows or a single-loop
LAMMPS/CHE molecule file with `Atoms` and `Bonds` sections. The server closes
an ordered polygon by connecting the final point back to the first point. CHE
input is ordered from its bond graph. The server then searches deterministic
generic projection directions and converts the selected diagram to PD notation
before starting invariant lookup. Brackets, commas, and semicolons are accepted
as ordered-row separators. An optional leading point count is also accepted
when it matches the number of following rows.
See the [3D Coordinate Manual](coordinates.md) for a complete trefoil sample.

`/api/index_knot_name` resolves standard prime names such as `K11n185`, mirror
names such as `mK7a7`, and comma-separated connected sums such as
`K3a1,mK5a2`. It then runs the same invariant and candidate lookup pipeline as
a PD-code request.

## Direct Conversion Endpoints

```text
GET  /api/knot_name2pd_code/<base64-name>
GET  /api/pd_code2homflypt/<base64-pd-code>
GET  /api/pd_code2khovanov/<base64-pd-code>
GET  /api/pd_code2knot_name/<base64-pd-code>
POST /api/coord_3d2pd_code
```

`/api/coord_3d2pd_code` accepts the same JSON and returns only the generated PD
code. Invalid numbers, incomplete rows, endpoint crossings, overlapping
projected segments, and other non-generic projections return a JSON error.
`/api/knot_name2pd_code` performs only the pure C++ name-to-PD conversion and
returns the resulting PD code without computing invariants.

## Task Endpoints

```text
GET  /api/tasks
GET  /api/tasks/history/<cursor>
POST /api/tasks/<task-id>/cancel
WS   /ws/tasks
```

The server associates task state with the `kil_client_id` browser cookie. The
WebSocket stream sends queued and running tasks plus the latest task for the
current browser, which lets the browser recover state after a refresh.
The browser stays at `/`; switching between knot lookup and Task Monitor is a
client-side Vue view change and does not navigate to another HTML route.

Completed records are stored on disk. `/api/tasks/history/0` returns the newest
100 records. Use the returned `next_cursor` in the next request while
`has_more` is true. Task and invariant statuses can include `queued`,
`running`, `completed`, `failed`, `cancelled`, `timed_out`, and
`resource_exhausted`.

## Build Info

```text
GET /api/last_build_info
```

This endpoint returns plain text with the server build/runtime information shown
in the UI navigation modal.

## Simple JSON Responses

Some helper endpoints use:

```json
{"status":"success","message":"..."}
```

or:

```json
{"status":"error","message":"..."}
```

Task-producing lookup endpoints return richer JSON with fields for candidate
names, canonical PD code, HOMFLY-PT status/result/error, and Khovanov
status/result/error.
