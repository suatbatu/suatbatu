"""Wake-on-LAN — send a magic packet to power on a sleeping device."""

from __future__ import annotations

import socket

from .oui import normalize


def build_magic_packet(mac: str) -> bytes:
    """Construct the 102-byte WoL magic packet for *mac*."""
    norm = normalize(mac)
    if not norm:
        raise ValueError(f"invalid MAC address: {mac!r}")
    mac_bytes = bytes.fromhex(norm.replace(":", ""))
    return b"\xff" * 6 + mac_bytes * 16


def wake(mac: str, broadcast: str = "255.255.255.255", port: int = 9) -> None:
    """Broadcast a magic packet.  Port 9 (discard) or 7 (echo) are conventional."""
    packet = build_magic_packet(mac)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.sendto(packet, (broadcast, port))
        # Repeat on the alternate conventional port for stubborn NICs.
        alt = 7 if port == 9 else 9
        sock.sendto(packet, (broadcast, alt))
    finally:
        sock.close()
