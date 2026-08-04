"""Fire-risk scoring for ForestGuard.

This is a transparent, tunable *heuristic* — not a certified fire-detection
algorithm. It blends four normalized signals (heat, dryness, smoke, gas) into a
0..100 score and maps that to a level. A detected flame, or a hard smoke spike,
overrides the blend so a real event is never averaged away.

Every input is optional: a node that only reports temperature still gets a
(weaker) score from the signals it does provide. This keeps the server robust to
whatever sensor mix a given node actually has.
"""

from __future__ import annotations

from dataclasses import dataclass

from .config import RiskThresholds

# Ordered severity ladder. Higher index = more severe. "offline"/"unknown" are
# lifecycle states applied by the monitor, not produced by scoring.
LEVELS = ("ok", "elevated", "warning", "critical")
LEVEL_ORDER = {name: i for i, name in enumerate(LEVELS)}
OFFLINE = "offline"
UNKNOWN = "unknown"


def level_rank(level: str) -> int:
    """Numeric severity used for comparisons. offline sorts just under critical."""
    if level == OFFLINE:
        return LEVEL_ORDER["warning"] + 0  # treat offline as warning-severity
    if level == UNKNOWN:
        return -1
    return LEVEL_ORDER.get(level, 0)


def _ramp(value: float, low: float, high: float) -> float:
    """Linearly map ``value`` in [low, high] to [0, 1], clamped. Supports a
    reversed band (low > high) for 'lower is worse' signals like humidity."""
    if high == low:
        return 1.0 if value >= high else 0.0
    frac = (value - low) / (high - low)
    return max(0.0, min(1.0, frac))


@dataclass(frozen=True)
class RiskResult:
    score: float                 # 0..100 blended score (before flame override)
    level: str                   # one of LEVELS
    components: dict[str, float]  # each normalized signal, 0..1
    reasons: list[str]           # human-readable drivers of the level
    flame: bool                  # flame detected

    def to_dict(self) -> dict:
        return {
            "score": round(self.score, 1),
            "level": self.level,
            "components": {k: round(v, 3) for k, v in self.components.items()},
            "reasons": self.reasons,
            "flame": self.flame,
        }


# Canonical metric names the scorer understands. Nodes may send more; extras are
# still stored and charted, just not scored.
METRIC_TEMP = "temp_c"
METRIC_HUMIDITY = "humidity"
METRIC_SMOKE = "smoke"
METRIC_GAS = "gas"
METRIC_FLAME = "flame"


def score(metrics: dict[str, float], t: RiskThresholds) -> RiskResult:
    """Compute a :class:`RiskResult` from the latest metric values of one node."""
    components: dict[str, float] = {}
    weights: dict[str, float] = {}
    reasons: list[str] = []

    temp = metrics.get(METRIC_TEMP)
    if temp is not None:
        heat = _ramp(temp, t.temp_low_c, t.temp_high_c)
        components["heat"] = heat
        weights["heat"] = t.weight_heat
        if heat >= 0.6:
            reasons.append(f"high temperature ({temp:g}°C)")

    humidity = metrics.get(METRIC_HUMIDITY)
    if humidity is not None:
        dryness = _ramp(humidity, t.humidity_high_pct, t.humidity_low_pct)
        components["dryness"] = dryness
        weights["dryness"] = t.weight_dryness
        if dryness >= 0.6:
            reasons.append(f"low humidity ({humidity:g}%)")

    smoke = metrics.get(METRIC_SMOKE)
    if smoke is not None:
        smoke_n = _ramp(smoke, t.smoke_baseline, t.smoke_max)
        components["smoke"] = smoke_n
        weights["smoke"] = t.weight_smoke
        if smoke_n >= 0.5:
            reasons.append(f"elevated smoke ({smoke:g})")

    gas = metrics.get(METRIC_GAS)
    if gas is not None:
        gas_n = _ramp(gas, t.gas_baseline, t.gas_max)
        components["gas"] = gas_n
        weights["gas"] = t.weight_gas
        if gas_n >= 0.5:
            reasons.append(f"elevated gas ({gas:g})")

    # Weighted mean over whatever signals are present.
    total_w = sum(weights.values())
    if total_w > 0:
        blended = sum(components[k] * weights[k] for k in weights) / total_w
    else:
        blended = 0.0
    base_score = 100.0 * blended

    flame_val = metrics.get(METRIC_FLAME)
    flame = flame_val is not None and flame_val >= t.flame_threshold

    # Map blended score to a level, then apply hard overrides.
    level = _level_for_score(base_score, t)
    if smoke is not None and smoke >= t.smoke_hard_warning:
        level = _max_level(level, "warning")
        reasons.append(f"smoke spike ≥ {t.smoke_hard_warning:g}")
    if flame:
        level = "critical"
        reasons.insert(0, "flame detected")

    if not reasons and level == "ok":
        reasons.append("all signals nominal")

    return RiskResult(
        score=base_score,
        level=level,
        components=components,
        reasons=reasons,
        flame=flame,
    )


def _level_for_score(s: float, t: RiskThresholds) -> str:
    if s >= t.level_critical:
        return "critical"
    if s >= t.level_warning:
        return "warning"
    if s >= t.level_elevated:
        return "elevated"
    return "ok"


def _max_level(a: str, b: str) -> str:
    return a if LEVEL_ORDER.get(a, 0) >= LEVEL_ORDER.get(b, 0) else b
