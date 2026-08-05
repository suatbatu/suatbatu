"""Local-network device discovery — the flagship "who's on my network" scan.

Strategy (all privilege-free):

1. Enumerate every host in the target subnet.
2. TCP-probe a handful of "liveness" ports on each.  A completed handshake *or*
   an actively refused connection both prove the host is up — so even firewalled
   devices with every port closed are still found.
3. Read the kernel ARP cache (``/proc/net/arp``), which the probes just
   populated, to attach MAC addresses (and to catch hosts that answered at L2
   but not on any probed port).
4. Enrich each device with vendor (OUI), hostname (reverse-DNS / mDNS) and a
   best-effort device-type guess.
"""

from __future__ import annotations

import ipaddress
import socket
import struct
import threading
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional

from . import oui
from .dnsr import reverse
from .netinfo import default_gateway, gather, primary_ipv4
from .util import parallel_map

# Ports probed to decide "is this host alive?".  Chosen to cover phones, PCs,
# printers, IoT, media devices and routers.
_LIVENESS_PORTS = [80, 443, 22, 445, 139, 135, 53, 5000, 7000, 8009, 8008,
                   62078, 3389, 548, 5353, 9100, 1883, 8080, 5555, 32400]


@dataclass
class Device:
    ip: str
    mac: Optional[str] = None
    vendor: Optional[str] = None
    hostname: Optional[str] = None
    open_ports: List[int] = field(default_factory=list)
    rtt_ms: Optional[float] = None
    device_type: str = "Unknown"
    icon: str = "device"
    is_gateway: bool = False
    is_self: bool = False

    def to_dict(self) -> Dict:
        return {
            "ip": self.ip,
            "mac": self.mac,
            "vendor": self.vendor,
            "hostname": self.hostname,
            "open_ports": self.open_ports,
            "rtt_ms": self.rtt_ms,
            "device_type": self.device_type,
            "icon": self.icon,
            "is_gateway": self.is_gateway,
            "is_self": self.is_self,
        }


def _sort_key(ip: str):
    try:
        return int(ipaddress.IPv4Address(ip))
    except ValueError:
        return 0


def read_arp_table() -> Dict[str, str]:
    """Map IP → MAC from the kernel ARP cache (Linux ``/proc/net/arp``)."""
    table: Dict[str, str] = {}
    try:
        with open("/proc/net/arp", "r", encoding="ascii") as fh:
            next(fh)  # header
            for line in fh:
                fields = line.split()
                if len(fields) >= 4:
                    ip, _hw, _flags, mac = fields[0], fields[1], fields[2], fields[3]
                    if mac and mac != "00:00:00:00:00:00":
                        table[ip] = mac.upper()
    except (OSError, StopIteration):
        pass
    return table


def _probe_liveness(ip: str, timeout: float) -> Optional[float]:
    """Return RTT in ms if the host answers on any liveness port, else ``None``."""
    best: Optional[float] = None
    for port in _LIVENESS_PORTS[:8]:  # first 8 keep the sweep fast
        import time
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        start = time.perf_counter()
        try:
            rc = sock.connect_ex((ip, port))
            elapsed = (time.perf_counter() - start) * 1000
            if rc == 0 or rc in (111, 61, 10061):  # open or refused → alive
                best = elapsed if best is None else min(best, elapsed)
                break
        except OSError:
            pass
        finally:
            sock.close()
    return best


def _quick_ports(ip: str, timeout: float, ports: List[int]) -> List[int]:
    open_ports: List[int] = []
    lock = threading.Lock()

    def check(port: int) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            if sock.connect_ex((ip, port)) == 0:
                with lock:
                    open_ports.append(port)
        except OSError:
            pass
        finally:
            sock.close()

    parallel_map(check, ports, workers=min(len(ports), 40))
    return sorted(open_ports)


def guess_device(dev: Device) -> None:
    """Populate ``device_type``/``icon`` from vendor, ports and hostname."""
    vendor = (dev.vendor or "").lower()
    host = (dev.hostname or "").lower()
    ports = set(dev.open_ports)

    def set_type(name: str, icon: str) -> None:
        dev.device_type, dev.icon = name, icon

    if dev.is_gateway:
        return set_type("Router / Gateway", "router")
    # Strong hostname signals first.
    for needle, (name, icon) in {
        "iphone": ("iPhone", "phone"), "ipad": ("iPad", "tablet"),
        "macbook": ("MacBook", "laptop"), "android": ("Android device", "phone"),
        "printer": ("Printer", "printer"), "-tv": ("Smart TV", "tv"),
        "chromecast": ("Chromecast", "cast"), "roku": ("Streaming device", "cast"),
        "camera": ("IP Camera", "camera"), "nas": ("NAS / Storage", "storage"),
        "router": ("Router / Gateway", "router"), "switch": ("Network switch", "router"),
    }.items():
        if needle in host:
            return set_type(name, icon)

    if 9100 in ports or 515 in ports or 631 in ports:
        return set_type("Printer", "printer")
    if 32400 in ports:
        return set_type("Plex media server", "media")
    if ports & {8009, 8008}:
        return set_type("Chromecast / Cast", "cast")
    if 554 in ports and ("camera" in vendor or 80 in ports):
        return set_type("IP Camera", "camera")
    if "sonos" in vendor:
        return set_type("Sonos speaker", "speaker")

    if "espressif" in vendor:
        return set_type("IoT device (ESP)", "iot")
    if "raspberry" in vendor:
        return set_type("Raspberry Pi", "server")
    if "apple" in vendor:
        if 62078 in ports:
            return set_type("iPhone / iPad", "phone")
        return set_type("Apple device", "laptop")
    if "samsung" in vendor:
        return set_type("Samsung device", "phone")
    if any(v in vendor for v in ("xiaomi", "huawei", "oneplus")):
        return set_type("Mobile device", "phone")
    if any(v in vendor for v in ("amazon",)):
        return set_type("Amazon device", "speaker")
    if any(v in vendor for v in ("google", "nest")):
        return set_type("Google device", "cast")
    if any(v in vendor for v in ("sonos", "onkyo")):
        return set_type("Media device", "speaker")

    if ports & {445, 139, 135, 3389}:
        return set_type("Windows PC", "desktop")
    if 22 in ports and ports & {80, 443}:
        return set_type("Server", "server")
    if 22 in ports:
        return set_type("Computer / Linux", "desktop")
    if ports & {80, 443}:
        return set_type("Web device", "device")
    if ports & {1883, 8883, 5683}:
        return set_type("IoT device", "iot")
    if dev.mac is None:
        return set_type("Unknown", "device")
    set_type("Network device", "device")


def scan(
    cidr: Optional[str] = None,
    timeout: float = 0.4,
    workers: int = 256,
    with_ports: bool = True,
    resolve_names: bool = True,
    port_set: Optional[List[int]] = None,
    on_device: Optional[Callable[[Device], None]] = None,
) -> List[Device]:
    """Discover devices on *cidr* (defaults to the host's own subnet).

    ``on_device`` is invoked as each device is confirmed, for live UIs.
    """
    if cidr is None:
        info = gather(include_public=False)
        cidr = info.cidr
        if cidr is None:
            raise RuntimeError("could not determine local subnet; pass cidr explicitly")

    network = ipaddress.IPv4Network(cidr, strict=False)
    gateway = default_gateway()
    myself = primary_ipv4()

    # Guard against accidentally scanning the whole internet.
    if network.num_addresses > 65536:
        raise ValueError(f"subnet {cidr} too large ({network.num_addresses} hosts); "
                         "narrow it down (<= /16)")

    hosts = [str(h) for h in network.hosts()]
    quick_ports = port_set or [22, 80, 443, 445, 139, 8080, 62078, 9100, 8009,
                               554, 32400, 5000, 1883, 3389, 53, 548, 7000]

    found: Dict[str, Device] = {}
    lock = threading.Lock()

    def worker(ip: str) -> Optional[Device]:
        rtt = _probe_liveness(ip, timeout)
        arp_mac = None  # filled after the sweep from the ARP cache
        if rtt is None and ip not in (gateway, myself):
            return None
        dev = Device(ip=ip, rtt_ms=round(rtt, 2) if rtt is not None else None)
        dev.is_gateway = (ip == gateway)
        dev.is_self = (ip == myself)
        if with_ports:
            dev.open_ports = _quick_ports(ip, timeout, quick_ports)
        with lock:
            found[ip] = dev
        return dev

    parallel_map(worker, hosts, workers=workers)

    # Attach MACs from the (now-populated) ARP cache; add L2-only hosts.
    arp = read_arp_table()
    for ip, mac in arp.items():
        if ip not in found:
            try:
                if ipaddress.IPv4Address(ip) not in network:
                    continue
            except ValueError:
                continue
            found[ip] = Device(ip=ip)
        found[ip].mac = mac

    # Make sure gateway & self always appear.
    for special in (gateway, myself):
        if special and special not in found:
            try:
                if ipaddress.IPv4Address(special) in network:
                    found[special] = Device(ip=special)
            except ValueError:
                pass
    if gateway and gateway in found:
        found[gateway].is_gateway = True
    if myself and myself in found:
        found[myself].is_self = True
        if found[myself].mac is None:
            for iface in gather(include_public=False).interfaces:
                if iface.ipv4 == myself and iface.mac:
                    found[myself].mac = iface.mac.upper()

    # Enrich: vendor, hostname, device type — in parallel.
    devices = list(found.values())

    def enrich(dev: Device) -> Device:
        if dev.mac:
            dev.vendor = oui.lookup(dev.mac)
        if resolve_names:
            dev.hostname = reverse(dev.ip)
            if not dev.hostname:
                dev.hostname = _mdns_hostname(dev.ip)
        guess_device(dev)
        if on_device is not None:
            on_device(dev)
        return dev

    parallel_map(enrich, devices, workers=min(len(devices) or 1, 64))
    return sorted(found.values(), key=lambda d: _sort_key(d.ip))


def _mdns_hostname(ip: str, timeout: float = 0.6) -> Optional[str]:
    """Best-effort mDNS reverse lookup (``x.x.x.x.in-addr.arpa`` over 5353)."""
    ptr = ".".join(reversed(ip.split("."))) + ".in-addr.arpa"
    # Minimal DNS PTR query.
    header = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    qname = b"".join(bytes([len(p)]) + p.encode() for p in ptr.split(".")) + b"\x00"
    packet = header + qname + struct.pack(">HH", 12, 1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(timeout)
    try:
        sock.sendto(packet, ("224.0.0.251", 5353))
        data, _ = sock.recvfrom(1024)
        # Extremely small parser: find the answer name.
        from .dnsr import _decode_name  # local import to avoid cycle at import time
        _, _, qd, an, _, _ = struct.unpack(">HHHHHH", data[:12])
        if an < 1:
            return None
        offset = 12
        for _ in range(qd):
            _, offset = _decode_name(data, offset)
            offset += 4
        _, offset = _decode_name(data, offset)
        rtype, _cls, _ttl, rdlen = struct.unpack(">HHIH", data[offset:offset + 10])
        offset += 10
        if rtype == 12:
            name, _ = _decode_name(data, offset)
            return name.rstrip(".")
    except (OSError, struct.error, IndexError):
        return None
    finally:
        sock.close()
    return None
