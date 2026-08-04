"""Cloud (AWS) forwarder for redundancy / failover mode.

When ``FG_MODE=redundancy`` and ``FG_CLOUD_URL`` is set, every telemetry reading
and every alert event is also written to a local ``outbox`` table and mirrored
to the cloud endpoint by this forwarder. Because the outbox is on the SSD, the
Pi is *always* a complete local copy of the data: if AWS is unreachable, rows
queue up and drain automatically when it returns. That is what makes the Pi a
genuine redundancy tier rather than a fragile passthrough.

The cloud endpoint just needs to accept a POST of
``{"source": "...", "items": [ {kind, payload}, ... ]}`` with an ``X-API-Key``
header — e.g. an API Gateway route into DynamoDB/Timestream, or an IoT ingest
Lambda. See docs/ARCHITECTURE.md.
"""

from __future__ import annotations

import json
import logging
import urllib.request

from .config import Config
from .db import Database

log = logging.getLogger("forestguard.cloud")

_BACKOFF_BASE_S = 5.0
_BACKOFF_CAP_S = 300.0


class CloudForwarder:
    def __init__(self, cfg: Config, db: Database):
        self.cfg = cfg
        self.db = db
        self.last_success: float | None = None
        self.last_error: str | None = None

    def enqueue_reading(self, payload: dict) -> None:
        if self.cfg.cloud_enabled:
            self.db.enqueue_outbox("telemetry", payload)

    def enqueue_event(self, payload: dict) -> None:
        if self.cfg.cloud_enabled:
            self.db.enqueue_outbox("event", payload)

    def drain_once(self, now: float) -> int:
        """Send one batch of due outbox rows. Returns number of rows sent."""
        if not self.cfg.cloud_enabled:
            return 0
        rows = self.db.due_outbox(self.cfg.cloud_batch_size, now=now)
        if not rows:
            return 0
        items = [{"kind": r["kind"], "payload": json.loads(r["payload"])} for r in rows]
        ok, err = self._post(items)
        if ok:
            self.db.mark_outbox_sent(r["id"] for r in rows)
            self.last_success = now
            self.last_error = None
            return len(rows)

        # Failure: back off each row; give up permanently past max attempts.
        self.last_error = err
        for r in rows:
            attempts = r["attempts"] + 1
            if self.cfg.cloud_max_attempts and attempts >= self.cfg.cloud_max_attempts:
                log.error("outbox row %s exceeded max attempts, dropping", r["id"])
                self.db.drop_outbox(r["id"])
            else:
                delay = min(_BACKOFF_CAP_S, _BACKOFF_BASE_S * (2 ** min(attempts, 12)))
                self.db.mark_outbox_failed(r["id"], now + delay)
        return 0

    def status(self) -> dict:
        stats = self.db.outbox_stats()
        return {
            "enabled": self.cfg.cloud_enabled,
            "mode": self.cfg.mode,
            "url_set": bool(self.cfg.cloud_url),
            "pending": stats["pending"],
            "sent": stats["sent"],
            "last_success": self.last_success,
            "last_error": self.last_error,
        }

    # -- internals ------------------------------------------------------
    def _post(self, items: list[dict]) -> tuple[bool, str | None]:
        body = json.dumps({"source": "forestguard-pi", "items": items}).encode()
        headers = {"Content-Type": "application/json"}
        if self.cfg.cloud_key:
            headers["X-API-Key"] = self.cfg.cloud_key
        req = urllib.request.Request(
            self.cfg.cloud_url, data=body, headers=headers, method="POST"
        )
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                if 200 <= resp.status < 300:
                    return True, None
                return False, f"HTTP {resp.status}"
        except Exception as exc:  # noqa: BLE001
            return False, str(exc)
