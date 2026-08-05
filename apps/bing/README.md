# Bing 📡

A **Fing-like network scanner** — discover every device on your network, see what
it is, scan its ports, check its security, and run the classic network toolbox
(ping, traceroute, DNS, Wake-on-LAN, speed test).

Bing ships in two parts that share one design and one JSON contract:

| Part | What it is | Runs on |
|---|---|---|
| **[`engine/`](engine/)** | A pure-Python scanning engine — CLI + web dashboard + REST API. Does real ARP-based discovery, MAC-vendor lookup, port/DNS/latency scanning. **Zero dependencies.** | Any computer, Raspberry Pi, NAS, home server |
| **[`mobile/`](mobile/)** | A cross-platform **Expo / React Native** app — one codebase, **iOS + Android**. Scans on-device, or connects to a Bing Engine for full-power scans. | iPhone & Android phones/tablets |

<p align="center"><em>The web dashboard and the mobile app are the same product in two skins.</em></p>

---

## What it does (vs. Fing)

| Capability | Bing | Notes |
|---|---|---|
| Discover devices on the LAN | ✅ | IP, MAC, vendor, hostname, type, latency |
| Identify device type & maker | ✅ | OUI vendor DB + port/hostname heuristics |
| Per-device port scan | ✅ | TCP connect scan, service names, banners |
| Security assessment | ✅ | Flags risky exposed services, 0-100 score |
| Ping / latency | ✅ | ICMP when privileged, TCP otherwise |
| Traceroute | ✅ | Raw-socket path trace (engine, root) |
| DNS lookup (A/AAAA/MX/TXT/…) | ✅ | Hand-rolled resolver — no `dig` needed |
| Reverse DNS / mDNS names | ✅ | |
| Wake-on-LAN | ✅ | Magic-packet broadcast |
| Internet speed test | ✅ | Latency, download, upload |
| Network & ISP info | ✅ | Local + public IP, gateway, subnet, ISP |
| iOS + Android app | ✅ | Single Expo codebase |
| Web dashboard | ✅ | Live streaming scan (SSE) |

---

## Quick start

### The engine (CLI + dashboard)

No installation, no dependencies — just Python 3.8+:

```bash
cd engine

# Discover everything on your network
python -m bing_engine scan

# Launch the web dashboard (Fing-style UI) at http://localhost:8787
python -m bing_engine web --open

# The toolbox
python -m bing_engine ports 192.168.1.1
python -m bing_engine ping 1.1.1.1
python -m bing_engine dns github.com -t A,AAAA,MX
python -m bing_engine speed
```

Or install it so `bing` is on your PATH:

```bash
cd engine && pip install -e .
bing scan
```

### The mobile app (iOS + Android)

```bash
cd mobile
npm install
npx expo start        # scan the QR code with Expo Go for a quick look
```

For the **full on-device scanner** (native TCP sockets) build a dev client:

```bash
npx expo run:ios       # or: npx expo run:android
```

See [`mobile/README.md`](mobile/README.md) for the on-device vs. Engine-mode
details and the iOS local-network permission notes.

---

## How the two parts fit together

```
                         ┌─────────────────────────────┐
                         │        Bing Mobile          │
                         │   iOS + Android (Expo/RN)   │
                         │  • on-device TCP/mDNS scan  │
                         │  • Fing-style device list   │
                         └───────┬─────────────┬───────┘
                    on-device    │             │  "Engine mode"
                    (sandboxed)  │             │  HTTP/JSON REST
                                 ▼             ▼
                    ┌────────────────┐   ┌─────────────────────────┐
                    │  Your Wi-Fi    │   │      Bing Engine        │
                    │  LAN devices   │◄──│  (desktop / Pi / NAS)   │
                    └────────────────┘   │  • ARP-based discovery  │
                                         │  • MAC vendor, ports    │
                                         │  • REST API + dashboard │
                                         └─────────────────────────┘
```

A phone's OS hides the ARP table from apps, so a from-scratch mobile scanner can
see IPs, open ports and Bonjour names but **not** MAC addresses or vendors.
Point the app at a Bing Engine on the same network and every scan gets the full
ARP-based detail — MACs, vendors, traceroute and Wake-on-LAN included. Both
modes speak the exact same JSON, so the UI never changes.

---

## Design principles

- **No required dependencies in the engine.** Everything is Python standard
  library, so `python -m bing_engine` runs on any box — including a stock
  Raspberry Pi — with nothing to install.
- **Degrade, don't fail.** Missing `ping`/`traceroute` binaries, no root, no
  ARP access — each feature falls back to a privilege-free method and says so.
- **One contract.** The engine's REST JSON, the CLI's `--json`, and the mobile
  types are all the same shapes (see [`mobile/src/types.ts`](mobile/src/types.ts)).

## Ethics & scope

Bing is for **networks you own or are authorised to test**. Port scanning and
device discovery on networks you don't control may be against the rules of your
provider or the law where you live. Use it on your own LAN.

## License

MIT — see the repository [`LICENSE`](../../LICENSE).
