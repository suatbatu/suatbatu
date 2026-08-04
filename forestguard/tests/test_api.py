import json
import threading
import time
import unittest
import urllib.error
import urllib.request

from app.server import create_server
from tests._util import make_config


def _request(port, method, path, token=None, key=None, body=None):
    url = f"http://127.0.0.1:{port}{path}"
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    if key:
        headers["X-API-Key"] = key
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status, resp.read(), resp.headers.get("Content-Type", "")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read(), exc.headers.get("Content-Type", "")


class TestApi(unittest.TestCase):
    def setUp(self):
        cfg = make_config(admin_token="secret", ingest_keys=("KEY1",),
                          dashboard_public=False)
        self.httpd, self.app = create_server(cfg)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.app.db.close()

    def get(self, path, **kw):
        status, body, ctype = _request(self.port, "GET", path, **kw)
        return status, body, ctype

    def get_json(self, path, **kw):
        status, body, _ = self.get(path, **kw)
        return status, json.loads(body) if body else None

    def post_json(self, path, body, **kw):
        status, raw, _ = _request(self.port, "POST", path, body=body, **kw)
        return status, json.loads(raw) if raw else None

    # -- tests ----------------------------------------------------------
    def test_health_is_open(self):
        status, obj = self.get_json("/healthz")
        self.assertEqual(status, 200)
        self.assertEqual(obj["status"], "ok")

    def test_telemetry_requires_key(self):
        status, _ = self.post_json("/api/v1/telemetry",
                                   {"node": "n1", "temp_c": 22})
        self.assertEqual(status, 401)

    def test_telemetry_accepts_with_key(self):
        status, obj = self.post_json(
            "/api/v1/telemetry",
            {"node": "ridge", "name": "Ridge East",
             "metrics": {"temp_c": 55, "humidity": 10, "smoke": 700, "gas": 600}},
            key="KEY1")
        self.assertEqual(status, 202)
        self.assertEqual(obj["accepted"], 1)
        self.assertEqual(obj["worst_level"], "critical")

    def test_reads_require_token(self):
        self.assertEqual(self.get_json("/api/v1/nodes")[0], 401)
        self.assertEqual(self.get_json("/api/v1/summary")[0], 401)

    def test_full_flow_with_token(self):
        self.post_json("/api/v1/telemetry",
                       {"node": "n1", "metrics": {"temp_c": 30, "humidity": 40}},
                       key="KEY1")
        status, obj = self.get_json("/api/v1/nodes", token="secret")
        self.assertEqual(status, 200)
        self.assertEqual(len(obj["nodes"]), 1)
        self.assertEqual(obj["nodes"][0]["id"], "n1")

        status, summ = self.get_json("/api/v1/summary", token="secret")
        self.assertEqual(status, 200)
        self.assertEqual(summ["node_count"], 1)
        self.assertEqual(summ["online"], 1)

    def test_history_endpoint(self):
        base = time.time() - 100
        for i in range(3):
            self.post_json("/api/v1/telemetry",
                           {"node": "n1", "ts": base + i,
                            "metrics": {"temp_c": 20 + i}}, key="KEY1")
        status, obj = self.get_json(
            "/api/v1/nodes/n1/history?metric=temp_c&since=3600s",
            token="secret")
        self.assertEqual(status, 200)
        self.assertEqual(len(obj["points"]), 3)
        self.assertEqual(obj["points"][0]["value"], 20)  # oldest first

    def test_update_node_requires_admin(self):
        self.post_json("/api/v1/telemetry", {"node": "n1", "temp_c": 20}, key="KEY1")
        # read token can't update (update needs admin; here admin==read token, so
        # instead verify no-token is rejected).
        status, _ = self.post_json("/api/v1/nodes/n1", {"name": "X"})
        self.assertEqual(status, 401)
        status, obj = self.post_json("/api/v1/nodes/n1",
                                     {"name": "Summit", "lat": 40.1, "lon": 29.0},
                                     token="secret")
        self.assertEqual(status, 200)
        self.assertEqual(obj["name"], "Summit")
        self.assertEqual(obj["lat"], 40.1)

    def test_unknown_node_404(self):
        self.assertEqual(self.get_json("/api/v1/nodes/nope", token="secret")[0], 404)

    def test_bad_json_422_or_400(self):
        status, raw, _ = _request(self.port, "POST", "/api/v1/telemetry",
                                   key="KEY1", body={"metrics": {"t": 1}})
        self.assertEqual(status, 422)  # missing node id

    def test_dashboard_served(self):
        status, body, ctype = self.get("/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ctype)
        self.assertIn(b"ForestGuard", body)

    def test_static_path_traversal_blocked(self):
        status, _, _ = self.get("/../app/config.py")
        self.assertIn(status, (403, 404))


if __name__ == "__main__":
    unittest.main()
