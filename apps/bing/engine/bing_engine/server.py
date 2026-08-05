"""HTTP server: a JSON REST API + the Bing web dashboard.

Built on the standard library's ``ThreadingHTTPServer`` — no Flask/FastAPI — so
it runs anywhere.  The same API is consumed by the Bing mobile app when it runs
in "engine mode", giving phones access to full ARP-based scans they can't do in
their own sandbox.

Endpoints (all JSON unless noted):

    GET  /                     the dashboard (static SPA)
    GET  /api/net              local + public network info
    GET  /api/scan             discover devices (blocking)
    GET  /api/scan/stream      discover devices (Server-Sent Events, live)
    GET  /api/device?ip=       deep single-device scan (ports+security+dns)
    GET  /api/ports?host=      port scan
    GET  /api/ping?host=       latency
    GET  /api/trace?host=      traceroute
    GET  /api/dns?name=        DNS lookup
    GET  /api/speed            internet speed test
    GET  /api/vendor?mac=      MAC vendor lookup
    POST /api/wol   {mac}      Wake-on-LAN
"""

from __future__ import annotations

import json
import os
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable, Dict
from urllib.parse import parse_qs, urlparse

from . import __version__, discovery, dnsr, netinfo, oui, ports, security, wol
from . import ping as ping_mod
from . import speedtest as speedtest_mod
from . import traceroute as traceroute_mod

_WEB_DIR = os.path.join(os.path.dirname(__file__), "web")

_CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
    ".json": "application/json",
    ".ico": "image/x-icon",
}


class Handler(BaseHTTPRequestHandler):
    server_version = f"Bing/{__version__}"

    # -- helpers ----------------------------------------------------------- #
    def _send_json(self, obj, status: int = 200) -> None:
        body = json.dumps(obj, default=lambda o: getattr(o, "to_dict", lambda: str(o))()
                          ).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_static(self, path: str) -> None:
        # Normalise and confine to the web dir.
        rel = path.lstrip("/") or "index.html"
        full = os.path.normpath(os.path.join(_WEB_DIR, rel))
        if not full.startswith(_WEB_DIR) or not os.path.isfile(full):
            self._send_json({"error": "not found"}, 404)
            return
        ext = os.path.splitext(full)[1]
        ctype = _CONTENT_TYPES.get(ext, "application/octet-stream")
        with open(full, "rb") as fh:
            body = fh.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _query(self) -> Dict[str, str]:
        qs = parse_qs(urlparse(self.path).query)
        return {k: v[0] for k, v in qs.items()}

    def log_message(self, fmt, *args):  # quieter logging
        if os.environ.get("BING_VERBOSE"):
            super().log_message(fmt, *args)

    # -- routing ----------------------------------------------------------- #
    def do_GET(self):  # noqa: N802
        route = urlparse(self.path).path
        try:
            if route == "/api/net":
                return self._api_net()
            if route == "/api/scan":
                return self._api_scan()
            if route == "/api/scan/stream":
                return self._api_scan_stream()
            if route == "/api/device":
                return self._api_device()
            if route == "/api/ports":
                return self._api_ports()
            if route == "/api/ping":
                return self._api_ping()
            if route == "/api/trace":
                return self._api_trace()
            if route == "/api/dns":
                return self._api_dns()
            if route == "/api/speed":
                return self._api_speed()
            if route == "/api/vendor":
                return self._api_vendor()
            if route == "/api/wol":
                return self._api_wol()
            if route.startswith("/api/"):
                return self._send_json({"error": "unknown endpoint"}, 404)
            return self._send_static(route)
        except BrokenPipeError:
            pass
        except Exception as exc:  # never crash a worker thread
            self._send_json({"error": str(exc)}, 500)

    def do_POST(self):  # noqa: N802
        route = urlparse(self.path).path
        try:
            if route == "/api/wol":
                return self._api_wol()
            return self._send_json({"error": "unknown endpoint"}, 404)
        except Exception as exc:
            self._send_json({"error": str(exc)}, 500)

    def do_OPTIONS(self):  # noqa: N802 — CORS preflight
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    # -- endpoints --------------------------------------------------------- #
    def _api_net(self):
        q = self._query()
        info = netinfo.gather(include_public=q.get("public", "1") != "0")
        self._send_json(info.to_dict())

    def _api_scan(self):
        q = self._query()
        devices = discovery.scan(
            cidr=q.get("cidr"),
            timeout=float(q.get("timeout", 0.4)),
            with_ports=q.get("ports", "1") != "0",
            resolve_names=q.get("resolve", "1") != "0",
        )
        self._send_json({"count": len(devices),
                         "devices": [d.to_dict() for d in devices]})

    def _api_scan_stream(self):
        """Server-Sent Events: emit each device as it is discovered."""
        q = self._query()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        lock = threading.Lock()

        def emit(event: str, data) -> None:
            payload = json.dumps(data, default=lambda o: o.to_dict())
            with lock:
                try:
                    self.wfile.write(f"event: {event}\ndata: {payload}\n\n".encode())
                    self.wfile.flush()
                except (BrokenPipeError, OSError):
                    raise

        try:
            emit("start", {"cidr": q.get("cidr") or "auto"})
            devices = discovery.scan(
                cidr=q.get("cidr"),
                timeout=float(q.get("timeout", 0.4)),
                with_ports=q.get("ports", "1") != "0",
                resolve_names=q.get("resolve", "1") != "0",
                on_device=lambda d: emit("device", d),
            )
            emit("done", {"count": len(devices)})
        except (BrokenPipeError, OSError):
            return
        except Exception as exc:
            try:
                emit("error", {"message": str(exc)})
            except OSError:
                pass

    def _api_device(self):
        q = self._query()
        ip = q.get("ip")
        if not ip:
            return self._send_json({"error": "ip required"}, 400)
        port_list = ports.parse_port_spec(q.get("ports", "top100"))
        open_ports = ports.scan(ip, ports=port_list, timeout=float(q.get("timeout", 1.0)))
        report = security.assess(ip, [p.port for p in open_ports])
        self._send_json({
            "ip": ip,
            "hostname": dnsr.reverse(ip),
            "open_ports": [p.to_dict() for p in open_ports],
            "security": report.to_dict(),
        })

    def _api_ports(self):
        q = self._query()
        host = q.get("host")
        if not host:
            return self._send_json({"error": "host required"}, 400)
        port_list = ports.parse_port_spec(q.get("ports", "common"))
        results = ports.scan(host, ports=port_list, timeout=float(q.get("timeout", 1.0)),
                             banner=q.get("banner", "1") != "0")
        report = security.assess(host, [r.port for r in results])
        self._send_json({"host": host, "open": [r.to_dict() for r in results],
                         "security": report.to_dict()})

    def _api_ping(self):
        q = self._query()
        host = q.get("host")
        if not host:
            return self._send_json({"error": "host required"}, 400)
        stats = ping_mod.ping(host, count=int(q.get("count", 4)),
                              port=int(q.get("port", 443)),
                              force_tcp=q.get("tcp", "0") == "1")
        self._send_json(stats.to_dict())

    def _api_trace(self):
        q = self._query()
        host = q.get("host")
        if not host:
            return self._send_json({"error": "host required"}, 400)
        result = traceroute_mod.trace(host, max_hops=int(q.get("max_hops", 30)))
        self._send_json(result.to_dict())

    def _api_dns(self):
        q = self._query()
        name = q.get("name")
        if not name:
            return self._send_json({"error": "name required"}, 400)
        if q.get("reverse") == "1":
            return self._send_json({"ip": name, "hostname": dnsr.reverse(name)})
        types = q.get("type", "A").split(",")
        records = []
        errors = []
        for t in types:
            try:
                records.extend(dnsr.query(name, t.strip().upper(), server=q.get("server")))
            except dnsr.DNSError as exc:
                errors.append(f"{t}: {exc}")
        self._send_json({"name": name, "records": [r.to_dict() for r in records],
                         "errors": errors})

    def _api_speed(self):
        q = self._query()
        result = speedtest_mod.run(quick=q.get("quick", "0") == "1")
        self._send_json(result.to_dict())

    def _api_vendor(self):
        q = self._query()
        mac = q.get("mac", "")
        self._send_json({"mac": oui.normalize(mac), "vendor": oui.lookup(mac)})

    def _api_wol(self):
        # Accept mac via query or JSON body.
        mac = self._query().get("mac")
        if mac is None and self.command == "POST":
            length = int(self.headers.get("Content-Length", 0))
            if length:
                try:
                    mac = json.loads(self.rfile.read(length)).get("mac")
                except (ValueError, AttributeError):
                    mac = None
        if not mac:
            return self._send_json({"error": "mac required"}, 400)
        try:
            wol.wake(mac)
        except ValueError as exc:
            return self._send_json({"error": str(exc)}, 400)
        self._send_json({"mac": oui.normalize(mac), "sent": True})


def serve(host: str = "127.0.0.1", port: int = 8787, open_browser: bool = False) -> None:
    httpd = ThreadingHTTPServer((host, port), Handler)
    url = f"http://{host if host != '0.0.0.0' else 'localhost'}:{port}/"
    print(f"Bing dashboard running at {url}")
    print("Press Ctrl+C to stop.")
    if open_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down…")
    finally:
        httpd.server_close()
