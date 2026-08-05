"""Lightweight security assessment for discovered devices.

Not a vulnerability scanner — it flags *exposed* services that are commonly
risky on a home/office LAN (plaintext admin protocols, unauthenticated
databases, remote-access ports) and produces a simple 0-100 score, mirroring
Fing's "security checks" at a high level.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List

# port -> (severity, short reason)
_RISKY: Dict[int, tuple] = {
    23: ("high", "Telnet — unencrypted remote login; disable it"),
    21: ("medium", "FTP — credentials sent in plaintext; prefer SFTP/FTPS"),
    2323: ("high", "Telnet (alt) — common IoT botnet target"),
    3389: ("medium", "RDP exposed — a top ransomware entry point if reachable"),
    5900: ("medium", "VNC — often unauthenticated or weakly protected"),
    445: ("medium", "SMB — keep patched; never expose to the internet"),
    139: ("low", "NetBIOS — legacy Windows sharing, leaks host info"),
    135: ("low", "MS-RPC exposed"),
    1900: ("low", "UPnP/SSDP — can auto-open router ports; disable if unused"),
    3306: ("high", "MySQL reachable — must not be network-exposed unauthenticated"),
    5432: ("high", "PostgreSQL reachable — should not be LAN/WAN exposed"),
    6379: ("high", "Redis — unauthenticated by default; a frequent breach vector"),
    27017: ("high", "MongoDB — historically exposed without auth"),
    9200: ("high", "Elasticsearch — often left open with no auth"),
    11211: ("high", "Memcached — no auth; abused for DDoS amplification"),
    5555: ("high", "Android ADB open — allows remote device control"),
    2375: ("high", "Docker API (no TLS) — full host takeover if reachable"),
    9100: ("low", "Raw print port open — can be abused to exfil/print spam"),
    161: ("low", "SNMP — default 'public' community leaks device details"),
    8080: ("low", "HTTP admin panel — ensure it requires a strong password"),
    10000: ("medium", "Webmin — remote admin; keep patched and firewalled"),
}

_SEVERITY_WEIGHT = {"high": 30, "medium": 15, "low": 6}


@dataclass
class Finding:
    port: int
    severity: str
    message: str

    def to_dict(self) -> Dict:
        return self.__dict__.copy()


@dataclass
class SecurityReport:
    ip: str
    score: int
    grade: str
    findings: List[Finding] = field(default_factory=list)
    encrypted_alternatives: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict:
        return {
            "ip": self.ip,
            "score": self.score,
            "grade": self.grade,
            "findings": [f.to_dict() for f in self.findings],
            "encrypted_alternatives": self.encrypted_alternatives,
        }


def _grade(score: int) -> str:
    if score >= 90:
        return "A"
    if score >= 80:
        return "B"
    if score >= 65:
        return "C"
    if score >= 50:
        return "D"
    return "F"


def assess(ip: str, open_ports: List[int]) -> SecurityReport:
    """Score a device from its open ports."""
    findings: List[Finding] = []
    penalty = 0
    ports = set(open_ports)
    for port in sorted(ports):
        if port in _RISKY:
            severity, reason = _RISKY[port]
            findings.append(Finding(port=port, severity=severity, message=reason))
            penalty += _SEVERITY_WEIGHT[severity]

    # Note where a plaintext service could be replaced by an encrypted one.
    alternatives = []
    if 80 in ports and 443 not in ports:
        alternatives.append("Serve the web UI over HTTPS (443) instead of plain HTTP (80)")
    if 21 in ports:
        alternatives.append("Replace FTP (21) with SFTP (22) or FTPS (990)")
    if 23 in ports:
        alternatives.append("Replace Telnet (23) with SSH (22)")

    score = max(0, 100 - penalty)
    return SecurityReport(ip=ip, score=score, grade=_grade(score),
                          findings=findings, encrypted_alternatives=alternatives)
