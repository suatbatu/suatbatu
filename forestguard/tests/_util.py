"""Shared test helpers."""

from __future__ import annotations

from app.config import Config, RiskThresholds
from app.db import Database


def make_config(**overrides) -> Config:
    base = dict(
        host="127.0.0.1",
        port=0,
        db_path=":memory:",
        offline_after_s=300,
        monitor_interval_s=999,
        notify_throttle_s=0,
        retention_days=0,
        thresholds=RiskThresholds(),
    )
    base.update(overrides)
    return Config(**base)


class FakeNotifier:
    """Captures alerts instead of sending them."""

    def __init__(self):
        self.sent: list[dict] = []
        self._started = False

    def start(self):
        self._started = True

    def stop(self):
        self._started = False

    def send(self, level, title, body="", data=None):
        self.sent.append({"level": level, "title": title, "data": data or {}})
