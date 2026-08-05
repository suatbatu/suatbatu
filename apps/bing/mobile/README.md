# Bing Mobile

A **Fing-like network scanner for iOS & Android**, built with **Expo / React
Native** — one TypeScript codebase, both platforms.

<img src="./assets/icon.png" width="96" align="right" alt="Bing icon" />

- 📡 **Discover devices** on your Wi-Fi — with a live, streaming device list
- 🔎 **Per-device deep scan** — open ports + a security grade
- 🧰 **Toolbox** — ping, port scan, DNS, traceroute, speed test, Wake-on-LAN, MAC vendor
- 🌐 **Network tab** — local + public IP, gateway, subnet, ISP
- ⚙️ **Two scanning modes** — on-device, or via a Bing Engine for full detail

## Run it

```bash
npm install

# Quick look in Expo Go (limited scanner — see "Scanning modes"):
npx expo start

# Full native scanner (recommended) — builds a dev client:
npx expo run:ios       # needs Xcode
npx expo run:android   # needs Android Studio / SDK
```

Requires Node 18+ and the Expo toolchain. This is a standard Expo (SDK 51)
project — `app.json` + `expo-router` file-based routing under `app/`.

## Scanning modes

A phone's OS sandboxes low-level networking, so there are two ways to scan:

### 1. On-device (standalone)

The app sweeps your subnet itself:

- **Native TCP mode** — with the `react-native-tcp-socket` native module (any
  dev/release build), it makes real `connect()` probes: it can tell *open*,
  *refused* (host up, port closed) and *timeout* apart, exactly like the desktop
  engine. This is the recommended way to run.
- **Fetch-fallback mode** — in plain **Expo Go** the native module isn't
  present, so discovery falls back to `fetch()` probes and can only find
  HTTP(S)-reachable devices. The Settings tab shows which mode is active.

DNS uses **DNS-over-HTTPS** and the speed test uses Cloudflare, so those tools
work on-device with no server. Because the OS hides the ARP table, on-device
scans show IP, open ports, type and Bonjour names — but **MAC/vendor need
Engine mode**, and traceroute / Wake-on-LAN require raw sockets / UDP broadcast
(also Engine mode).

### 2. Engine mode (full power)

Run the [Bing Engine](../engine/) on a computer, Raspberry Pi or NAS on the same
network:

```bash
cd ../engine && python -m bing_engine web --host 0.0.0.0 --port 8787
```

Then in the app: **Settings → Bing Engine →** enter `192.168.1.10:8787` → **Test
connection**. Now every scan is a full ARP-based scan — MAC addresses, vendors,
hostnames, traceroute and Wake-on-LAN — streamed to your phone. Same UI, more
data.

`Settings → Scan mode` lets you force **On-device**, force **Engine**, or use
**Auto** (engine when reachable, else on-device).

## iOS local-network permission

iOS requires explicit consent to talk to the local network. `app.json` already
declares:

- `NSLocalNetworkUsageDescription` — the prompt the user sees on first scan
- `NSBonjourServices` — the mDNS service types the app browses (`_http._tcp`,
  `_googlecast._tcp`, `_airplay._tcp`, `_printer._tcp`, …)

The permission dialog appears the first time you tap **Scan network**. Android
declares `ACCESS_WIFI_STATE` / `CHANGE_WIFI_MULTICAST_STATE` and (for Wi-Fi
details) `ACCESS_FINE_LOCATION`.

## Project layout

```
app/                       expo-router screens
  _layout.tsx              root stack + settings provider
  (tabs)/
    _layout.tsx            bottom tab navigator
    index.tsx              Devices — the discovery screen
    tools.tsx              the network toolbox
    network.tsx            network & ISP info
    settings.tsx           scan mode + engine config
  device/[ip].tsx          per-device detail + deep scan
src/
  scanner/                 on-device engine
    discovery.ts           subnet sweep orchestrator
    tcp.ts                 TCP probe (native socket / fetch fallback)
    subnet.ts, ports.ts    IP math, service DB
    oui.ts, deviceType.ts  vendor lookup, classification
    security.ts, netinfo.ts
  api/engineClient.ts      Bing Engine REST client (Engine mode)
  tools/onDevice.ts        on-device ping / portscan / DoH / speedtest
  store/settings.tsx       settings context
  components/, hooks/, theme.ts, types.ts
assets/                    app icon, adaptive icon, splash, favicon
```

The data types in [`src/types.ts`](src/types.ts) match the engine's REST JSON
exactly, so a device from an on-device scan and one from the engine are
interchangeable throughout the UI.

## Notes

- Settings are held in memory for this version — wire up `AsyncStorage` or
  `expo-secure-store` in [`src/store/settings.tsx`](src/store/settings.tsx) to
  persist the engine address across launches.
- Only scan networks you own or are authorised to test.
