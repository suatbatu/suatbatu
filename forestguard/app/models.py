"""Lightweight validation for incoming telemetry — no pydantic dependency.

Accepts either a single reading object or a batch. A reading is intentionally
permissive about *which* metrics it carries (a node reports whatever sensors it
has), but strict about types so bad data never reaches the database.
"""

from __future__ import annotations

import time
from dataclasses import dataclass


class ValidationError(ValueError):
    """Raised for a malformed telemetry payload. Message is client-safe."""


# A telemetry timestamp this far from now (seconds) is rejected as bogus, so a
# node with a wildly wrong clock can't poison the time series.
MAX_CLOCK_SKEW_S = 3 * 24 * 3600
_MAX_METRICS = 64


@dataclass
class Reading:
    node: str
    ts: float
    metrics: dict[str, float]
    name: str | None = None
    lat: float | None = None
    lon: float | None = None


def _num(value, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValidationError(f"'{field}' must be a number")
    f = float(value)
    if f != f or f in (float("inf"), float("-inf")):  # NaN / inf
        raise ValidationError(f"'{field}' must be finite")
    return f


def _opt_num(obj: dict, field: str) -> float | None:
    if field not in obj or obj[field] is None:
        return None
    return _num(obj[field], field)


def parse_reading(obj: dict, now: float | None = None) -> Reading:
    now = now if now is not None else time.time()
    if not isinstance(obj, dict):
        raise ValidationError("reading must be a JSON object")

    node = obj.get("node") or obj.get("node_id") or obj.get("id")
    if not isinstance(node, str) or not node.strip():
        raise ValidationError("'node' (string id) is required")
    node = node.strip()
    if len(node) > 128:
        raise ValidationError("'node' id too long")

    ts = obj.get("ts")
    if ts is None:
        ts = now
    else:
        ts = _num(ts, "ts")
        # Accept milliseconds if someone sends them.
        if ts > 1e12:
            ts /= 1000.0
        if abs(ts - now) > MAX_CLOCK_SKEW_S:
            raise ValidationError("'ts' is too far from server time")

    raw_metrics = obj.get("metrics")
    if raw_metrics is None:
        # Also allow flat form: any top-level numeric field becomes a metric.
        reserved = {"node", "node_id", "id", "ts", "name", "lat", "lon"}
        raw_metrics = {k: v for k, v in obj.items()
                       if k not in reserved and isinstance(v, (int, float))
                       and not isinstance(v, bool)}
    if not isinstance(raw_metrics, dict) or not raw_metrics:
        raise ValidationError("'metrics' object with at least one reading is required")
    if len(raw_metrics) > _MAX_METRICS:
        raise ValidationError("too many metrics in one reading")

    metrics: dict[str, float] = {}
    for k, v in raw_metrics.items():
        if not isinstance(k, str) or not k.strip():
            raise ValidationError("metric names must be non-empty strings")
        metrics[k.strip()[:64]] = _num(v, k)

    name = obj.get("name")
    if name is not None and not isinstance(name, str):
        raise ValidationError("'name' must be a string")

    return Reading(
        node=node,
        ts=ts,
        metrics=metrics,
        name=name.strip()[:128] if isinstance(name, str) and name.strip() else None,
        lat=_opt_num(obj, "lat"),
        lon=_opt_num(obj, "lon"),
    )


def parse_payload(body: object, now: float | None = None) -> list[Reading]:
    """Parse a request body into one or more readings.

    Accepts: a single reading object, ``{"readings": [...]}``, or a bare list.
    """
    if isinstance(body, list):
        items = body
    elif isinstance(body, dict) and isinstance(body.get("readings"), list):
        items = body["readings"]
    elif isinstance(body, dict):
        items = [body]
    else:
        raise ValidationError("body must be a JSON object or array")

    if not items:
        raise ValidationError("no readings in payload")
    if len(items) > 500:
        raise ValidationError("too many readings in one batch (max 500)")

    return [parse_reading(item, now=now) for item in items]
