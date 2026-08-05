"""TCP port scanning with service identification and light banner grabbing.

Uses ordinary ``connect()`` scans, so it needs no special privileges and works
identically on every OS.  Banner grabbing reads whatever a service volunteers
on connect (and nudges HTTP ports with a HEAD request).
"""

from __future__ import annotations

import socket
import ssl
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

from .util import Stopwatch, parallel_map

# Well-known ports → service name.  Covers the services a network scanner cares
# about; not the full IANA list.
SERVICES: Dict[int, str] = {
    20: "ftp-data", 21: "ftp", 22: "ssh", 23: "telnet", 25: "smtp", 53: "dns",
    67: "dhcp", 68: "dhcp", 69: "tftp", 80: "http", 88: "kerberos", 110: "pop3",
    111: "rpcbind", 119: "nntp", 123: "ntp", 135: "msrpc", 137: "netbios-ns",
    138: "netbios-dgm", 139: "netbios-ssn", 143: "imap", 161: "snmp", 162: "snmp-trap",
    179: "bgp", 389: "ldap", 443: "https", 445: "smb", 465: "smtps", 500: "isakmp",
    514: "syslog", 515: "printer", 520: "rip", 523: "ibm-db2", 548: "afp",
    554: "rtsp", 587: "smtp-submission", 631: "ipp", 636: "ldaps", 873: "rsync",
    902: "vmware", 989: "ftps-data", 990: "ftps", 993: "imaps", 995: "pop3s",
    1080: "socks", 1194: "openvpn", 1433: "mssql", 1521: "oracle", 1723: "pptp",
    1883: "mqtt", 1900: "upnp/ssdp", 2049: "nfs", 2082: "cpanel", 2083: "cpanel-ssl",
    2181: "zookeeper", 2375: "docker", 2376: "docker-tls", 3000: "dev-http",
    3128: "http-proxy", 3260: "iscsi", 3306: "mysql", 3389: "rdp", 3478: "stun",
    3689: "daap/airplay", 4000: "dev", 4444: "metasploit", 4500: "ipsec-nat",
    5000: "upnp/uPnP", 5001: "synology", 5060: "sip", 5061: "sip-tls",
    5222: "xmpp", 5353: "mdns", 5432: "postgresql", 5555: "adb", 5601: "kibana",
    5672: "amqp", 5683: "coap", 5900: "vnc", 5984: "couchdb", 6000: "x11",
    6379: "redis", 6443: "kubernetes", 6667: "irc", 7000: "airplay", 7070: "airplay",
    8000: "http-alt", 8008: "http/chromecast", 8009: "chromecast", 8080: "http-proxy",
    8081: "http-alt", 8083: "http-alt", 8086: "influxdb", 8096: "jellyfin/emby",
    8123: "home-assistant", 8181: "http-alt", 8443: "https-alt", 8883: "mqtt-tls",
    8888: "http-alt", 9000: "php-fpm/portainer", 9090: "prometheus", 9100: "jetdirect",
    9200: "elasticsearch", 9443: "https-alt", 10000: "webmin", 11211: "memcached",
    27017: "mongodb", 32400: "plex", 49152: "upnp", 51820: "wireguard",
    62078: "apple-sync",
}

# Sensible defaults for a "quick" device scan — the ports actually worth probing
# on a typical LAN host.
COMMON_PORTS: List[int] = [
    21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443, 445, 465, 515, 548,
    554, 587, 631, 993, 995, 1883, 1900, 2049, 3000, 3306, 3389, 5000, 5060,
    5353, 5432, 5900, 6379, 7000, 8000, 8008, 8009, 8080, 8083, 8096, 8123,
    8443, 8888, 9000, 9100, 9200, 32400, 62078,
]

# The classic nmap "top ~100" set for a broader default scan.
TOP_100_PORTS: List[int] = sorted(set(COMMON_PORTS) | {
    7, 9, 13, 26, 37, 79, 81, 88, 106, 113, 119, 144, 179, 199, 389, 427, 543,
    544, 636, 646, 800, 888, 1025, 1026, 1027, 1028, 1029, 1110, 1433, 1720,
    1755, 1900, 2000, 2001, 2121, 2717, 3128, 3986, 4899, 5009, 5051, 5101,
    5190, 5357, 5631, 5666, 5800, 6001, 6646, 7070, 8031, 8081, 8443, 8888,
    9040, 9999, 10000, 32768, 49152, 49153, 49154, 49155, 49156, 49157,
})


@dataclass
class PortResult:
    port: int
    open: bool
    service: Optional[str] = None
    banner: Optional[str] = None
    latency_ms: Optional[float] = None

    def to_dict(self) -> Dict:
        return {
            "port": self.port,
            "open": self.open,
            "service": self.service,
            "banner": self.banner,
            "latency_ms": self.latency_ms,
        }


def service_name(port: int) -> Optional[str]:
    return SERVICES.get(port)


def _grab_banner(sock: "socket.socket", host: str, port: int) -> Optional[str]:
    """Read a short banner; prompt HTTP-ish and TLS ports appropriately."""
    try:
        sock.settimeout(1.5)
        if port in (443, 8443, 9443, 993, 995, 465, 990, 5061, 6443, 8883):
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            with ctx.wrap_socket(sock, server_hostname=host) as tls:
                cert = tls.getpeercert()
                proto = tls.version()
                subject = ""
                if cert:
                    subject = dict(x[0] for x in cert.get("subject", []) if x).get(
                        "commonName", "")
                return f"TLS {proto}" + (f" · CN={subject}" if subject else "")
        if port in (80, 8080, 8000, 8008, 8081, 8888, 3000, 5000, 8123):
            sock.sendall(b"HEAD / HTTP/1.0\r\nHost: %b\r\n\r\n" % host.encode())
        data = sock.recv(256)
        text = data.decode("latin-1", "replace").strip()
        # Compress the HTTP response to the interesting header lines.
        if text.startswith("HTTP/"):
            lines = [l for l in text.splitlines()
                     if l.split(":", 1)[0].lower() in ("server", "location", "www-authenticate")]
            head = text.splitlines()[0]
            return " · ".join([head] + lines) if lines else head
        return text[:120] or None
    except (socket.timeout, ssl.SSLError, OSError):
        return None


def scan_port(host: str, port: int, timeout: float = 1.0, banner: bool = True) -> PortResult:
    """Probe a single TCP port on *host*."""
    sw = Stopwatch()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        rc = sock.connect_ex((host, port))
        latency = sw.elapsed() * 1000
        if rc == 0:
            grabbed = _grab_banner(sock, host, port) if banner else None
            return PortResult(port=port, open=True, service=service_name(port),
                             banner=grabbed, latency_ms=round(latency, 1))
        return PortResult(port=port, open=False, service=service_name(port))
    except (socket.gaierror, OSError):
        return PortResult(port=port, open=False, service=service_name(port))
    finally:
        sock.close()


def scan(
    host: str,
    ports: Optional[List[int]] = None,
    timeout: float = 1.0,
    workers: int = 200,
    banner: bool = True,
    on_open: Optional[Callable[[PortResult], None]] = None,
) -> List[PortResult]:
    """Scan a list of ports on *host*, returning only the open ones (sorted).

    ``on_open`` streams each open port as it is discovered.
    """
    ports = ports or COMMON_PORTS

    def probe(p: int) -> PortResult:
        return scan_port(host, p, timeout=timeout, banner=banner)

    def _stream(res: PortResult) -> None:
        if res.open and on_open is not None:
            on_open(res)

    results = parallel_map(probe, ports, workers=workers, on_result=_stream)
    return sorted((r for r in results if r.open), key=lambda r: r.port)


def parse_port_spec(spec: str) -> List[int]:
    """Parse ``22,80,443`` or ``1-1024`` or ``top100`` / ``common`` / ``all``."""
    spec = spec.strip().lower()
    if spec in ("common", "default"):
        return COMMON_PORTS
    if spec in ("top", "top100"):
        return TOP_100_PORTS
    if spec == "all":
        return list(range(1, 65536))
    ports: List[int] = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            lo, hi = chunk.split("-", 1)
            ports.extend(range(int(lo), int(hi) + 1))
        else:
            ports.append(int(chunk))
    return sorted(set(p for p in ports if 0 < p < 65536))
