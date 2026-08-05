/** Client for the Bing Engine REST API.
 *
 *  When the user points the app at a running engine (desktop / Raspberry Pi /
 *  router), the phone gets full ARP-based scans — MAC addresses, vendors,
 *  hostnames, traceroute, Wake-on-LAN — that a sandboxed mobile app can't do
 *  itself. Same JSON shapes as the on-device scanner, so the UI is identical. */

import type { Device, DnsRecord, NetworkInfo, PingResult, PortResult, SecurityReport } from "../types";

export function normalizeBaseUrl(input: string): string {
  let url = input.trim();
  if (!url) return "";
  if (!/^https?:\/\//i.test(url)) url = `http://${url}`;
  return url.replace(/\/+$/, "");
}

export class EngineClient {
  constructor(private baseUrl: string) {
    this.baseUrl = normalizeBaseUrl(baseUrl);
  }

  private async get<T>(path: string, timeoutMs = 20000): Promise<T> {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), timeoutMs);
    try {
      const res = await fetch(`${this.baseUrl}${path}`, { signal: ctrl.signal });
      const data = await res.json();
      if (!res.ok) throw new Error(data?.error || `HTTP ${res.status}`);
      return data as T;
    } finally {
      clearTimeout(timer);
    }
  }

  /** Quick reachability + version probe. */
  async ping(timeoutMs = 4000): Promise<boolean> {
    try {
      await this.get<NetworkInfo>("/api/net?public=0", timeoutMs);
      return true;
    } catch {
      return false;
    }
  }

  net(includePublic = true): Promise<NetworkInfo> {
    return this.get<NetworkInfo>(`/api/net?public=${includePublic ? 1 : 0}`);
  }

  async scan(cidr?: string): Promise<Device[]> {
    const q = cidr ? `?cidr=${encodeURIComponent(cidr)}` : "";
    const data = await this.get<{ count: number; devices: Device[] }>(`/api/scan${q}`, 90000);
    return data.devices.map((d) => ({ ...d, source: "engine" as const }));
  }

  device(ip: string): Promise<{ ip: string; hostname: string | null; open_ports: PortResult[]; security: SecurityReport }> {
    return this.get(`/api/device?ip=${encodeURIComponent(ip)}&ports=top100`, 60000);
  }

  ports(host: string, spec = "common"): Promise<{ host: string; open: PortResult[]; security: SecurityReport }> {
    return this.get(`/api/ports?host=${encodeURIComponent(host)}&ports=${encodeURIComponent(spec)}`, 60000);
  }

  pingHost(host: string, count = 5): Promise<PingResult> {
    return this.get<PingResult>(`/api/ping?host=${encodeURIComponent(host)}&count=${count}`, 30000);
  }

  trace(host: string): Promise<any> {
    return this.get(`/api/trace?host=${encodeURIComponent(host)}`, 60000);
  }

  dns(name: string, type = "A"): Promise<{ name: string; records: DnsRecord[]; errors: string[] }> {
    return this.get(`/api/dns?name=${encodeURIComponent(name)}&type=${encodeURIComponent(type)}`);
  }

  speed(quick = true): Promise<any> {
    return this.get(`/api/speed?quick=${quick ? 1 : 0}`, 45000);
  }

  wol(mac: string): Promise<{ mac: string; sent: boolean }> {
    return this.get(`/api/wol?mac=${encodeURIComponent(mac)}`);
  }

  vendor(mac: string): Promise<{ mac: string | null; vendor: string | null }> {
    return this.get(`/api/vendor?mac=${encodeURIComponent(mac)}`);
  }
}
