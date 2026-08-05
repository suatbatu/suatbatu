"""Latency measurement.

Two strategies:

* **TCP ping** — time a ``connect()`` to a port.  Needs no privileges, works
  everywhere, and is what mobile apps use since raw ICMP is sandboxed away.
* **ICMP ping** — a real echo request, used automatically when the process can
  open a raw / ICMP-datagram socket (root, or ``net.ipv4.ping_group_range``).
"""

from __future__ import annotations

import os
import socket
import struct
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .util import Stopwatch


@dataclass
class PingReply:
    seq: int
    success: bool
    rtt_ms: Optional[float] = None
    error: Optional[str] = None


@dataclass
class PingStats:
    host: str
    address: Optional[str]
    method: str
    transmitted: int
    received: int
    replies: List[PingReply] = field(default_factory=list)

    @property
    def loss_pct(self) -> float:
        if self.transmitted == 0:
            return 0.0
        return round(100.0 * (self.transmitted - self.received) / self.transmitted, 1)

    @property
    def rtts(self) -> List[float]:
        return [r.rtt_ms for r in self.replies if r.success and r.rtt_ms is not None]

    def summary(self) -> Dict[str, Optional[float]]:
        rtts = self.rtts
        if not rtts:
            return {"min": None, "avg": None, "max": None, "jitter": None}
        avg = sum(rtts) / len(rtts)
        jitter = (sum((x - avg) ** 2 for x in rtts) / len(rtts)) ** 0.5
        return {
            "min": round(min(rtts), 2),
            "avg": round(avg, 2),
            "max": round(max(rtts), 2),
            "jitter": round(jitter, 2),
        }

    def to_dict(self) -> Dict:
        return {
            "host": self.host,
            "address": self.address,
            "method": self.method,
            "transmitted": self.transmitted,
            "received": self.received,
            "loss_pct": self.loss_pct,
            "summary": self.summary(),
            "replies": [r.__dict__ for r in self.replies],
        }


def _checksum(data: bytes) -> int:
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) + data[i + 1]
    total = (total >> 16) + (total & 0xFFFF)
    total += total >> 16
    return ~total & 0xFFFF


def _icmp_socket() -> Optional["socket.socket"]:
    """Try to obtain an ICMP socket without raising to callers."""
    for kind in (socket.SOCK_DGRAM, socket.SOCK_RAW):
        try:
            s = socket.socket(socket.AF_INET, kind, socket.IPPROTO_ICMP)
            return s
        except (PermissionError, OSError):
            continue
    return None


def icmp_ping_once(sock: "socket.socket", addr: str, seq: int, ident: int,
                   timeout: float) -> PingReply:
    header = struct.pack(">BBHHH", 8, 0, 0, ident, seq)
    payload = struct.pack(">d", time.time()) + b"bing-icmp-probe"
    chksum = _checksum(header + payload)
    packet = struct.pack(">BBHHH", 8, 0, chksum, ident, seq) + payload

    sock.settimeout(timeout)
    start = time.perf_counter()
    try:
        sock.sendto(packet, (addr, 0))
        while True:
            data, _ = sock.recvfrom(1024)
            rtt = (time.perf_counter() - start) * 1000
            # DGRAM ICMP sockets strip the IP header; RAW sockets include it.
            icmp = data[20:] if len(data) >= 28 and data[0] >> 4 == 4 else data
            if len(icmp) < 8:
                continue
            itype, _, _, rident, rseq = struct.unpack(">BBHHH", icmp[:8])
            if itype == 0 and rseq == seq:
                return PingReply(seq=seq, success=True, rtt_ms=round(rtt, 2))
            if time.perf_counter() - start > timeout:
                return PingReply(seq=seq, success=False, error="timeout")
    except socket.timeout:
        return PingReply(seq=seq, success=False, error="timeout")
    except OSError as exc:
        return PingReply(seq=seq, success=False, error=str(exc))


def tcp_ping_once(addr: str, port: int, seq: int, timeout: float) -> PingReply:
    sw = Stopwatch()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        rc = sock.connect_ex((addr, port))
        rtt = sw.elapsed() * 1000
        # An open port *or* an actively refused connection both prove the host
        # answered within the round-trip — either way the latency is valid.
        if rc == 0 or rc in (111, 61, 10061):
            return PingReply(seq=seq, success=True, rtt_ms=round(rtt, 2))
        return PingReply(seq=seq, success=False, error=os.strerror(rc) if rc else "no route")
    except socket.timeout:
        return PingReply(seq=seq, success=False, error="timeout")
    except OSError as exc:
        return PingReply(seq=seq, success=False, error=str(exc))
    finally:
        sock.close()


def ping(
    host: str,
    count: int = 4,
    timeout: float = 1.0,
    interval: float = 0.3,
    port: int = 443,
    force_tcp: bool = False,
    on_reply=None,
) -> PingStats:
    """Ping *host* ``count`` times, auto-selecting ICMP when available."""
    try:
        address = socket.gethostbyname(host)
    except socket.gaierror:
        return PingStats(host=host, address=None, method="none", transmitted=0,
                         received=0)

    icmp_sock = None if force_tcp else _icmp_socket()
    method = "icmp" if icmp_sock else "tcp"
    ident = os.getpid() & 0xFFFF
    stats = PingStats(host=host, address=address, method=method,
                      transmitted=0, received=0)

    try:
        for seq in range(count):
            if seq:
                time.sleep(interval)
            if icmp_sock is not None:
                reply = icmp_ping_once(icmp_sock, address, seq, ident, timeout)
            else:
                reply = tcp_ping_once(address, port, seq, timeout)
            stats.transmitted += 1
            if reply.success:
                stats.received += 1
            stats.replies.append(reply)
            if on_reply is not None:
                on_reply(reply)
    finally:
        if icmp_sock is not None:
            icmp_sock.close()
    return stats
