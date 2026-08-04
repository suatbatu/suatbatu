# ForestGuard Server 🌲

A **self-hosted web server and dashboard** for a wildfire / forest-monitoring
sensor network, built to run on a **Raspberry Pi 5 (8 GB) + NVMe SSD HAT**.

It ingests telemetry from field nodes (temperature, humidity, smoke, gas, flame,
battery…), scores each node's **fire risk**, raises alerts on smoke/heat spikes
and offline nodes, and serves a live dashboard — all from one small box on your
own network.

It is designed to **either replace a cloud (AWS) backend outright, or run
alongside it as a local redundancy tier** that keeps a complete copy of the data
and mirrors it to the cloud when reachable. One config flag picks the mode.

> Zero dependencies. The whole server is the Python 3 standard library — no pip,
> no framework, no ARM wheels to fight. If a Pi can run `python3`, it can run
> ForestGuard. That is deliberate: field hardware should not rot because a
> dependency broke.

---

## Why it's built this way

| Requirement | Design decision |
|---|---|
| Survive an internet / AWS outage | **Everything runs locally.** SQLite on the SSD is the source of truth; the cloud is optional. |
| "Replace AWS" *and* "be a redundancy" — without committing yet | **Two modes, one binary.** `FG_MODE=local` is fully self-contained; `FG_MODE=redundancy` also mirrors to AWS via a durable on-SSD **outbox** (store-and-forward). |
| Deploy on a Pi in the field, reliably | **Stdlib only** + a systemd unit + one install script. Nothing to `pip install`. |
| Different nodes, different sensors | **Schema-flexible telemetry** — a node reports whatever metrics it has; the risk model scores what's present. |
| Don't miss a real fire, don't cry wolf | **Transparent, tunable risk model** with flame + smoke-spike overrides and level hysteresis (alerts fire on state changes, not every reading). |
| Don't wear out the SD card | DB and outbox live on the **SSD**; retention pruning keeps it bounded. |

## Architecture

```
     Field sensor nodes (ESP32 / LoRa gateway)
     temp · humidity · smoke · gas · flame · battery
                     │  HTTP POST  (X-API-Key)
                     ▼
        ┌─────────────────────────────────────┐
        │      Raspberry Pi 5 (8 GB)           │
        │      ForestGuard server (stdlib)     │
        │                                      │
        │   ingest ─► risk score ─► alerts ────┼──► Telegram / webhook
        │      │            │                  │
        │      ▼            ▼                  │
        │   SQLite (WAL, on the SSD)  ◄── dashboard (web UI, SVG charts)
        │      │                               │
        │      ▼  outbox (redundancy mode)     │
        └──────┼───────────────────────────────┘
               │  HTTPS batch (store-and-forward)
               ▼
        AWS (API Gateway → Lambda/DynamoDB/Timestream)   ← optional
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the data flow, the risk
model, and how the "replace vs. redundancy" modes work.

## Quick start (on any machine)

```bash
cd forestguard

# 1. Run the server (open, in-memory-ish defaults; DB file in the cwd)
FG_INGEST_KEYS=KEY1 FG_ADMIN_TOKEN=secret python3 run.py

# 2. In another terminal, feed it simulated nodes incl. a fire scenario
python3 scripts/simulate.py --url http://localhost:8080 --key KEY1 \
        --nodes 5 --interval 3 --fire ridge-2

# 3. Open the dashboard
#    http://localhost:8080/   (enter the admin token "secret" via the 🔑 button)
```

You'll see five nodes; `ridge-2` climbs **OK → ELEVATED → WARNING → CRITICAL**
as its fire progresses, alerts appear, and the event log fills in.

## Deploy on the Raspberry Pi 5

```bash
sudo ./deploy/install.sh          # creates the service, points the DB at the SSD
sudo nano /etc/forestguard.env    # set FG_INGEST_KEYS and FG_ADMIN_TOKEN
sudo systemctl restart forestguard
journalctl -u forestguard -f
```

Full walkthrough — enabling PCIe for the NVMe HAT, mounting the SSD, TLS, and
turning on cloud redundancy — is in [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md).

## Sending telemetry from a node

Any device that can POST JSON works. Minimal payload:

```bash
curl -X POST http://<pi>:8080/api/v1/telemetry \
  -H "X-API-Key: KEY1" -H "Content-Type: application/json" \
  -d '{"node":"ridge-2","name":"Ridge East","lat":40.2,"lon":29.0,
       "metrics":{"temp_c":41.2,"humidity":18,"smoke":320,"gas":210,
                  "flame":0,"battery_v":3.9}}'
```

Batches (`{"readings":[...]}`) and a flat form (top-level numeric fields become
metrics) are also accepted. See [`docs/API.md`](docs/API.md) for the full API.

## Repository layout

```
forestguard/
  app/            The server (stdlib only)
    config.py       env/.env configuration
    db.py           SQLite storage (nodes, readings, events, outbox)
    models.py       telemetry validation
    risk.py         fire-risk scoring (heuristic, tunable)
    service.py      ingest + alert state machine + monitor
    notify.py       Telegram / webhook / log channels
    cloud.py        AWS forwarder (redundancy mode)
    server.py       HTTP server, REST API, static dashboard
    security.py     API-key + admin-token auth
    main.py         entrypoint
  web/            Dashboard (index.html, style.css, app.js) — no external deps
  scripts/
    simulate.py     virtual sensor network for demos / smoke tests
  deploy/
    forestguard.service   systemd unit (hardened)
    install.sh            Pi installer
    nginx.forestguard.conf optional TLS reverse proxy
  tests/          unittest suite (risk, models, service, live API)
  docs/           ARCHITECTURE · API · DEPLOYMENT · BOM
```

## Tests

```bash
cd forestguard
python3 -m unittest discover -s tests -t .
```

Covers the risk model, telemetry validation, the alert/offline state machine,
the cloud outbox, and a live end-to-end HTTP run (auth, ingest, history, static).

## Security posture (read before exposing it)

- **Ingestion** requires an `X-API-Key` when `FG_INGEST_KEYS` is set (do set it).
- **Dashboard + query/control** require a bearer `FG_ADMIN_TOKEN`. Reads can be
  opened without a token via `FG_DASHBOARD_PUBLIC=true` — LAN-only, never remote.
- **No built-in TLS.** ForestGuard serves plain HTTP; terminate TLS at nginx
  (config provided) or a VPN before anything leaves the LAN.
- Constant-time comparisons for keys/tokens; bad clocks and junk payloads are
  rejected at ingest.

## Status & scope

This is a working, tested foundation — not a certified fire-detection system.
The risk model is a transparent heuristic you are expected to calibrate for your
site and sensors. Treat its alerts as *decision support*, and keep a proven
escalation path (people, the fire service) in the loop.

## License

MIT — see the repository [`LICENSE`](../LICENSE). Provided **as-is, no warranty**.
