/** On-device implementations of the network tools, for when no engine is set.
 *
 *  Pure JS / native-TCP — no engine required:
 *    • ping      — TCP-connect latency (like the engine's TCP ping)
 *    • portScan  — TCP-connect scan
 *    • dns       — DNS-over-HTTPS (Cloudflare), so lookups work with no resolver
 *    • speedtest — Cloudflare down/up transfers over fetch
 *
 *  Traceroute and Wake-on-LAN need raw sockets / UDP broadcast and are only
 *  offered in Engine mode; the UI says so. */

import type { DnsRecord, PingResult, PortResult } from "../types";
import { serviceName } from "../scanner/ports";
import { probeMany, probePort } from "../scanner/tcp";

/** TCP ping: measure connect latency to a port over several attempts. */
export async function tcpPing(host: string, port = 443, count = 5, timeout = 1500): Promise<PingResult> {
  const rtts: number[] = [];
  let received = 0;
  for (let i = 0; i < count; i++) {
    const r = await probePort(host, port, timeout);
    if (r.alive && r.rttMs != null) {
      received++;
      rtts.push(r.rttMs);
    }
    if (i < count - 1) await sleep(250);
  }
  const summary = rtts.length
    ? {
        min: round(Math.min(...rtts)),
        max: round(Math.max(...rtts)),
        avg: round(rtts.reduce((a, b) => a + b, 0) / rtts.length),
        jitter: round(stddev(rtts)),
      }
    : { min: null, max: null, avg: null, jitter: null };
  return {
    host,
    address: host,
    method: `tcp:${port}`,
    transmitted: count,
    received,
    loss_pct: round((100 * (count - received)) / count),
    summary,
  };
}

/** TCP-connect port scan. `ports` is an explicit list. */
export async function portScan(host: string, ports: number[], timeout = 1200): Promise<PortResult[]> {
  const open: PortResult[] = [];
  await probeMany(
    ports,
    async (port) => {
      const r = await probePort(host, port, timeout);
      if (r.open) {
        open.push({ port, open: true, service: serviceName(port), banner: null, latency_ms: r.rttMs });
      }
    },
    24,
  );
  return open.sort((a, b) => a.port - b.port);
}

/** DNS lookup via DNS-over-HTTPS (RFC 8484 JSON form). */
export async function dohLookup(name: string, types: string[]): Promise<DnsRecord[]> {
  const TYPE_NUM: Record<string, number> = {
    A: 1, NS: 2, CNAME: 5, SOA: 6, PTR: 12, MX: 15, TXT: 16, AAAA: 28, SRV: 33, CAA: 257,
  };
  const NUM_TYPE: Record<number, string> = Object.fromEntries(
    Object.entries(TYPE_NUM).map(([k, v]) => [v, k]),
  );
  const out: DnsRecord[] = [];
  for (const t of types) {
    const type = t.trim().toUpperCase();
    try {
      const res = await fetch(
        `https://cloudflare-dns.com/dns-query?name=${encodeURIComponent(name)}&type=${type}`,
        { headers: { Accept: "application/dns-json" } },
      );
      const data = await res.json();
      for (const ans of data.Answer || []) {
        out.push({
          name: ans.name?.replace(/\.$/, "") || name,
          type: NUM_TYPE[ans.type] || String(ans.type),
          ttl: ans.TTL ?? 0,
          value: ans.data,
        });
      }
    } catch {
      /* skip this type */
    }
  }
  return out;
}

export interface SpeedResult {
  latency_ms: number | null;
  download_mbps: number | null;
  upload_mbps: number | null;
  server: string | null;
  isp: string | null;
  error?: string;
}

/** Speed test against Cloudflare (down + up over fetch). */
export async function speedTest(quick = true): Promise<SpeedResult> {
  const result: SpeedResult = { latency_ms: null, download_mbps: null, upload_mbps: null, server: null, isp: null };
  try {
    const meta = await (await fetch("https://speed.cloudflare.com/meta")).json();
    result.server = meta.colo ? `Cloudflare ${meta.colo}` : "Cloudflare";
    result.isp = meta.asOrganization ?? null;
  } catch {
    /* ignore */
  }
  // latency
  try {
    const samples: number[] = [];
    for (let i = 0; i < 4; i++) {
      const s = Date.now();
      await fetch("https://speed.cloudflare.com/__down?bytes=0");
      samples.push(Date.now() - s);
    }
    result.latency_ms = round(Math.min(...samples));
  } catch {
    /* ignore */
  }
  // download
  try {
    const bytes = quick ? 10_000_000 : 25_000_000;
    const start = Date.now();
    const res = await fetch(`https://speed.cloudflare.com/__down?bytes=${bytes}`);
    const blob = await res.blob();
    const secs = (Date.now() - start) / 1000;
    if (secs > 0) result.download_mbps = round((blob.size * 8) / secs / 1e6, 2);
  } catch {
    /* ignore */
  }
  // upload
  try {
    const bytes = quick ? 2_000_000 : 6_000_000;
    const payload = new Uint8Array(bytes);
    const start = Date.now();
    await fetch("https://speed.cloudflare.com/__up", { method: "POST", body: payload });
    const secs = (Date.now() - start) / 1000;
    if (secs > 0) result.upload_mbps = round((bytes * 8) / secs / 1e6, 2);
  } catch {
    /* ignore */
  }
  if (result.download_mbps == null && result.latency_ms == null) {
    result.error = "Speed test unreachable — check your connection.";
  }
  return result;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const round = (n: number, d = 1) => Math.round(n * 10 ** d) / 10 ** d;
const stddev = (xs: number[]) => {
  const m = xs.reduce((a, b) => a + b, 0) / xs.length;
  return Math.sqrt(xs.reduce((a, b) => a + (b - m) ** 2, 0) / xs.length);
};
