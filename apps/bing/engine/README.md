# Bing Engine

The pure-Python scanning core behind Bing. It powers three things from one
codebase and **zero third-party dependencies**:

- a **CLI** (`bing …`)
- a **web dashboard** (Fing-style UI)
- a **REST API** (consumed by the Bing mobile app in "Engine mode")

Requires only **Python 3.8+**.

## Install

```bash
# Run in place, nothing to install:
python -m bing_engine scan

# …or put `bing` on your PATH:
pip install -e .
bing scan
```

## CLI

```
bing scan                 discover devices on your network
bing net                  local + public network info
bing ports <host>         scan a host's TCP ports (+ security grade)
bing ping <host>          measure latency (ICMP if privileged, else TCP)
bing trace <host>         trace the route to a host
bing dns <name>           DNS lookups (A/AAAA/MX/TXT/NS/CNAME/SOA)
bing wol <mac>            wake a device (Wake-on-LAN)
bing speed                internet speed test
bing vendor <mac>         MAC-address vendor lookup
bing web                  launch the web dashboard
bing update-oui           download the full IEEE OUI vendor database
```

Every command takes `--json` for machine-readable output, so the CLI doubles as
a scripting tool:

```bash
bing scan --json | jq '.devices[] | select(.device_type=="Printer")'
bing ports 192.168.1.10 --ports top100 --json
bing dns example.com -t MX --json
```

Useful flags:

```bash
bing scan --cidr 10.0.0.0/24 --timeout 0.5   # scan a specific subnet
bing scan --no-ports --no-resolve            # fastest liveness-only sweep
bing ports host --ports 1-1024               # explicit range
bing ping host --tcp --port 80               # force TCP ping on a chosen port
```

## Web dashboard

```bash
bing web --open                 # http://127.0.0.1:8787
bing web --host 0.0.0.0 --port 8787   # reachable from your phone / LAN
```

The dashboard streams devices live as they're found (Server-Sent Events),
shows a network summary, and opens a per-device drawer with a deep port +
security scan. Serve it on `0.0.0.0` and point the Bing mobile app's
"Engine mode" at `http://<this-machine-ip>:8787`.

## REST API

All endpoints return JSON and set permissive CORS headers.

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/net?public=1` | local + public network info |
| GET | `/api/scan?cidr=&ports=1&resolve=1` | discover devices (blocking) |
| GET | `/api/scan/stream` | discover devices (SSE, live) |
| GET | `/api/device?ip=` | deep single-device scan (ports + security) |
| GET | `/api/ports?host=&ports=common` | port scan |
| GET | `/api/ping?host=&count=4` | latency |
| GET | `/api/trace?host=` | traceroute |
| GET | `/api/dns?name=&type=A` | DNS lookup |
| GET | `/api/speed?quick=1` | speed test |
| GET | `/api/vendor?mac=` | MAC vendor |
| GET/POST | `/api/wol?mac=` | Wake-on-LAN |

## How it works (no magic, no deps)

- **Discovery** — TCP-probes a set of "liveness" ports across the subnet in
  parallel. A completed handshake *or* a refused connection both prove a host is
  up, so even fully-firewalled devices are found. The probes populate the kernel
  ARP cache, which is then read from `/proc/net/arp` to attach MAC addresses.
- **MAC vendor** — a curated built-in OUI table (~150 common makers) covers the
  offline case; `bing update-oui` fetches the full IEEE registry (~35k entries)
  to `~/.cache/bing/`.
- **DNS** — a hand-written resolver builds/parses DNS wire format directly, so
  A/AAAA/MX/TXT/NS/CNAME/SOA lookups work with no `dig`/`nslookup` present.
- **Ping** — uses an ICMP datagram/raw socket when the process is allowed one
  (root or `net.ipv4.ping_group_range`), otherwise times a TCP connect.
- **Traceroute** — sends UDP probes with increasing IP TTL and reads ICMP
  time-exceeded via a raw socket (needs root/`CAP_NET_RAW`); without it, reports
  reachability to the destination and says tracing needs privileges.
- **Security** — flags exposed risky services (Telnet, unauthenticated
  databases, remote-admin ports, …) and produces a 0-100 grade.

Privileged features degrade gracefully — nothing hard-fails for lack of root.

## Tests

```bash
python -m unittest discover -s tests -v
```

41 tests cover the OUI DB, DNS wire codec, port-spec parsing, live TCP scan/ping
against a local listener, device classification, security scoring and WoL.

## Layout

```
bing_engine/
  discovery.py   device discovery (the flagship scan)
  ports.py       port scanner + service DB + banner grab
  oui.py         MAC-vendor lookup + IEEE updater
  dnsr.py        pure-Python DNS resolver
  ping.py        ICMP / TCP ping
  traceroute.py  TTL-based traceroute
  wol.py         Wake-on-LAN
  netinfo.py     local/public network context
  security.py    exposed-service risk assessment
  speedtest.py   internet speed test
  cli.py         command-line interface
  server.py      http.server REST API + dashboard host
  web/           dashboard SPA (index.html / styles.css / app.js)
```
