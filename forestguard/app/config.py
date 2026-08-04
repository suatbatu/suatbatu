"""Configuration for the ForestGuard server.

All settings come from environment variables (12-factor style), with a small
built-in parser for an optional ``.env`` file so the systemd unit can point at
one file on the SSD. No external dependency (python-dotenv) is required.

Nothing secret is ever committed: see ``.env.example`` for the template.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path


def _load_dotenv(path: str | os.PathLike[str]) -> None:
    """Populate ``os.environ`` from a ``.env`` file without overriding values
    already present in the real environment. Intentionally tiny: ``KEY=value``
    lines, ``#`` comments, optional surrounding quotes."""
    p = Path(path)
    if not p.is_file():
        return
    for raw in p.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value


def _get(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _get_bool(name: str, default: bool = False) -> bool:
    raw = _get(name)
    if not raw:
        return default
    return raw.lower() in ("1", "true", "yes", "on")


def _get_int(name: str, default: int) -> int:
    raw = _get(name)
    try:
        return int(raw) if raw else default
    except ValueError:
        return default


def _get_float(name: str, default: float) -> float:
    raw = _get(name)
    try:
        return float(raw) if raw else default
    except ValueError:
        return default


def _get_list(name: str) -> list[str]:
    raw = _get(name)
    return [item.strip() for item in raw.split(",") if item.strip()]


@dataclass(frozen=True)
class RiskThresholds:
    """Tunable inputs to the (documented, heuristic) fire-risk model.

    See ``app/risk.py`` and ``docs/ARCHITECTURE.md``. These are NOT calibrated
    for any particular site; treat them as a starting point.
    """

    # Temperature ramp (deg C): risk from heat rises from 0 -> 1 across this band.
    temp_low_c: float = 25.0
    temp_high_c: float = 50.0
    # Relative humidity ramp (%): drier air -> higher risk (note reversed band).
    humidity_high_pct: float = 45.0
    humidity_low_pct: float = 15.0
    # Smoke / gas analog readings (raw ADC-style 0..1023 by default). Values at
    # or below *_baseline contribute nothing; at or above *_max they contribute 1.
    smoke_baseline: float = 120.0
    smoke_max: float = 600.0
    gas_baseline: float = 100.0
    gas_max: float = 500.0
    # A flame reading at/above this is treated as an active flame -> critical.
    flame_threshold: float = 1.0

    # Relative weights for the blended base score (flame is handled separately).
    weight_heat: float = 0.30
    weight_dryness: float = 0.20
    weight_smoke: float = 0.35
    weight_gas: float = 0.15

    # Level cut points on the 0..100 score.
    level_elevated: float = 25.0
    level_warning: float = 50.0
    level_critical: float = 75.0
    # Any smoke reading at/above this forces at least a "warning" regardless of
    # the blended score, so a smoke spike is never averaged away.
    smoke_hard_warning: float = 450.0


@dataclass(frozen=True)
class Config:
    # --- Server ---------------------------------------------------------
    host: str = "0.0.0.0"
    port: int = 8080

    # --- Storage --------------------------------------------------------
    # On the Pi this should live on the SSD, e.g. /mnt/ssd/forestguard/forestguard.db
    db_path: str = "forestguard.db"

    # --- Auth -----------------------------------------------------------
    # Sensor nodes authenticate ingestion with one of these keys (X-API-Key).
    ingest_keys: tuple[str, ...] = ()
    # Dashboard + query/control API require this bearer token.
    admin_token: str = ""
    # If true, GET (read) API + dashboard are open without a token (LAN-only use).
    dashboard_public: bool = False

    # --- Monitoring -----------------------------------------------------
    # A node with no telemetry within this many seconds is flagged offline.
    offline_after_s: int = 900
    # Background monitor cadence (offline sweep + cloud drain).
    monitor_interval_s: int = 30
    # Suppress repeat notifications for the same node+level within this window.
    notify_throttle_s: int = 300
    # How long to keep raw readings before pruning (0 = keep forever).
    retention_days: int = 90

    # --- Cloud (AWS) redundancy ----------------------------------------
    # Mode: "local" (no cloud) or "redundancy" (mirror to cloud via outbox).
    mode: str = "local"
    cloud_url: str = ""          # HTTPS endpoint (e.g. API Gateway / IoT ingest)
    cloud_key: str = ""          # sent as X-API-Key to the cloud endpoint
    cloud_batch_size: int = 50
    cloud_max_attempts: int = 0  # 0 = retry forever (store-and-forward)

    # --- Notifications --------------------------------------------------
    telegram_token: str = ""
    telegram_chat_id: str = ""
    webhook_url: str = ""        # generic JSON webhook for alerts

    thresholds: RiskThresholds = field(default_factory=RiskThresholds)

    @property
    def cloud_enabled(self) -> bool:
        return self.mode == "redundancy" and bool(self.cloud_url)

    def redacted(self) -> dict:
        """A dict safe to expose over the API / log (no secrets)."""
        return {
            "version_mode": self.mode,
            "cloud_enabled": self.cloud_enabled,
            "cloud_url_set": bool(self.cloud_url),
            "dashboard_public": self.dashboard_public,
            "offline_after_s": self.offline_after_s,
            "retention_days": self.retention_days,
            "telegram": bool(self.telegram_token and self.telegram_chat_id),
            "webhook": bool(self.webhook_url),
            "ingest_keys_configured": len(self.ingest_keys),
        }


def load_config(dotenv_path: str | None = None) -> Config:
    """Build a :class:`Config` from the environment (and an optional .env)."""
    if dotenv_path is None:
        dotenv_path = os.environ.get("FG_ENV_FILE", ".env")
    _load_dotenv(dotenv_path)

    thresholds = RiskThresholds(
        temp_low_c=_get_float("FG_TEMP_LOW_C", RiskThresholds.temp_low_c),
        temp_high_c=_get_float("FG_TEMP_HIGH_C", RiskThresholds.temp_high_c),
        humidity_high_pct=_get_float("FG_HUMIDITY_HIGH_PCT", RiskThresholds.humidity_high_pct),
        humidity_low_pct=_get_float("FG_HUMIDITY_LOW_PCT", RiskThresholds.humidity_low_pct),
        smoke_baseline=_get_float("FG_SMOKE_BASELINE", RiskThresholds.smoke_baseline),
        smoke_max=_get_float("FG_SMOKE_MAX", RiskThresholds.smoke_max),
        gas_baseline=_get_float("FG_GAS_BASELINE", RiskThresholds.gas_baseline),
        gas_max=_get_float("FG_GAS_MAX", RiskThresholds.gas_max),
        flame_threshold=_get_float("FG_FLAME_THRESHOLD", RiskThresholds.flame_threshold),
        smoke_hard_warning=_get_float("FG_SMOKE_HARD_WARNING", RiskThresholds.smoke_hard_warning),
    )

    return Config(
        host=_get("FG_HOST", "0.0.0.0"),
        port=_get_int("FG_PORT", 8080),
        db_path=_get("FG_DB_PATH", "forestguard.db"),
        ingest_keys=tuple(_get_list("FG_INGEST_KEYS")),
        admin_token=_get("FG_ADMIN_TOKEN"),
        dashboard_public=_get_bool("FG_DASHBOARD_PUBLIC", False),
        offline_after_s=_get_int("FG_OFFLINE_AFTER_S", 900),
        monitor_interval_s=_get_int("FG_MONITOR_INTERVAL_S", 30),
        notify_throttle_s=_get_int("FG_NOTIFY_THROTTLE_S", 300),
        retention_days=_get_int("FG_RETENTION_DAYS", 90),
        mode=_get("FG_MODE", "local").lower(),
        cloud_url=_get("FG_CLOUD_URL"),
        cloud_key=_get("FG_CLOUD_KEY"),
        cloud_batch_size=_get_int("FG_CLOUD_BATCH_SIZE", 50),
        cloud_max_attempts=_get_int("FG_CLOUD_MAX_ATTEMPTS", 0),
        telegram_token=_get("FG_TELEGRAM_TOKEN"),
        telegram_chat_id=_get("FG_TELEGRAM_CHAT_ID"),
        webhook_url=_get("FG_WEBHOOK_URL"),
        thresholds=thresholds,
    )
