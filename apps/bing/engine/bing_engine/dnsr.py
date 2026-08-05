"""A small, dependency-free DNS resolver.

Builds and parses DNS wire format directly over UDP (with TCP fallback on
truncation), so we get ``dig``-style lookups — A, AAAA, MX, TXT, NS, CNAME,
SOA, PTR — without any system tools installed.  Also does OS-level forward and
reverse resolution via ``socket`` for convenience.
"""

from __future__ import annotations

import ipaddress
import socket
import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

QTYPES: Dict[str, int] = {
    "A": 1, "NS": 2, "CNAME": 5, "SOA": 6, "PTR": 12,
    "MX": 15, "TXT": 16, "AAAA": 28, "SRV": 33, "CAA": 257,
}
_QTYPE_NAME = {v: k for k, v in QTYPES.items()}


@dataclass
class Record:
    name: str
    rtype: str
    ttl: int
    value: str

    def to_dict(self) -> Dict:
        return {"name": self.name, "type": self.rtype, "ttl": self.ttl, "value": self.value}


class DNSError(Exception):
    pass


def _encode_name(name: str) -> bytes:
    out = bytearray()
    for label in name.rstrip(".").split("."):
        if not label:
            continue
        b = label.encode("idna") if any(ord(c) > 127 for c in label) else label.encode("ascii")
        if len(b) > 63:
            raise DNSError(f"label too long: {label}")
        out.append(len(b))
        out.extend(b)
    out.append(0)
    return bytes(out)


def _decode_name(data: bytes, offset: int) -> Tuple[str, int]:
    """Decode a (possibly compressed) DNS name; returns (name, next_offset)."""
    labels: List[str] = []
    jumped = False
    next_offset = offset
    hops = 0
    while True:
        if offset >= len(data):
            raise DNSError("truncated name")
        length = data[offset]
        if length & 0xC0 == 0xC0:  # compression pointer
            if offset + 1 >= len(data):
                raise DNSError("truncated pointer")
            pointer = ((length & 0x3F) << 8) | data[offset + 1]
            if not jumped:
                next_offset = offset + 2
            offset = pointer
            jumped = True
            hops += 1
            if hops > 128:
                raise DNSError("compression loop")
            continue
        offset += 1
        if length == 0:
            break
        labels.append(data[offset:offset + length].decode("ascii", "replace"))
        offset += length
    if not jumped:
        next_offset = offset
    return ".".join(labels), next_offset


def _build_query(qname: str, qtype: int, qid: int = 0x1234) -> bytes:
    header = struct.pack(">HHHHHH", qid, 0x0100, 1, 0, 0, 0)  # RD=1
    question = _encode_name(qname) + struct.pack(">HH", qtype, 1)  # class IN
    return header + question


def _parse_rdata(rtype: int, data: bytes, rdstart: int, rdlen: int) -> str:
    end = rdstart + rdlen
    if rtype == 1:  # A
        return socket.inet_ntoa(data[rdstart:rdstart + 4])
    if rtype == 28:  # AAAA
        return socket.inet_ntop(socket.AF_INET6, data[rdstart:rdstart + 16])
    if rtype in (2, 5, 12):  # NS, CNAME, PTR
        name, _ = _decode_name(data, rdstart)
        return name
    if rtype == 15:  # MX
        pref = struct.unpack(">H", data[rdstart:rdstart + 2])[0]
        name, _ = _decode_name(data, rdstart + 2)
        return f"{pref} {name}"
    if rtype == 16:  # TXT — one or more length-prefixed strings
        parts, i = [], rdstart
        while i < end:
            ln = data[i]
            i += 1
            parts.append(data[i:i + ln].decode("utf-8", "replace"))
            i += ln
        return '"' + "".join(parts) + '"'
    if rtype == 6:  # SOA
        mname, i = _decode_name(data, rdstart)
        rname, i = _decode_name(data, i)
        serial, refresh, retry, expire, minimum = struct.unpack(">IIIII", data[i:i + 20])
        return f"{mname} {rname} {serial} {refresh} {retry} {expire} {minimum}"
    if rtype == 33:  # SRV
        prio, weight, port = struct.unpack(">HHH", data[rdstart:rdstart + 6])
        target, _ = _decode_name(data, rdstart + 6)
        return f"{prio} {weight} {port} {target}"
    if rtype == 257:  # CAA
        flags = data[rdstart]
        tag_len = data[rdstart + 1]
        tag = data[rdstart + 2:rdstart + 2 + tag_len].decode("ascii", "replace")
        value = data[rdstart + 2 + tag_len:end].decode("ascii", "replace")
        return f'{flags} {tag} "{value}"'
    return data[rdstart:end].hex()


def _default_resolver() -> str:
    try:
        with open("/etc/resolv.conf", "r", encoding="ascii", errors="ignore") as fh:
            for line in fh:
                if line.startswith("nameserver"):
                    return line.split()[1]
    except OSError:
        pass
    return "1.1.1.1"


def query(
    name: str,
    rtype: str = "A",
    server: Optional[str] = None,
    timeout: float = 3.0,
) -> List[Record]:
    """Resolve *name* / *rtype* against a DNS server (default: system resolver)."""
    rtype = rtype.upper()
    if rtype not in QTYPES:
        raise DNSError(f"unsupported record type: {rtype}")
    server = server or _default_resolver()
    packet = _build_query(name, QTYPES[rtype])

    resp = _exchange(packet, server, timeout)
    return _parse_response(resp)


def _exchange(packet: bytes, server: str, timeout: float) -> bytes:
    # UDP first
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(packet, (server, 53))
        data, _ = sock.recvfrom(4096)
    finally:
        sock.close()
    flags = struct.unpack(">H", data[2:4])[0]
    if flags & 0x0200:  # TC (truncated) — retry over TCP
        return _exchange_tcp(packet, server, timeout)
    return data


def _exchange_tcp(packet: bytes, server: str, timeout: float) -> bytes:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((server, 53))
        sock.sendall(struct.pack(">H", len(packet)) + packet)
        length_bytes = _recv_exact(sock, 2)
        length = struct.unpack(">H", length_bytes)[0]
        return _recv_exact(sock, length)
    finally:
        sock.close()


def _recv_exact(sock: "socket.socket", n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise DNSError("connection closed early")
        buf.extend(chunk)
    return bytes(buf)


def _parse_response(data: bytes) -> List[Record]:
    if len(data) < 12:
        raise DNSError("short response")
    _, flags, qdcount, ancount, _, _ = struct.unpack(">HHHHHH", data[:12])
    rcode = flags & 0x000F
    if rcode == 3:
        raise DNSError("NXDOMAIN — name does not exist")
    if rcode != 0:
        raise DNSError(f"server returned rcode {rcode}")

    offset = 12
    for _ in range(qdcount):  # skip questions
        _, offset = _decode_name(data, offset)
        offset += 4

    records: List[Record] = []
    for _ in range(ancount):
        name, offset = _decode_name(data, offset)
        rtype, rclass, ttl, rdlen = struct.unpack(">HHIH", data[offset:offset + 10])
        offset += 10
        value = _parse_rdata(rtype, data, offset, rdlen)
        offset += rdlen
        records.append(Record(name=name, rtype=_QTYPE_NAME.get(rtype, str(rtype)),
                              ttl=ttl, value=value))
    return records


# --------------------------------------------------------------------------- #
# Convenience helpers backed by the OS resolver                               #
# --------------------------------------------------------------------------- #

def resolve(host: str) -> List[str]:
    """All A/AAAA addresses for *host* via ``getaddrinfo`` (respects /etc/hosts)."""
    addrs: List[str] = []
    try:
        for info in socket.getaddrinfo(host, None):
            ip = info[4][0]
            if ip not in addrs:
                addrs.append(ip)
    except socket.gaierror:
        pass
    return addrs


def reverse(ip: str, timeout: float = 3.0) -> Optional[str]:
    """PTR (reverse-DNS) hostname for an IP, or ``None``."""
    try:
        host, _, _ = socket.gethostbyaddr(ip)
        return host
    except (socket.herror, socket.gaierror, OSError):
        pass
    # Manual PTR query as a fallback.
    try:
        addr = ipaddress.ip_address(ip)
        if addr.version == 4:
            ptr = ".".join(reversed(ip.split("."))) + ".in-addr.arpa"
        else:
            nibbles = "".join(reversed(addr.exploded.replace(":", "")))
            ptr = ".".join(nibbles) + ".ip6.arpa"
        recs = query(ptr, "PTR", timeout=timeout)
        return recs[0].value if recs else None
    except (DNSError, ValueError, OSError):
        return None
