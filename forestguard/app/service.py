"""The ForestGuard engine: ingestion, risk evaluation, the alert state machine,
offline detection, retention, and cloud draining. All request handlers and the
background monitor call into this one object so the rules live in a single place.
"""

from __future__ import annotations

import logging
import time

from .cloud import CloudForwarder
from .config import Config
from .db import Database
from .models import Reading
from .notify import Notifier
from .risk import RiskResult, level_rank, score

log = logging.getLogger("forestguard.service")

# Levels that warrant pushing a notification out (vs. just logging an event).
_NOTIFY_LEVELS = {"warning", "critical", "offline"}


class Service:
    def __init__(self, cfg: Config, db: Database, notifier: Notifier,
                 cloud: CloudForwarder):
        self.cfg = cfg
        self.db = db
        self.notifier = notifier
        self.cloud = cloud

    # -- ingestion ------------------------------------------------------
    def ingest(self, reading: Reading) -> dict:
        """Store one reading, re-evaluate the node, fire alerts, mirror to cloud."""
        prev = self.db.get_node(reading.node)
        prev_level = prev["last_level"] if prev else "unknown"

        self.db.upsert_node_seen(
            reading.node, reading.ts, reading.lat, reading.lon, reading.name
        )
        stored = self.db.insert_readings(reading.node, reading.ts, reading.metrics)

        # A node that was offline just reported in. Announce recovery, then
        # evaluate its new risk from a neutral baseline so we don't double-fire
        # both an "online" and a "cleared to OK" notification for the same event.
        if prev_level == "offline":
            self._emit(
                node_id=reading.node, level="online", type_="online",
                message=f"Node {self._nname(reading.node)} is back online",
                ts=reading.ts,
            )
            prev_level = "ok"

        latest = self.db.latest_metrics(reading.node)
        result = score(latest, self.cfg.thresholds)
        self._apply_result(reading.node, prev_level, result, reading.ts)

        self.cloud.enqueue_reading({
            "node": reading.node, "ts": reading.ts, "metrics": reading.metrics,
            "lat": reading.lat, "lon": reading.lon,
        })

        return {
            "node": reading.node,
            "stored_metrics": stored,
            "risk": result.to_dict(),
        }

    # -- evaluation / alerting -----------------------------------------
    def _apply_result(self, node_id: str, prev_level: str, result: RiskResult,
                      ts: float) -> None:
        self.db.set_node_level(node_id, result.level, result.score)
        if result.level == prev_level:
            return  # no state change, nothing to announce

        # An event row for every transition (both directions) — this is the log.
        going_up = level_rank(result.level) > level_rank(prev_level)
        type_ = "flame" if result.flame else "risk_change"
        verb = "rose to" if going_up else "cleared to"
        msg = (f"{self._nname(node_id)} {verb} {result.level.upper()} "
               f"(score {result.score:.0f}): {', '.join(result.reasons)}")
        self._emit(
            node_id=node_id, level=result.level, type_=type_, message=msg, ts=ts,
            data={"score": round(result.score, 1), "reasons": result.reasons,
                  "components": result.components, "prev_level": prev_level},
            notify=self._should_notify(node_id, prev_level, result.level),
        )

    def _should_notify(self, node_id: str, prev_level: str, new_level: str) -> bool:
        going_up = level_rank(new_level) > level_rank(prev_level)
        # Escalations into a notify-worthy level, and recoveries back to ok.
        if going_up and new_level in _NOTIFY_LEVELS:
            return not self._throttled(node_id, new_level)
        if not going_up and new_level == "ok" and prev_level in _NOTIFY_LEVELS:
            return True
        return False

    def _throttled(self, node_id: str, level: str) -> bool:
        node = self.db.get_node(node_id)
        if not node:
            return False
        if node.get("last_notified_level") != level:
            return False
        last = node.get("last_notified_at") or 0
        return (time.time() - last) < self.cfg.notify_throttle_s

    # -- monitor tick ---------------------------------------------------
    def monitor_tick(self, now: float | None = None) -> dict:
        now = now if now is not None else time.time()
        offline = self._sweep_offline(now)
        drained = self.cloud.drain_once(now)
        pruned = self._prune(now)
        return {"offline_flagged": offline, "cloud_drained": drained, "pruned": pruned}

    def _sweep_offline(self, now: float) -> int:
        flagged = 0
        for node in self.db.list_nodes():
            last_seen = node.get("last_seen") or 0
            if node["last_level"] == "offline":
                continue
            if node["last_level"] == "unknown" and not last_seen:
                continue
            if now - last_seen > self.cfg.offline_after_s:
                self.db.set_node_level(node["id"], "offline", node.get("last_score"))
                mins = int((now - last_seen) / 60)
                self._emit(
                    node_id=node["id"], level="offline", type_="offline",
                    message=f"{self._nname(node['id'])} went offline "
                            f"(no data for {mins} min)",
                    ts=now, notify=not self._throttled(node["id"], "offline"),
                )
                flagged += 1
        return flagged

    def _prune(self, now: float) -> int:
        if self.cfg.retention_days <= 0:
            return 0
        cutoff = now - self.cfg.retention_days * 86400
        return self.db.prune_readings(cutoff)

    # -- helpers --------------------------------------------------------
    def _emit(self, node_id: str | None, level: str, type_: str, message: str,
              ts: float, data: dict | None = None, notify: bool = True) -> None:
        event_id = self.db.add_event(level, type_, message, node_id=node_id,
                                     data=data, ts=ts)
        self.cloud.enqueue_event({
            "event_id": event_id, "ts": ts, "node": node_id,
            "level": level, "type": type_, "message": message, "data": data or {},
        })
        # Whether to notify is decided by the caller (_should_notify / offline
        # sweep). _emit just honors that decision — it must not second-guess it
        # by level, or recovery ("ok") notifications would be swallowed.
        if notify:
            self.notifier.send(level, message, data=data or {})
            if node_id:
                self.db.mark_notified(node_id, level, ts)
        log.info("event[%s] %s: %s", type_, level, message)

    def _nname(self, node_id: str) -> str:
        node = self.db.get_node(node_id)
        name = node.get("name") if node else None
        return name or node_id
