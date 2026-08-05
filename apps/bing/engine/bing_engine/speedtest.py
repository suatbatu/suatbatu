"""A lightweight internet speed test.

Measures latency, download and upload throughput against Cloudflare's
``speed.cloudflare.com`` endpoints (no API key, CORS-friendly, globally
anycast).  Falls back to a smaller download if the network is slow.  This is a
rough "is my line healthy" check, not a lab-grade benchmark.
"""

from __future__ import annotations

import time
import urllib.request
from dataclasses import dataclass
from typing import Dict, Optional

_DOWN_URL = "https://speed.cloudflare.com/__down?bytes={n}"
_UP_URL = "https://speed.cloudflare.com/__up"
_META_URL = "https://speed.cloudflare.com/meta"


@dataclass
class SpeedResult:
    latency_ms: Optional[float] = None
    jitter_ms: Optional[float] = None
    download_mbps: Optional[float] = None
    upload_mbps: Optional[float] = None
    server: Optional[str] = None
    client_ip: Optional[str] = None
    isp: Optional[str] = None
    error: Optional[str] = None

    def to_dict(self) -> Dict:
        return self.__dict__.copy()


def _measure_latency(samples: int = 5, timeout: float = 5.0):
    times = []
    for _ in range(samples):
        start = time.perf_counter()
        try:
            req = urllib.request.Request(_DOWN_URL.format(n=0),
                                         headers={"User-Agent": "bing-speed/1.0"})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                resp.read()
            times.append((time.perf_counter() - start) * 1000)
        except Exception:
            continue
    if not times:
        return None, None
    avg = sum(times) / len(times)
    jitter = (sum((t - avg) ** 2 for t in times) / len(times)) ** 0.5
    return round(min(times), 2), round(jitter, 2)


def _measure_download(total_bytes: int, timeout: float) -> Optional[float]:
    req = urllib.request.Request(_DOWN_URL.format(n=total_bytes),
                                 headers={"User-Agent": "bing-speed/1.0"})
    start = time.perf_counter()
    read = 0
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            while True:
                chunk = resp.read(65536)
                if not chunk:
                    break
                read += len(chunk)
                if time.perf_counter() - start > timeout:
                    break
    except Exception:
        if read == 0:
            return None
    elapsed = time.perf_counter() - start
    if elapsed <= 0 or read == 0:
        return None
    return round((read * 8) / elapsed / 1e6, 2)


def _measure_upload(total_bytes: int, timeout: float) -> Optional[float]:
    payload = b"\x00" * total_bytes
    req = urllib.request.Request(_UP_URL, data=payload, method="POST",
                                 headers={"Content-Type": "application/octet-stream",
                                          "User-Agent": "bing-speed/1.0"})
    start = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            resp.read()
    except Exception:
        return None
    elapsed = time.perf_counter() - start
    if elapsed <= 0:
        return None
    return round((total_bytes * 8) / elapsed / 1e6, 2)


def _meta(timeout: float) -> Dict[str, Optional[str]]:
    try:
        import json
        req = urllib.request.Request(_META_URL, headers={"User-Agent": "bing-speed/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read().decode("utf-8", "replace"))
        loc = data.get("colo")
        return {
            "server": f"Cloudflare {loc}" if loc else "Cloudflare",
            "client_ip": data.get("clientIp"),
            "isp": data.get("asOrganization"),
        }
    except Exception:
        return {"server": None, "client_ip": None, "isp": None}


def run(quick: bool = False, timeout: float = 15.0) -> SpeedResult:
    """Run the full test.  ``quick`` uses smaller transfers for a faster estimate."""
    result = SpeedResult()
    meta = _meta(min(timeout, 5.0))
    result.server = meta["server"]
    result.client_ip = meta["client_ip"]
    result.isp = meta["isp"]

    result.latency_ms, result.jitter_ms = _measure_latency()

    down_bytes = 10_000_000 if quick else 50_000_000
    up_bytes = 2_000_000 if quick else 10_000_000

    result.download_mbps = _measure_download(down_bytes, timeout)
    result.upload_mbps = _measure_upload(up_bytes, timeout)

    if result.download_mbps is None and result.latency_ms is None:
        result.error = "Speed test unreachable — check your internet connection."
    return result
