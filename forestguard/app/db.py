"""SQLite storage layer for ForestGuard.

Uses only the stdlib ``sqlite3`` module. The database lives on the SSD; WAL mode
gives durable, concurrent reads while the single writer thread appends. All
access goes through one connection guarded by a lock — at forest-network scale
(a handful to a few hundred nodes) this is more than fast enough and avoids
per-thread connection juggling.
"""

from __future__ import annotations

import json
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any, Iterable

SCHEMA = """
CREATE TABLE IF NOT EXISTS nodes (
    id          TEXT PRIMARY KEY,
    name        TEXT,
    lat         REAL,
    lon         REAL,
    meta        TEXT,                       -- JSON blob
    created_at  REAL NOT NULL,
    last_seen   REAL,
    last_level  TEXT NOT NULL DEFAULT 'unknown',
    last_score  REAL,
    last_notified_level TEXT,
    last_notified_at    REAL
);

CREATE TABLE IF NOT EXISTS readings (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id  TEXT NOT NULL,
    ts       REAL NOT NULL,
    metric   TEXT NOT NULL,
    value    REAL NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_readings_node_ts ON readings(node_id, ts);
CREATE INDEX IF NOT EXISTS idx_readings_node_metric_ts ON readings(node_id, metric, ts);

CREATE TABLE IF NOT EXISTS events (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    ts       REAL NOT NULL,
    node_id  TEXT,
    level    TEXT NOT NULL,
    type     TEXT NOT NULL,                 -- risk_change | offline | online | flame | info
    message  TEXT NOT NULL,
    data     TEXT                           -- JSON blob
);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);

-- Store-and-forward queue for cloud (AWS) redundancy mode.
CREATE TABLE IF NOT EXISTS outbox (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at   REAL NOT NULL,
    kind         TEXT NOT NULL,             -- telemetry | event
    payload      TEXT NOT NULL,             -- JSON blob
    attempts     INTEGER NOT NULL DEFAULT 0,
    next_attempt REAL NOT NULL DEFAULT 0,
    sent_at      REAL
);
CREATE INDEX IF NOT EXISTS idx_outbox_pending ON outbox(sent_at, next_attempt);

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT
);
"""


def _now() -> float:
    return time.time()


class Database:
    def __init__(self, path: str):
        self.path = path
        if path != ":memory:":
            Path(path).parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self._conn = sqlite3.connect(path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        with self._lock:
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.execute("PRAGMA synchronous=NORMAL")
            self._conn.execute("PRAGMA foreign_keys=ON")
            self._conn.executescript(SCHEMA)
            self._conn.commit()

    def close(self) -> None:
        with self._lock:
            self._conn.close()

    # -- nodes ----------------------------------------------------------
    def upsert_node_seen(
        self, node_id: str, ts: float, lat: float | None, lon: float | None,
        name: str | None = None,
    ) -> None:
        with self._lock:
            row = self._conn.execute("SELECT id FROM nodes WHERE id=?", (node_id,)).fetchone()
            if row is None:
                self._conn.execute(
                    "INSERT INTO nodes(id, name, lat, lon, meta, created_at, last_seen) "
                    "VALUES(?,?,?,?,?,?,?)",
                    (node_id, name or node_id, lat, lon, "{}", ts, ts),
                )
            else:
                # Only overwrite lat/lon/name when a new value is supplied.
                sets = ["last_seen=?"]
                params: list[Any] = [ts]
                if lat is not None:
                    sets.append("lat=?"); params.append(lat)
                if lon is not None:
                    sets.append("lon=?"); params.append(lon)
                if name:
                    sets.append("name=?"); params.append(name)
                params.append(node_id)
                self._conn.execute(f"UPDATE nodes SET {', '.join(sets)} WHERE id=?", params)
            self._conn.commit()

    def update_node_fields(self, node_id: str, **fields: Any) -> bool:
        allowed = {"name", "lat", "lon", "meta"}
        sets, params = [], []
        for k, v in fields.items():
            if k not in allowed:
                continue
            if k == "meta" and not isinstance(v, str):
                v = json.dumps(v)
            sets.append(f"{k}=?"); params.append(v)
        if not sets:
            return False
        params.append(node_id)
        with self._lock:
            cur = self._conn.execute(f"UPDATE nodes SET {', '.join(sets)} WHERE id=?", params)
            self._conn.commit()
            return cur.rowcount > 0

    def set_node_level(self, node_id: str, level: str, score: float | None) -> None:
        with self._lock:
            self._conn.execute(
                "UPDATE nodes SET last_level=?, last_score=? WHERE id=?",
                (level, score, node_id),
            )
            self._conn.commit()

    def mark_notified(self, node_id: str, level: str, ts: float) -> None:
        with self._lock:
            self._conn.execute(
                "UPDATE nodes SET last_notified_level=?, last_notified_at=? WHERE id=?",
                (level, ts, node_id),
            )
            self._conn.commit()

    def get_node(self, node_id: str) -> dict | None:
        with self._lock:
            row = self._conn.execute("SELECT * FROM nodes WHERE id=?", (node_id,)).fetchone()
        return _node_row(row) if row else None

    def list_nodes(self) -> list[dict]:
        with self._lock:
            rows = self._conn.execute("SELECT * FROM nodes ORDER BY id").fetchall()
        return [_node_row(r) for r in rows]

    # -- readings -------------------------------------------------------
    def insert_readings(self, node_id: str, ts: float, metrics: dict[str, float]) -> int:
        rows = [(node_id, ts, m, float(v)) for m, v in metrics.items() if _is_number(v)]
        if not rows:
            return 0
        with self._lock:
            self._conn.executemany(
                "INSERT INTO readings(node_id, ts, metric, value) VALUES(?,?,?,?)", rows
            )
            self._conn.commit()
        return len(rows)

    def latest_metrics(self, node_id: str) -> dict[str, float]:
        """Most recent value per metric for a node."""
        with self._lock:
            rows = self._conn.execute(
                """
                SELECT metric, value FROM readings r
                WHERE node_id=? AND ts = (
                    SELECT MAX(ts) FROM readings WHERE node_id=r.node_id AND metric=r.metric
                )
                GROUP BY metric
                """,
                (node_id,),
            ).fetchall()
        return {r["metric"]: r["value"] for r in rows}

    def history(
        self, node_id: str, metric: str, since: float | None = None, limit: int = 2000
    ) -> list[dict]:
        q = "SELECT ts, value FROM readings WHERE node_id=? AND metric=?"
        params: list[Any] = [node_id, metric]
        if since is not None:
            q += " AND ts >= ?"; params.append(since)
        q += " ORDER BY ts DESC LIMIT ?"; params.append(limit)
        with self._lock:
            rows = self._conn.execute(q, params).fetchall()
        return [{"ts": r["ts"], "value": r["value"]} for r in reversed(rows)]

    def metrics_for_node(self, node_id: str) -> list[str]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT DISTINCT metric FROM readings WHERE node_id=? ORDER BY metric",
                (node_id,),
            ).fetchall()
        return [r["metric"] for r in rows]

    def prune_readings(self, older_than_ts: float) -> int:
        with self._lock:
            cur = self._conn.execute("DELETE FROM readings WHERE ts < ?", (older_than_ts,))
            self._conn.commit()
            return cur.rowcount

    # -- events ---------------------------------------------------------
    def add_event(
        self, level: str, type_: str, message: str,
        node_id: str | None = None, data: dict | None = None, ts: float | None = None,
    ) -> int:
        ts = ts if ts is not None else _now()
        with self._lock:
            cur = self._conn.execute(
                "INSERT INTO events(ts, node_id, level, type, message, data) VALUES(?,?,?,?,?,?)",
                (ts, node_id, level, type_, message, json.dumps(data or {})),
            )
            self._conn.commit()
            return cur.lastrowid

    def list_events(self, since: float | None = None, limit: int = 100) -> list[dict]:
        q = "SELECT * FROM events"
        params: list[Any] = []
        if since is not None:
            q += " WHERE ts >= ?"; params.append(since)
        q += " ORDER BY ts DESC LIMIT ?"; params.append(limit)
        with self._lock:
            rows = self._conn.execute(q, params).fetchall()
        return [_event_row(r) for r in rows]

    # -- outbox (cloud redundancy) --------------------------------------
    def enqueue_outbox(self, kind: str, payload: dict) -> int:
        with self._lock:
            cur = self._conn.execute(
                "INSERT INTO outbox(created_at, kind, payload) VALUES(?,?,?)",
                (_now(), kind, json.dumps(payload)),
            )
            self._conn.commit()
            return cur.lastrowid

    def due_outbox(self, limit: int, now: float | None = None) -> list[dict]:
        now = now if now is not None else _now()
        with self._lock:
            rows = self._conn.execute(
                "SELECT * FROM outbox WHERE sent_at IS NULL AND next_attempt<=? "
                "ORDER BY id LIMIT ?",
                (now, limit),
            ).fetchall()
        return [dict(r) for r in rows]

    def mark_outbox_sent(self, ids: Iterable[int]) -> None:
        ids = list(ids)
        if not ids:
            return
        now = _now()
        with self._lock:
            self._conn.executemany(
                "UPDATE outbox SET sent_at=? WHERE id=?", [(now, i) for i in ids]
            )
            self._conn.commit()

    def mark_outbox_failed(self, id_: int, next_attempt: float) -> None:
        with self._lock:
            self._conn.execute(
                "UPDATE outbox SET attempts=attempts+1, next_attempt=? WHERE id=?",
                (next_attempt, id_),
            )
            self._conn.commit()

    def drop_outbox(self, id_: int) -> None:
        with self._lock:
            self._conn.execute("UPDATE outbox SET sent_at=? WHERE id=?", (_now(), id_))
            self._conn.commit()

    def outbox_stats(self) -> dict:
        with self._lock:
            pending = self._conn.execute(
                "SELECT COUNT(*) c FROM outbox WHERE sent_at IS NULL"
            ).fetchone()["c"]
            sent = self._conn.execute(
                "SELECT COUNT(*) c FROM outbox WHERE sent_at IS NOT NULL"
            ).fetchone()["c"]
        return {"pending": pending, "sent": sent}


def _is_number(v: Any) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _node_row(row: sqlite3.Row) -> dict:
    d = dict(row)
    try:
        d["meta"] = json.loads(d.get("meta") or "{}")
    except (json.JSONDecodeError, TypeError):
        d["meta"] = {}
    return d


def _event_row(row: sqlite3.Row) -> dict:
    d = dict(row)
    try:
        d["data"] = json.loads(d.get("data") or "{}")
    except (json.JSONDecodeError, TypeError):
        d["data"] = {}
    return d
