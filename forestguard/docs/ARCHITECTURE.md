# ForestGuard — Architecture

## 1. Overview

ForestGuard is a single-process HTTP server that ingests sensor telemetry,
stores it durably on an SSD, evaluates fire risk, raises alerts, and serves a
dashboard. It is intentionally small and dependency-free so it runs unattended
on a Raspberry Pi 5 in the field.

```
node ──POST──► [ingest] ──► [validate] ──► [store] ──► [score] ──► [alert]
                                              │            │           │
                                              ▼            ▼           ▼
                                          SQLite       node.level   events +
                                         (readings)                 notify +
                                              │                     cloud outbox
   monitor loop (every FG_MONITOR_INTERVAL_S):│
     • flag offline nodes  • drain cloud outbox  • prune old readings
```

Two long-lived background threads run alongside the HTTP threads:

- **monitor** — offline detection, retention pruning, and (in redundancy mode)
  draining the cloud outbox.
- **notifier** — delivers alerts to Telegram / webhook off the request path.

## 2. Components

| Module | Responsibility |
|---|---|
| `config.py` | Load settings from env / `.env`; risk thresholds. |
| `models.py` | Validate + normalize incoming telemetry (no pydantic). |
| `db.py` | SQLite access (WAL, one locked connection). |
| `risk.py` | Pure fire-risk scoring from a node's latest metrics. |
| `service.py` | Ingest orchestration, alert state machine, monitor tick. |
| `notify.py` | Telegram / webhook / log channels (background worker). |
| `cloud.py` | Store-and-forward mirror to AWS (redundancy mode). |
| `server.py` | HTTP router, REST API, static dashboard, background threads. |
| `security.py` | Constant-time API-key and admin-token checks. |

## 3. Data model (SQLite)

Readings are stored **long / narrow** — one row per (node, timestamp, metric).
This is what makes the server agnostic to each node's sensor mix: a node can send
`temp_c` today and add `pm25` tomorrow with no schema change.

```
nodes(id, name, lat, lon, meta, created_at, last_seen,
      last_level, last_score, last_notified_level, last_notified_at)
readings(id, node_id, ts, metric, value)          -- indexed by (node,ts) & (node,metric,ts)
events(id, ts, node_id, level, type, message, data)
outbox(id, created_at, kind, payload, attempts, next_attempt, sent_at)
```

- **nodes** is the current-state table (one row per node, updated in place).
- **readings** is the append-only time series (pruned by retention).
- **events** is the immutable alert / lifecycle log.
- **outbox** is the cloud mirror queue (redundancy mode only).

WAL mode + `synchronous=NORMAL` gives durable writes and non-blocking reads,
which is exactly the read-heavy-dashboard / append-heavy-ingest pattern here.

## 4. Fire-risk model

The scorer (`risk.py`) is a **transparent heuristic**, not a certified detector.
It blends the signals a node actually reports into a 0–100 score, then maps that
to a level, with two hard overrides so a real event is never averaged away.

Each signal is normalized to 0..1 by a linear ramp (clamped):

| Signal | Ramp (default) | Notes |
|---|---|---|
| `heat` | `temp_c` 25 °C → 50 °C | hotter = higher |
| `dryness` | `humidity` 45 % → 15 % | **reversed** band: drier = higher |
| `smoke` | `smoke` 120 → 600 | raw ADC-style value or ppm |
| `gas` | `gas` 100 → 500 | raw ADC-style value |

Blended score:

```
score = 100 · Σ(componentᵢ · weightᵢ) / Σ(weightᵢ)      over present signals only
default weights: heat 0.30, dryness 0.20, smoke 0.35, gas 0.15
```

Level mapping and overrides:

```
score ≥ 75 → critical    │  override 1: flame ≥ threshold  → critical
score ≥ 50 → warning     │  override 2: smoke ≥ hard-warning → at least warning
score ≥ 25 → elevated    │
else       → ok          │
```

Because the mean is taken **only over signals that are present**, a node that
reports a single alarming value (e.g. only `temp_c = 48`) is scored on that
signal alone and can still reach a high level. Every threshold and weight is an
`FG_*` environment variable — calibrate them for your site (see `.env.example`).

The result carries human-readable `reasons` (e.g. *"low humidity (12%)",
"smoke spike ≥ 450"*), which flow straight into events, notifications, and the
dashboard so an alert always explains itself.

## 5. Alert state machine

Each node has a persisted `last_level`. On every reading the node is re-scored
and compared:

- **Any level change** writes an `events` row (both escalation and recovery) —
  this is the audit log, and it prevents duplicate spam because an unchanged
  level produces no event.
- **Notifications** (Telegram/webhook) fire only on:
  - escalation into `warning`, `critical`, or `offline`, and
  - recovery back to `ok` from one of those.
  Repeat notifications for the same node+level are throttled
  (`FG_NOTIFY_THROTTLE_S`).
- **Offline**: the monitor flags a node whose `last_seen` is older than
  `FG_OFFLINE_AFTER_S`. When it reports again, an `online` recovery event fires
  and scoring resumes from a neutral baseline (so you don't get a redundant
  "back online" *and* "cleared to OK" for the same moment).

## 6. Replace-AWS vs. redundancy

The same server covers both of the deployment intentions:

### `FG_MODE=local` — replace AWS
Nothing leaves the Pi. SQLite on the SSD is the system of record; the dashboard,
API, and alerts are all served locally. This is the simplest, most resilient
setup and needs no cloud account.

### `FG_MODE=redundancy` — local tier + cloud mirror
Every ingested reading and every event is **also** written to the on-SSD
`outbox`, and the cloud forwarder POSTs batches to `FG_CLOUD_URL` with an
`X-API-Key`. Key properties:

- **The Pi is always a complete copy.** Storage happens before the mirror; the
  cloud is a downstream replica, not a dependency.
- **Store-and-forward.** If AWS is unreachable, rows stay queued and drain
  automatically when it returns (exponential backoff, capped). This is what
  makes the Pi a genuine *failover* tier rather than a fragile passthrough.
- **Bounded.** `FG_CLOUD_MAX_ATTEMPTS` can cap retries; `0` means retry forever.

The cloud endpoint only has to accept:

```json
POST {FG_CLOUD_URL}
X-API-Key: {FG_CLOUD_KEY}
{ "source": "forestguard-pi",
  "items": [ {"kind": "telemetry", "payload": {…}},
             {"kind": "event",     "payload": {…}} ] }
```

A tiny API Gateway → Lambda that writes to DynamoDB/Timestream, or an IoT ingest
route, satisfies this. Because the contract is trivial, you can also point it at
a second Pi for Pi-to-Pi redundancy.

### Which way does failover go?
- **Cloud is primary today, Pi is the backup:** run `redundancy`; if AWS is
  down, operators use the Pi's own dashboard/API until the queue drains.
- **Pi is primary, cloud is the backup/archive:** also `redundancy` — the Pi
  serves everything and AWS holds a mirror for long-term storage or a public
  dashboard. Same mode, different emphasis.

## 7. Failure behavior

| Failure | Behavior |
|---|---|
| Power loss | WAL DB recovers on restart; systemd restarts the service. |
| Internet / AWS down | Local operation unaffected; outbox buffers, drains later. |
| Node dies | Flagged `offline` after the timeout; alert fires; recovery auto-detected. |
| Bad clock / junk payload | Rejected at ingest (422); never stored. |
| Notification endpoint down | Logged and dropped; never blocks ingest or the monitor. |
| Disk full | Writes fail loudly; retention pruning + SSD sizing keep it bounded. |
