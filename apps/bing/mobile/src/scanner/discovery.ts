/** On-device network discovery.
 *
 *  Sweeps the local subnet with TCP probes (see tcp.ts), classifies each live
 *  host from its open ports, and streams results as they're found. MAC/vendor
 *  aren't available to a sandboxed mobile app (the OS hides the ARP table), so
 *  those stay null here — use Engine mode for full ARP-based detail. */

import type { Device, NetworkInfo } from "../types";
import { classify } from "./deviceType";
import { localNetwork } from "./netinfo";
import { CLASSIFY_PORTS, LIVENESS_PORTS } from "./ports";
import { hostsInCidr } from "./subnet";
import { hasNativeTcp, probeMany, probePort } from "./tcp";

export interface ScanProgress {
  scanned: number;
  total: number;
  found: number;
  phase: "sweep" | "classify" | "done";
}

export interface ScanOptions {
  cidr?: string;
  net?: NetworkInfo;
  timeout?: number;
  concurrency?: number;
  onDevice?: (d: Device) => void;
  onProgress?: (p: ScanProgress) => void;
  shouldStop?: () => boolean;
}

const LIVENESS_SUBSET = LIVENESS_PORTS.slice(0, 6);

export async function scanNetwork(opts: ScanOptions = {}): Promise<Device[]> {
  const net = opts.net ?? (await localNetwork());
  const cidr = opts.cidr ?? net.cidr;
  if (!cidr) throw new Error("Could not determine your subnet. Connect to Wi-Fi or set it in Settings.");

  const hosts = hostsInCidr(cidr);
  const timeout = opts.timeout ?? 700;
  const concurrency = opts.concurrency ?? 16;
  const gateway = net.gateway;
  const self = net.primary_ipv4;

  const devices = new Map<string, Device>();
  let scanned = 0;

  const emitProgress = (phase: ScanProgress["phase"]) =>
    opts.onProgress?.({ scanned, total: hosts.length, found: devices.size, phase });

  const makeDevice = (ip: string, rtt: number | null): Device => {
    const base: Device = {
      ip, mac: null, vendor: null, hostname: null, open_ports: [],
      rtt_ms: rtt != null ? Math.round(rtt * 10) / 10 : null,
      device_type: "Unknown", icon: "device",
      is_gateway: ip === gateway, is_self: ip === self, source: "device",
    };
    return base;
  };

  await probeMany(
    hosts,
    async (ip) => {
      if (opts.shouldStop?.()) return;
      // Parallel liveness probe across a few ports; alive if any answers.
      const probes = await Promise.all(LIVENESS_SUBSET.map((p) => probePort(ip, p, timeout)));
      scanned++;
      const alive = probes.find((r) => r.alive);
      if (alive) {
        const rtts = probes.filter((r) => r.rttMs != null).map((r) => r.rttMs!);
        const dev = makeDevice(ip, rtts.length ? Math.min(...rtts) : null);
        // Record ports already found open during liveness.
        dev.open_ports = LIVENESS_SUBSET.filter((_, i) => probes[i].open);
        devices.set(ip, dev);
        const guessed = classify(dev);
        dev.device_type = guessed.device_type;
        dev.icon = guessed.icon;
        opts.onDevice?.(dev);
      }
      if (scanned % 8 === 0) emitProgress("sweep");
    },
    concurrency,
  );
  emitProgress("sweep");

  // Always surface the gateway & this device, even if silent to probes.
  for (const special of [gateway, self]) {
    if (special && !devices.has(special) && hosts.includes(special)) {
      const dev = makeDevice(special, null);
      const guessed = classify(dev);
      dev.device_type = guessed.device_type;
      dev.icon = guessed.icon;
      devices.set(special, dev);
      opts.onDevice?.(dev);
    }
  }

  // Classify pass: probe the fuller port set on each live host to refine type.
  const live = Array.from(devices.values());
  emitProgress("classify");
  await probeMany(
    live,
    async (dev) => {
      if (opts.shouldStop?.()) return;
      const found: number[] = [...dev.open_ports];
      await probeMany(
        CLASSIFY_PORTS.filter((p) => !found.includes(p)),
        async (port) => {
          const r = await probePort(dev.ip, port, timeout);
          if (r.open) found.push(port);
        },
        10,
      );
      dev.open_ports = Array.from(new Set(found)).sort((a, b) => a - b);
      const guessed = classify(dev);
      dev.device_type = guessed.device_type;
      dev.icon = guessed.icon;
      opts.onDevice?.({ ...dev });
    },
    8,
  );

  emitProgress("done");
  return live.sort((a, b) => ipNum(a.ip) - ipNum(b.ip));
}

export const scannerMode = () => (hasNativeTcp() ? "native-tcp" : "fetch-fallback");

const ipNum = (ip: string) =>
  ip.split(".").reduce((acc, o) => (acc << 8) + (parseInt(o, 10) || 0), 0) >>> 0;
