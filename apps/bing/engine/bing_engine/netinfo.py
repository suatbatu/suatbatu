"""Discover the host's own network context.

Everything degrades gracefully: on Linux we read ``/proc`` and use ioctls; on
other platforms (or when those are unavailable) we fall back to the classic
"connect a UDP socket and read the source address" trick and a /24 assumption.
"""

from __future__ import annotations

import ipaddress
import json
import socket
import struct
import urllib.request
from dataclasses import dataclass, field
from typing import Dict, List, Optional

try:  # Linux-only, optional
    import fcntl
except ImportError:  # pragma: no cover - non-Linux
    fcntl = None  # type: ignore

_SIOCGIFADDR = 0x8915
_SIOCGIFNETMASK = 0x891B
_SIOCGIFHWADDR = 0x8927


@dataclass
class Interface:
    name: str
    ipv4: Optional[str] = None
    netmask: Optional[str] = None
    mac: Optional[str] = None
    cidr: Optional[str] = None


@dataclass
class NetworkInfo:
    hostname: str
    primary_ipv4: Optional[str]
    gateway: Optional[str]
    netmask: Optional[str]
    cidr: Optional[str]
    interfaces: List[Interface] = field(default_factory=list)
    dns_servers: List[str] = field(default_factory=list)
    public_ip: Optional[str] = None
    isp: Optional[str] = None
    location: Optional[str] = None

    def to_dict(self) -> Dict:
        return {
            "hostname": self.hostname,
            "primary_ipv4": self.primary_ipv4,
            "gateway": self.gateway,
            "netmask": self.netmask,
            "cidr": self.cidr,
            "interfaces": [i.__dict__ for i in self.interfaces],
            "dns_servers": self.dns_servers,
            "public_ip": self.public_ip,
            "isp": self.isp,
            "location": self.location,
        }


def primary_ipv4() -> Optional[str]:
    """Best-effort primary outbound IPv4, without needing any external tool."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # no packets are actually sent for UDP connect
        return s.getsockname()[0]
    except OSError:
        try:
            return socket.gethostbyname(socket.gethostname())
        except OSError:
            return None
    finally:
        s.close()


def _ioctl_ip(sock: "socket.socket", ifname: str, code: int) -> Optional[str]:
    if fcntl is None:
        return None
    try:
        packed = struct.pack("256s", ifname.encode()[:15])
        res = fcntl.ioctl(sock.fileno(), code, packed)
        return socket.inet_ntoa(res[20:24])
    except OSError:
        return None


def _ioctl_mac(sock: "socket.socket", ifname: str) -> Optional[str]:
    if fcntl is None:
        return None
    try:
        packed = struct.pack("256s", ifname.encode()[:15])
        info = fcntl.ioctl(sock.fileno(), _SIOCGIFHWADDR, packed)
        mac = ":".join("%02x" % b for b in info[18:24])
        return mac if mac != "00:00:00:00:00:00" else None
    except OSError:
        return None


def list_interfaces() -> List[Interface]:
    """Enumerate interfaces with their IPv4/netmask/MAC where discoverable."""
    out: List[Interface] = []
    try:
        names = [name for _, name in socket.if_nameindex()]
    except (OSError, AttributeError):
        names = []
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for name in names:
            ipv4 = _ioctl_ip(sock, name, _SIOCGIFADDR)
            netmask = _ioctl_ip(sock, name, _SIOCGIFNETMASK)
            mac = _ioctl_mac(sock, name)
            cidr = None
            if ipv4 and netmask:
                try:
                    cidr = str(ipaddress.IPv4Network(f"{ipv4}/{netmask}", strict=False))
                except ValueError:
                    cidr = None
            out.append(Interface(name=name, ipv4=ipv4, netmask=netmask, mac=mac, cidr=cidr))
    finally:
        sock.close()
    return out


def default_gateway() -> Optional[str]:
    """Read the default gateway from ``/proc/net/route`` (Linux)."""
    try:
        with open("/proc/net/route", "r", encoding="ascii") as fh:
            next(fh)  # header
            for line in fh:
                fields = line.split()
                if len(fields) < 3:
                    continue
                dest, gw, flags = fields[1], fields[2], int(fields[3], 16)
                if dest == "00000000" and (flags & 0x2):  # default route, gateway up
                    # /proc stores the address little-endian; pack it back the
                    # same way so inet_ntoa reads the octets in the right order.
                    return socket.inet_ntoa(struct.pack("<L", int(gw, 16)))
    except (OSError, StopIteration, ValueError):
        pass
    return None


def _subnet_for(ip: str, interfaces: List[Interface]) -> Optional[str]:
    for iface in interfaces:
        if iface.ipv4 == ip and iface.cidr:
            return iface.cidr
    # Fallback: read on-link routes from /proc/net/route
    try:
        target = ipaddress.IPv4Address(ip)
        with open("/proc/net/route", "r", encoding="ascii") as fh:
            next(fh)
            for line in fh:
                f = line.split()
                if len(f) < 8 or f[1] == "00000000":
                    continue
                dest = socket.inet_ntoa(struct.pack("<L", int(f[1], 16)))
                mask = socket.inet_ntoa(struct.pack("<L", int(f[7], 16)))
                net = ipaddress.IPv4Network(f"{dest}/{mask}", strict=False)
                if target in net and net.prefixlen >= 8:
                    return str(net)
    except (OSError, StopIteration, ValueError):
        pass
    # Last resort: assume a /24
    try:
        return str(ipaddress.IPv4Network(f"{ip}/24", strict=False))
    except ValueError:
        return None


def dns_servers() -> List[str]:
    servers: List[str] = []
    try:
        with open("/etc/resolv.conf", "r", encoding="ascii", errors="ignore") as fh:
            for line in fh:
                line = line.strip()
                if line.startswith("nameserver"):
                    parts = line.split()
                    if len(parts) >= 2:
                        servers.append(parts[1])
    except OSError:
        pass
    return servers


def public_info(timeout: float = 5.0) -> Dict[str, Optional[str]]:
    """Look up the public IP, ISP and rough location (needs internet)."""
    result: Dict[str, Optional[str]] = {"public_ip": None, "isp": None, "location": None}
    try:
        req = urllib.request.Request(
            "http://ip-api.com/json/?fields=query,isp,org,city,regionName,country",
            headers={"User-Agent": "bing/1.0"},
        )
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
        result["public_ip"] = data.get("query")
        result["isp"] = data.get("isp") or data.get("org")
        loc = ", ".join(x for x in (data.get("city"), data.get("regionName"),
                                    data.get("country")) if x)
        result["location"] = loc or None
    except Exception:
        # Fall back to a bare IP-only endpoint if the rich one is blocked.
        try:
            with urllib.request.urlopen("https://api.ipify.org", timeout=timeout) as resp:
                result["public_ip"] = resp.read().decode().strip()
        except Exception:
            pass
    return result


def gather(include_public: bool = True) -> NetworkInfo:
    """Collect a full picture of the host's network context."""
    hostname = socket.gethostname()
    interfaces = list_interfaces()
    ip = primary_ipv4()
    gw = default_gateway()
    cidr = _subnet_for(ip, interfaces) if ip else None
    netmask = None
    if cidr:
        try:
            netmask = str(ipaddress.IPv4Network(cidr).netmask)
        except ValueError:
            netmask = None

    info = NetworkInfo(
        hostname=hostname,
        primary_ipv4=ip,
        gateway=gw,
        netmask=netmask,
        cidr=cidr,
        interfaces=interfaces,
        dns_servers=dns_servers(),
    )
    if include_public:
        pub = public_info()
        info.public_ip = pub["public_ip"]
        info.isp = pub["isp"]
        info.location = pub["location"]
    return info
