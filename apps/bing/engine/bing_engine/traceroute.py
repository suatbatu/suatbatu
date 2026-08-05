"""Traceroute via incrementing IP TTL.

Sends UDP probes with a rising TTL and listens for the ICMP *time-exceeded*
replies that each router along the path returns.  Receiving ICMP requires a raw
socket (root / CAP_NET_RAW); when that is unavailable we fall back to a
TCP-connect "path probe" that at least confirms reachability and measures RTT to
the destination, and we say so honestly in the result.
"""

from __future__ import annotations

import socket
import struct
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .dnsr import reverse


@dataclass
class Hop:
    ttl: int
    address: Optional[str]
    hostname: Optional[str]
    rtt_ms: Optional[float]
    final: bool = False

    def to_dict(self) -> Dict:
        return {
            "ttl": self.ttl,
            "address": self.address,
            "hostname": self.hostname,
            "rtt_ms": self.rtt_ms,
            "final": self.final,
        }


@dataclass
class TraceResult:
    host: str
    address: Optional[str]
    method: str
    hops: List[Hop] = field(default_factory=list)
    note: Optional[str] = None

    def to_dict(self) -> Dict:
        return {
            "host": self.host,
            "address": self.address,
            "method": self.method,
            "note": self.note,
            "hops": [h.to_dict() for h in self.hops],
        }


def _can_raw() -> bool:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP)
        s.close()
        return True
    except (PermissionError, OSError):
        return False


def _udp_trace(dest: str, max_hops: int, timeout: float, base_port: int,
               resolve: bool) -> List[Hop]:
    hops: List[Hop] = []
    for ttl in range(1, max_hops + 1):
        recv = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP)
        recv.settimeout(timeout)
        recv.bind(("", 0))
        send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        send.setsockopt(socket.IPPROTO_IP, socket.IP_TTL, ttl)
        start = time.perf_counter()
        addr: Optional[str] = None
        final = False
        try:
            send.sendto(b"bing-trace", (dest, base_port + ttl))
            data, raw = recv.recvfrom(512)
            addr = raw[0]
            rtt = (time.perf_counter() - start) * 1000
            icmp_type = data[20] if len(data) > 20 else None
            if icmp_type == 3:  # destination unreachable → we've arrived
                final = True
        except socket.timeout:
            rtt = None
        finally:
            send.close()
            recv.close()

        if addr == dest:
            final = True
        hostname = reverse(addr) if (addr and resolve) else None
        hops.append(Hop(ttl=ttl, address=addr, hostname=hostname,
                        rtt_ms=round(rtt, 2) if rtt is not None else None, final=final))
        if final:
            break
    return hops


def _tcp_fallback(dest: str, timeout: float) -> List[Hop]:
    """Without raw sockets, at least time the final hop by TCP connect."""
    start = time.perf_counter()
    rtt = None
    for port in (443, 80, 22):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        try:
            start = time.perf_counter()
            rc = s.connect_ex((dest, port))
            if rc == 0 or rc in (111, 61, 10061):
                rtt = (time.perf_counter() - start) * 1000
                break
        except OSError:
            pass
        finally:
            s.close()
    return [Hop(ttl=1, address=dest, hostname=reverse(dest),
                rtt_ms=round(rtt, 2) if rtt is not None else None, final=True)]


def trace(
    host: str,
    max_hops: int = 30,
    timeout: float = 2.0,
    base_port: int = 33434,
    resolve: bool = True,
) -> TraceResult:
    """Trace the network path to *host*."""
    try:
        dest = socket.gethostbyname(host)
    except socket.gaierror:
        return TraceResult(host=host, address=None, method="none",
                           note="could not resolve host")

    if _can_raw():
        hops = _udp_trace(dest, max_hops, timeout, base_port, resolve)
        return TraceResult(host=host, address=dest, method="udp/icmp", hops=hops)

    hops = _tcp_fallback(dest, timeout)
    return TraceResult(
        host=host, address=dest, method="tcp-fallback", hops=hops,
        note="Full hop-by-hop tracing needs raw sockets (run as root/CAP_NET_RAW). "
             "Showing reachability to the destination only.",
    )
