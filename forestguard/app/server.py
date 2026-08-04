"""HTTP server, REST API, and background monitor for ForestGuard.

Built on ``http.server.ThreadingHTTPServer`` — one thread per request, a tiny
path router, JSON in/out, and static serving of the dashboard from ``web/``.
No web framework, so there is nothing to install on the Pi beyond Python 3.
"""

from __future__ import annotations

import json
import logging
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from . import __version__
from .cloud import CloudForwarder
from .config import Config, load_config
from .db import Database
from .models import ValidationError, parse_payload
from .notify import Notifier
from .risk import level_rank, score
from .security import check_admin, check_ingest_key, read_allowed
from .service import Service

log = logging.getLogger("forestguard.server")

WEB_DIR = Path(__file__).resolve().parent.parent / "web"
_CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
    ".json": "application/json",
    ".ico": "image/x-icon",
}


class HttpError(Exception):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status
        self.message = message


class Request:
    def __init__(self, method: str, path: str, query: dict, headers, body: bytes):
        self.method = method
        self.path = path
        self.query = query
        self.headers = headers
        self.body = body

    def json(self):
        if not self.body:
            raise HttpError(400, "empty request body")
        try:
            return json.loads(self.body.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            raise HttpError(400, f"invalid JSON: {exc}") from exc

    def header(self, name: str) -> str | None:
        return self.headers.get(name)


class ForestGuardApp:
    """Owns state and route handlers; the HTTP handler delegates to this."""

    def __init__(self, cfg: Config, db: Database | None = None):
        self.cfg = cfg
        self.db = db or Database(cfg.db_path)
        self.notifier = Notifier(cfg)
        self.cloud = CloudForwarder(cfg, self.db)
        self.service = Service(cfg, self.db, self.notifier, self.cloud)
        self._routes: list[tuple[str, list[str], object]] = []
        self._monitor_stop = threading.Event()
        self._monitor_thread: threading.Thread | None = None
        self._register_routes()

    # -- lifecycle ------------------------------------------------------
    def start_background(self) -> None:
        self.notifier.start()
        if self._monitor_thread is None:
            self._monitor_thread = threading.Thread(
                target=self._monitor_loop, name="fg-monitor", daemon=True
            )
            self._monitor_thread.start()

    def stop_background(self) -> None:
        self._monitor_stop.set()
        self.notifier.stop()

    def _monitor_loop(self) -> None:
        while not self._monitor_stop.wait(self.cfg.monitor_interval_s):
            try:
                self.service.monitor_tick()
            except Exception:  # noqa: BLE001
                log.exception("monitor tick failed")

    # -- routing --------------------------------------------------------
    def _register_routes(self) -> None:
        r = self._add_route
        r("GET", "/healthz", self.h_health)
        r("GET", "/api/v1/config", self.h_config)
        r("GET", "/api/v1/summary", self.h_summary)
        r("GET", "/api/v1/nodes", self.h_nodes)
        r("GET", "/api/v1/nodes/{id}", self.h_node)
        r("GET", "/api/v1/nodes/{id}/history", self.h_history)
        r("POST", "/api/v1/nodes/{id}", self.h_update_node)
        r("GET", "/api/v1/events", self.h_events)
        r("GET", "/api/v1/cloud/status", self.h_cloud_status)
        r("POST", "/api/v1/telemetry", self.h_telemetry)
        r("POST", "/api/v1/monitor/tick", self.h_monitor_tick)

    def _add_route(self, method: str, pattern: str, handler) -> None:
        self._routes.append((method, [p for p in pattern.split("/") if p != ""], handler))

    def dispatch(self, req: Request):
        segs = [p for p in req.path.split("/") if p != ""]
        for method, pattern, handler in self._routes:
            if method != req.method:
                continue
            params = _match(pattern, segs)
            if params is not None:
                return handler(req, params)
        # Fall through to static file serving for GET.
        if req.method == "GET":
            return self._serve_static(req.path)
        raise HttpError(404, "not found")

    # -- auth guards ----------------------------------------------------
    def _require_read(self, req: Request) -> None:
        if not read_allowed(self.cfg, req.header("Authorization")):
            raise HttpError(401, "read access requires admin token")

    def _require_admin(self, req: Request) -> None:
        if not check_admin(self.cfg, req.header("Authorization")):
            raise HttpError(401, "admin token required")

    # -- handlers -------------------------------------------------------
    def h_health(self, req: Request, params: dict):
        return 200, {"status": "ok", "version": __version__, "time": time.time()}

    def h_config(self, req: Request, params: dict):
        self._require_read(req)
        return 200, {"version": __version__, **self.cfg.redacted()}

    def h_telemetry(self, req: Request, params: dict):
        if not check_ingest_key(self.cfg, req.header("X-API-Key")):
            raise HttpError(401, "invalid or missing X-API-Key")
        try:
            readings = parse_payload(req.json())
        except ValidationError as exc:
            raise HttpError(422, str(exc)) from exc
        results = [self.service.ingest(rd) for rd in readings]
        worst = _worst_level([r["risk"]["level"] for r in results])
        return 202, {"accepted": len(results), "worst_level": worst, "results": results}

    def h_nodes(self, req: Request, params: dict):
        self._require_read(req)
        now = time.time()
        return 200, {"nodes": [self._node_view(n, now) for n in self.db.list_nodes()]}

    def h_node(self, req: Request, params: dict):
        self._require_read(req)
        node = self.db.get_node(params["id"])
        if not node:
            raise HttpError(404, "node not found")
        view = self._node_view(node, time.time())
        view["metrics_available"] = self.db.metrics_for_node(node["id"])
        return 200, view

    def h_history(self, req: Request, params: dict):
        self._require_read(req)
        node = self.db.get_node(params["id"])
        if not node:
            raise HttpError(404, "node not found")
        metric = _one(req.query.get("metric"))
        if not metric:
            raise HttpError(400, "?metric= is required")
        since = _parse_since(req.query.get("since"))
        limit = _parse_int(req.query.get("limit"), default=2000, lo=1, hi=20000)
        points = self.db.history(node["id"], metric, since=since, limit=limit)
        return 200, {"node": node["id"], "metric": metric, "points": points}

    def h_update_node(self, req: Request, params: dict):
        self._require_admin(req)
        node = self.db.get_node(params["id"])
        if not node:
            raise HttpError(404, "node not found")
        body = req.json()
        if not isinstance(body, dict):
            raise HttpError(400, "expected a JSON object")
        fields = {k: body[k] for k in ("name", "lat", "lon", "meta") if k in body}
        if not fields:
            raise HttpError(400, "no updatable fields (name, lat, lon, meta)")
        self.db.update_node_fields(node["id"], **fields)
        return 200, self._node_view(self.db.get_node(node["id"]), time.time())

    def h_events(self, req: Request, params: dict):
        self._require_read(req)
        since = _parse_since(req.query.get("since"))
        limit = _parse_int(req.query.get("limit"), default=100, lo=1, hi=1000)
        return 200, {"events": self.db.list_events(since=since, limit=limit)}

    def h_cloud_status(self, req: Request, params: dict):
        self._require_read(req)
        return 200, self.cloud.status()

    def h_monitor_tick(self, req: Request, params: dict):
        self._require_admin(req)
        return 200, self.service.monitor_tick()

    def h_summary(self, req: Request, params: dict):
        self._require_read(req)
        now = time.time()
        views = [self._node_view(n, now) for n in self.db.list_nodes()]
        counts: dict[str, int] = {}
        for v in views:
            counts[v["level"]] = counts.get(v["level"], 0) + 1
        online = sum(1 for v in views if v["online"])
        alerts = [v for v in views if v["level"] in ("warning", "critical", "offline")]
        alerts.sort(key=lambda v: level_rank(v["level"]), reverse=True)
        worst = _worst_level([v["level"] for v in views]) if views else "ok"
        return 200, {
            "time": now,
            "node_count": len(views),
            "online": online,
            "offline": len(views) - online,
            "worst_level": worst,
            "counts": counts,
            "active_alerts": alerts,
            "recent_events": self.db.list_events(limit=8),
            "cloud": self.cloud.status(),
        }

    # -- views ----------------------------------------------------------
    def _node_view(self, node: dict, now: float) -> dict:
        last_seen = node.get("last_seen") or 0
        age = now - last_seen if last_seen else None
        online = age is not None and age <= self.cfg.offline_after_s
        metrics = self.db.latest_metrics(node["id"])
        result = score(metrics, self.cfg.thresholds)
        # Effective level: offline overrides the stored risk level in the view.
        level = node["last_level"]
        reasons = result.reasons
        if not online:
            level = "offline"
            reasons = ["no recent telemetry"]
        elif level in ("unknown", "offline"):
            # Recompute if the stored value is stale relative to latest metrics.
            level = result.level
        return {
            "id": node["id"],
            "name": node.get("name") or node["id"],
            "lat": node.get("lat"),
            "lon": node.get("lon"),
            "last_seen": last_seen or None,
            "age_s": round(age, 1) if age is not None else None,
            "online": online,
            "level": level,
            "score": round(result.score, 1),
            "metrics": metrics,
            "reasons": reasons,
            "flame": result.flame,
        }

    # -- static ---------------------------------------------------------
    def _serve_static(self, path: str):
        rel = "index.html" if path in ("/", "") else path.lstrip("/")
        target = (WEB_DIR / rel).resolve()
        try:
            target.relative_to(WEB_DIR.resolve())
        except ValueError:
            raise HttpError(403, "forbidden")
        if not target.is_file():
            raise HttpError(404, "not found")
        ctype = _CONTENT_TYPES.get(target.suffix, "application/octet-stream")
        return 200, target.read_bytes(), ctype


# -- routing helpers ----------------------------------------------------
def _match(pattern: list[str], segs: list[str]) -> dict | None:
    if len(pattern) != len(segs):
        return None
    params: dict[str, str] = {}
    for pat, seg in zip(pattern, segs):
        if pat.startswith("{") and pat.endswith("}"):
            params[pat[1:-1]] = urllib.parse.unquote(seg)
        elif pat != seg:
            return None
    return params


def _one(values):
    if not values:
        return None
    return values[0] if isinstance(values, list) else values


def _parse_int(values, default: int, lo: int, hi: int) -> int:
    raw = _one(values)
    try:
        n = int(raw) if raw is not None else default
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, n))


def _parse_since(values) -> float | None:
    """Accept an absolute epoch, or a relative ``-3600`` / ``3600s`` window."""
    raw = _one(values)
    if raw is None:
        return None
    raw = str(raw).strip()
    try:
        if raw.endswith("s"):
            return time.time() - float(raw[:-1])
        val = float(raw)
    except ValueError:
        return None
    if val < 0:
        return time.time() + val  # e.g. -3600 -> last hour
    if val < 1e6:
        return time.time() - val  # small positive -> "last N seconds"
    return val                    # large -> absolute epoch


def _worst_level(levels) -> str:
    worst = "ok"
    for lv in levels:
        if level_rank(lv) > level_rank(worst):
            worst = lv
    return worst


# -- HTTP glue ----------------------------------------------------------
class _Handler(BaseHTTPRequestHandler):
    server_version = f"ForestGuard/{__version__}"
    protocol_version = "HTTP/1.1"

    @property
    def app(self) -> ForestGuardApp:
        return self.server.app  # type: ignore[attr-defined]

    def log_message(self, fmt, *args):  # quieter default logging
        log.debug("%s - %s", self.address_string(), fmt % args)

    def _handle(self, method: str) -> None:
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        body = b""
        length = int(self.headers.get("Content-Length") or 0)
        if length > 0:
            if length > 5_000_000:
                self._send(413, {"error": "payload too large"})
                return
            body = self.rfile.read(length)
        req = Request(method, parsed.path, query, self.headers, body)
        try:
            result = self.app.dispatch(req)
            self._send_result(result)
        except HttpError as exc:
            self._send(exc.status, {"error": exc.message})
        except Exception:  # noqa: BLE001
            log.exception("unhandled error handling %s %s", method, parsed.path)
            self._send(500, {"error": "internal server error"})

    def _send_result(self, result) -> None:
        if isinstance(result, tuple) and len(result) == 3:
            status, body, ctype = result
            self._send_raw(status, body if isinstance(body, bytes) else str(body).encode(),
                           ctype)
        else:
            status, body = result
            self._send(status, body)

    def _send(self, status: int, obj) -> None:
        self._send_raw(status, json.dumps(obj).encode(), "application/json")

    def _send_raw(self, status: int, body: bytes, ctype: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        self._handle("GET")

    def do_POST(self):
        self._handle("POST")

    def do_HEAD(self):
        self._handle("GET")


def create_server(cfg: Config | None = None, app: ForestGuardApp | None = None) -> tuple:
    cfg = cfg or load_config()
    app = app or ForestGuardApp(cfg)
    httpd = ThreadingHTTPServer((cfg.host, cfg.port), _Handler)
    httpd.app = app  # type: ignore[attr-defined]
    httpd.daemon_threads = True
    return httpd, app
