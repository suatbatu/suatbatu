import unittest

from app.cloud import CloudForwarder
from app.db import Database
from app.models import Reading
from app.service import Service
from tests._util import FakeNotifier, make_config


def build(**cfg_overrides):
    cfg = make_config(**cfg_overrides)
    db = Database(":memory:")
    notifier = FakeNotifier()
    cloud = CloudForwarder(cfg, db)
    return cfg, db, notifier, Service(cfg, db, notifier, cloud), cloud


def reading(node, ts, **metrics):
    return Reading(node=node, ts=ts, metrics=metrics)


class TestService(unittest.TestCase):
    def test_ingest_stores_and_scores(self):
        _, db, _, svc, _ = build()
        out = svc.ingest(reading("n1", 1000, temp_c=22, humidity=55, smoke=100))
        self.assertEqual(out["node"], "n1")
        self.assertEqual(out["risk"]["level"], "ok")
        self.assertEqual(db.latest_metrics("n1")["temp_c"], 22)
        self.assertEqual(db.get_node("n1")["last_level"], "ok")

    def test_escalation_emits_event_and_notifies(self):
        _, db, notifier, svc, _ = build()
        svc.ingest(reading("n1", 1000, temp_c=20, humidity=60, smoke=100, gas=90))
        svc.ingest(reading("n1", 1001, temp_c=55, humidity=10, smoke=700, gas=600))
        node = db.get_node("n1")
        self.assertEqual(node["last_level"], "critical")
        events = db.list_events()
        self.assertTrue(any(e["type"] in ("risk_change", "flame") for e in events))
        self.assertTrue(any(n["level"] == "critical" for n in notifier.sent))

    def test_no_event_when_level_unchanged(self):
        _, db, _, svc, _ = build()
        svc.ingest(reading("n1", 1000, temp_c=20, humidity=60, smoke=100))
        svc.ingest(reading("n1", 1001, temp_c=21, humidity=59, smoke=105))
        # Both nominal -> exactly one transition (unknown->ok), no duplicates.
        risk_events = [e for e in db.list_events() if e["type"] == "risk_change"]
        self.assertEqual(len(risk_events), 1)

    def test_recovery_notifies_back_to_ok(self):
        _, db, notifier, svc, _ = build()
        svc.ingest(reading("n1", 1000, temp_c=55, humidity=10, smoke=700, gas=600))
        notifier.sent.clear()
        svc.ingest(reading("n1", 1100, temp_c=20, humidity=60, smoke=100, gas=90))
        self.assertEqual(db.get_node("n1")["last_level"], "ok")
        self.assertTrue(any(n["level"] == "ok" for n in notifier.sent))

    def test_offline_then_online(self):
        cfg, db, notifier, svc, _ = build(offline_after_s=300)
        svc.ingest(reading("n1", 1000, temp_c=20, humidity=60, smoke=100))
        # Sweep well past the offline window.
        res = svc.monitor_tick(now=2000)
        self.assertEqual(res["offline_flagged"], 1)
        self.assertEqual(db.get_node("n1")["last_level"], "offline")
        self.assertTrue(any(n["level"] == "offline" for n in notifier.sent))
        # Node reports again -> online recovery event.
        notifier.sent.clear()
        svc.ingest(reading("n1", 2100, temp_c=20, humidity=60, smoke=100))
        self.assertNotEqual(db.get_node("n1")["last_level"], "offline")
        self.assertTrue(any(e["type"] == "online" for e in db.list_events()))
        self.assertTrue(any(n["level"] == "online" for n in notifier.sent))

    def test_offline_sweep_idempotent(self):
        _, db, _, svc, _ = build(offline_after_s=300)
        svc.ingest(reading("n1", 1000, temp_c=20))
        self.assertEqual(svc.monitor_tick(now=2000)["offline_flagged"], 1)
        self.assertEqual(svc.monitor_tick(now=2100)["offline_flagged"], 0)

    def test_cloud_outbox_enqueue_when_enabled(self):
        _, db, _, svc, cloud = build(mode="redundancy",
                                     cloud_url="https://example.invalid/ingest")
        self.assertTrue(cloud.cfg.cloud_enabled)
        svc.ingest(reading("n1", 1000, temp_c=55, humidity=10, smoke=700, gas=600))
        stats = db.outbox_stats()
        # One telemetry row + at least one event row queued for the cloud.
        self.assertGreaterEqual(stats["pending"], 2)

    def test_cloud_disabled_no_outbox(self):
        _, db, _, svc, _ = build()  # local mode
        svc.ingest(reading("n1", 1000, temp_c=55, smoke=700))
        self.assertEqual(db.outbox_stats()["pending"], 0)


if __name__ == "__main__":
    unittest.main()
