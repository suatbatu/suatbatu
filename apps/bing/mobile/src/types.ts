/** Shared data shapes. These match the Bing Engine REST API JSON exactly, so a
 *  device from an on-device scan and one from the engine are interchangeable. */

export interface Device {
  ip: string;
  mac: string | null;
  vendor: string | null;
  hostname: string | null;
  open_ports: number[];
  rtt_ms: number | null;
  device_type: string;
  icon: string;
  is_gateway: boolean;
  is_self: boolean;
  /** Where this record came from — useful for UI hints. */
  source?: "device" | "engine";
}

export interface NetworkInfo {
  hostname: string | null;
  primary_ipv4: string | null;
  gateway: string | null;
  netmask: string | null;
  cidr: string | null;
  interfaces?: { name: string; ipv4: string | null; netmask: string | null; mac: string | null }[];
  dns_servers?: string[];
  public_ip?: string | null;
  isp?: string | null;
  location?: string | null;
}

export interface PortResult {
  port: number;
  open: boolean;
  service: string | null;
  banner: string | null;
  latency_ms: number | null;
}

export interface SecurityFinding {
  port: number;
  severity: "high" | "medium" | "low";
  message: string;
}

export interface SecurityReport {
  ip: string;
  score: number;
  grade: string;
  findings: SecurityFinding[];
  encrypted_alternatives: string[];
}

export interface PingResult {
  host: string;
  address: string | null;
  method: string;
  transmitted: number;
  received: number;
  loss_pct: number;
  summary: { min: number | null; avg: number | null; max: number | null; jitter: number | null };
}

export interface DnsRecord {
  name: string;
  type: string;
  ttl: number;
  value: string;
}
