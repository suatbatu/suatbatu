#!/usr/bin/env python3
"""Sensor simulator for ForestGuard.

Posts realistic telemetry for a handful of virtual forest nodes so you can see
the dashboard work and exercise the alert path without hardware. One node can be
driven into a fire scenario (rising temp, dropping humidity, smoke spike, flame)
to watch the risk level climb and the alerts fire.

    python3 scripts/simulate.py --url http://localhost:8080 --key KEY1 \
        --nodes 5 --interval 3 --fire ridge-3

Dependency-free (urllib only).
"""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
import time
import urllib.error
import urllib.request

SITE = [
    ("ridge-1", "Ridge West", 40.201, 29.010),
    ("ridge-2", "Ridge East", 40.205, 29.020),
    ("valley-1", "Valley Floor", 40.190, 29.015),
    ("creek-1", "Creek Bend", 40.198, 29.032),
    ("summit-1", "Summit Tower", 40.210, 29.008),
    ("meadow-1", "Meadow North", 40.215, 29.025),
]


def baseline(node_id: str, tick: int) -> dict:
    """Calm-forest readings with gentle diurnal + noise variation."""
    phase = (tick / 40.0) + hash(node_id) % 7
    temp = 22 + 6 * math.sin(phase) + random.uniform(-1, 1)
    humidity = 55 - 10 * math.sin(phase) + random.uniform(-3, 3)
    return {
        "temp_c": round(temp, 1),
        "humidity": round(max(5, min(95, humidity)), 0),
        "smoke": round(90 + random.uniform(0, 40), 0),
        "gas": round(80 + random.uniform(0, 30), 0),
        "flame": 0,
        "battery_v": round(3.9 + random.uniform(-0.15, 0.1), 2),
    }


def fire_progression(tick_in_fire: int) -> dict:
    """A worsening fire: heat rises, air dries, smoke/gas spike, flame appears."""
    p = min(1.0, tick_in_fire / 8.0)
    return {
        "temp_c": round(28 + 35 * p + random.uniform(-1, 1), 1),
        "humidity": round(max(6, 45 - 35 * p), 0),
        "smoke": round(150 + 650 * p + random.uniform(-20, 20), 0),
        "gas": round(120 + 500 * p, 0),
        "flame": 1 if p > 0.6 else 0,
        "battery_v": round(3.8 - 0.2 * p, 2),
    }


def post(url: str, key: str | None, readings: list[dict]) -> None:
    body = json.dumps({"readings": readings}).encode()
    headers = {"Content-Type": "application/json"}
    if key:
        headers["X-API-Key"] = key
    req = urllib.request.Request(url.rstrip("/") + "/api/v1/telemetry",
                                 data=body, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            payload = json.loads(resp.read())
            print(f"  -> {resp.status} accepted={payload.get('accepted')} "
                  f"worst={payload.get('worst_level')}")
    except urllib.error.HTTPError as exc:
        print(f"  !! HTTP {exc.code}: {exc.read().decode()[:200]}", file=sys.stderr)
    except OSError as exc:
        print(f"  !! connection failed: {exc}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description="ForestGuard sensor simulator")
    ap.add_argument("--url", default="http://localhost:8080")
    ap.add_argument("--key", default=None, help="X-API-Key if the server needs one")
    ap.add_argument("--nodes", type=int, default=5)
    ap.add_argument("--interval", type=float, default=3.0)
    ap.add_argument("--once", action="store_true", help="send one batch and exit")
    ap.add_argument("--fire", metavar="NODE_ID",
                    help="drive this node into a fire scenario")
    ap.add_argument("--fire-after", type=int, default=3,
                    help="ticks before the fire node starts burning")
    args = ap.parse_args()

    nodes = SITE[: max(1, min(args.nodes, len(SITE)))]
    print(f"Simulating {len(nodes)} node(s) -> {args.url}"
          + (f" (fire: {args.fire})" if args.fire else ""))

    tick = 0
    while True:
        readings = []
        for node_id, name, lat, lon in nodes:
            if args.fire and node_id == args.fire and tick >= args.fire_after:
                metrics = fire_progression(tick - args.fire_after)
            else:
                metrics = baseline(node_id, tick)
            readings.append({"node": node_id, "name": name, "lat": lat, "lon": lon,
                             "ts": time.time(), "metrics": metrics})
        print(f"tick {tick}:")
        post(args.url, args.key, readings)
        tick += 1
        if args.once:
            return 0
        try:
            time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\nstopped.")
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
