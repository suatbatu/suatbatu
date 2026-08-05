/** On-device security assessment — mirrors the engine's security.py. */
import type { SecurityFinding, SecurityReport } from "../types";

const RISKY: Record<number, [SecurityFinding["severity"], string]> = {
  23: ["high", "Telnet — unencrypted remote login; disable it"],
  21: ["medium", "FTP — credentials sent in plaintext; prefer SFTP/FTPS"],
  2323: ["high", "Telnet (alt) — common IoT botnet target"],
  3389: ["medium", "RDP exposed — a top ransomware entry point if reachable"],
  5900: ["medium", "VNC — often unauthenticated or weakly protected"],
  445: ["medium", "SMB — keep patched; never expose to the internet"],
  139: ["low", "NetBIOS — legacy Windows sharing, leaks host info"],
  135: ["low", "MS-RPC exposed"],
  1900: ["low", "UPnP/SSDP — can auto-open router ports; disable if unused"],
  3306: ["high", "MySQL reachable — must not be network-exposed unauthenticated"],
  5432: ["high", "PostgreSQL reachable — should not be LAN/WAN exposed"],
  6379: ["high", "Redis — unauthenticated by default; a frequent breach vector"],
  27017: ["high", "MongoDB — historically exposed without auth"],
  9200: ["high", "Elasticsearch — often left open with no auth"],
  11211: ["high", "Memcached — no auth; abused for DDoS amplification"],
  5555: ["high", "Android ADB open — allows remote device control"],
  2375: ["high", "Docker API (no TLS) — full host takeover if reachable"],
  9100: ["low", "Raw print port open — can be abused to print spam"],
  161: ["low", "SNMP — default 'public' community leaks device details"],
  8080: ["low", "HTTP admin panel — ensure it requires a strong password"],
  10000: ["medium", "Webmin — remote admin; keep patched and firewalled"],
};

const WEIGHT: Record<SecurityFinding["severity"], number> = { high: 30, medium: 15, low: 6 };

function grade(score: number): string {
  if (score >= 90) return "A";
  if (score >= 80) return "B";
  if (score >= 65) return "C";
  if (score >= 50) return "D";
  return "F";
}

export function assess(ip: string, openPorts: number[]): SecurityReport {
  const findings: SecurityFinding[] = [];
  let penalty = 0;
  const ports = new Set(openPorts);
  for (const port of [...ports].sort((a, b) => a - b)) {
    const risk = RISKY[port];
    if (risk) {
      findings.push({ port, severity: risk[0], message: risk[1] });
      penalty += WEIGHT[risk[0]];
    }
  }
  const alternatives: string[] = [];
  if (ports.has(80) && !ports.has(443)) alternatives.push("Serve over HTTPS (443) instead of plain HTTP (80)");
  if (ports.has(21)) alternatives.push("Replace FTP (21) with SFTP (22) or FTPS (990)");
  if (ports.has(23)) alternatives.push("Replace Telnet (23) with SSH (22)");

  const score = Math.max(0, 100 - penalty);
  return { ip, score, grade: grade(score), findings, encrypted_alternatives: alternatives };
}
