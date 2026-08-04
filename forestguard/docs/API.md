# ForestGuard — HTTP API

Base URL: `http://<pi>:8080` (or behind your TLS reverse proxy).
All responses are JSON unless noted. Errors are `{"error": "..."}` with an
appropriate status code.

## Authentication

| Access | Requirement |
|---|---|
| Ingestion (`POST /api/v1/telemetry`) | `X-API-Key: <key>` — required when `FG_INGEST_KEYS` is set. |
| Reads (`GET /api/v1/*`) | `Authorization: Bearer <FG_ADMIN_TOKEN>` — unless `FG_DASHBOARD_PUBLIC=true`. |
| Control (`POST /api/v1/nodes/{id}`, `POST /api/v1/monitor/tick`) | `Authorization: Bearer <FG_ADMIN_TOKEN>` (always). |
| `GET /healthz` | none. |

---

## Ingestion

### `POST /api/v1/telemetry`
Submit one reading, a batch, or a flat form.

**Single:**
```json
{ "node": "ridge-2", "name": "Ridge East", "ts": 1785879365,
  "lat": 40.2, "lon": 29.0,
  "metrics": { "temp_c": 41.2, "humidity": 18, "smoke": 320,
               "gas": 210, "flame": 0, "battery_v": 3.9 } }
```

**Batch:** `{ "readings": [ {…}, {…} ] }` (max 500) — or a bare JSON array.

**Flat:** top-level numeric fields are promoted to metrics:
```json
{ "node": "ridge-2", "temp_c": 41.2, "humidity": 18 }
```

Field rules:
- `node` (string, required) — also accepted as `node_id` or `id`.
- `ts` (number, optional) — epoch seconds; milliseconds auto-detected; defaults
  to server time; rejected if > 3 days from now.
- `metrics` — object of `name → number` (bools/NaN/inf rejected). At least one.
- `name`, `lat`, `lon` optional; used to register / update the node.

**Response `202`:**
```json
{ "accepted": 1, "worst_level": "critical",
  "results": [ { "node": "ridge-2", "stored_metrics": 6,
                 "risk": { "score": 89.0, "level": "critical",
                           "components": {"heat":0.8,"dryness":0.9,"smoke":0.7,"gas":0.6},
                           "reasons": ["flame detected", "smoke spike ≥ 450"],
                           "flame": true } } ] }
```
Errors: `401` (bad/missing key), `422` (validation), `413` (body too large).

---

## Reads

### `GET /api/v1/summary`
Site-wide overview for the dashboard.
```json
{ "time": 1785879365.0, "node_count": 5, "online": 5, "offline": 0,
  "worst_level": "critical",
  "counts": {"ok": 4, "critical": 1},
  "active_alerts": [ { "id": "ridge-2", "name": "Ridge East", "level": "critical",
                       "score": 100.0, "reasons": [...], "online": true } ],
  "recent_events": [ { "ts": ..., "level": "critical", "message": "..." } ],
  "cloud": { "enabled": false, "mode": "local", "pending": 0, "sent": 0 } }
```

### `GET /api/v1/nodes`
`{ "nodes": [ <node view>, ... ] }`. A **node view**:
```json
{ "id": "ridge-2", "name": "Ridge East", "lat": 40.2, "lon": 29.0,
  "last_seen": 1785879365.0, "age_s": 4.1, "online": true,
  "level": "critical", "score": 100.0,
  "metrics": {"temp_c": 62.4, "humidity": 10, "smoke": 814, ...},
  "reasons": ["flame detected", ...], "flame": true }
```

### `GET /api/v1/nodes/{id}`
One node view, plus `"metrics_available": ["temp_c", "humidity", ...]`.
`404` if unknown.

### `GET /api/v1/nodes/{id}/history?metric=<m>&since=<s>&limit=<n>`
Time series for one metric, oldest-first.
- `metric` (required) — e.g. `temp_c`.
- `since` — absolute epoch, a `"3600s"` window, or a negative `-3600` (last hour).
- `limit` — max points (default 2000).
```json
{ "node": "ridge-2", "metric": "temp_c",
  "points": [ {"ts": 1785879000.0, "value": 28.2}, ... ] }
```

### `GET /api/v1/events?since=<s>&limit=<n>`
`{ "events": [ {id, ts, node_id, level, type, message, data}, ... ] }`
(newest first). `type` ∈ `risk_change | flame | offline | online | info`.

### `GET /api/v1/cloud/status`
`{ "enabled", "mode", "url_set", "pending", "sent", "last_success", "last_error" }`

### `GET /api/v1/config`
Redacted, secret-free view of the running configuration.

### `GET /healthz`
`{ "status": "ok", "version": "0.1.0", "time": ... }` — no auth (liveness probe).

---

## Control

### `POST /api/v1/nodes/{id}`
Update node metadata (admin). Body may include `name`, `lat`, `lon`, `meta`
(object). Returns the updated node view.

### `POST /api/v1/monitor/tick`
Force one monitor pass (offline sweep + cloud drain + prune). Handy for ops and
tests. Returns `{ "offline_flagged", "cloud_drained", "pruned" }`.

---

## Notes
- Responses set `Cache-Control: no-store`.
- The server speaks HTTP/1.1 with keep-alive; bodies are always length-delimited.
- Max request body is 5 MB (telemetry batches are far smaller).
