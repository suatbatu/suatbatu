import time
import unittest

from app.models import ValidationError, parse_payload, parse_reading


class TestModels(unittest.TestCase):
    def test_single_reading(self):
        rd = parse_reading({"node": "n1", "metrics": {"temp_c": 30.5}})
        self.assertEqual(rd.node, "n1")
        self.assertEqual(rd.metrics["temp_c"], 30.5)

    def test_batch_and_wrapped(self):
        self.assertEqual(len(parse_payload([{"node": "a", "temp_c": 1},
                                            {"node": "b", "temp_c": 2}])), 2)
        self.assertEqual(len(parse_payload({"readings": [{"node": "a", "x": 1}]})), 1)

    def test_flat_form_promotes_numbers(self):
        rd = parse_reading({"node": "n1", "temp_c": 22, "humidity": 40, "name": "R"})
        self.assertEqual(rd.metrics, {"temp_c": 22.0, "humidity": 40.0})
        self.assertEqual(rd.name, "R")

    def test_millisecond_timestamp(self):
        now = time.time()
        rd = parse_reading({"node": "n", "ts": now * 1000, "metrics": {"x": 1}}, now=now)
        self.assertAlmostEqual(rd.ts, now, delta=1)

    def test_requires_node(self):
        with self.assertRaises(ValidationError):
            parse_reading({"metrics": {"temp_c": 1}})

    def test_requires_metrics(self):
        with self.assertRaises(ValidationError):
            parse_reading({"node": "n1"})

    def test_rejects_non_numeric_metric(self):
        with self.assertRaises(ValidationError):
            parse_reading({"node": "n1", "metrics": {"temp_c": "hot"}})

    def test_rejects_bool_metric(self):
        with self.assertRaises(ValidationError):
            parse_reading({"node": "n1", "metrics": {"flame": True}})

    def test_rejects_clock_skew(self):
        with self.assertRaises(ValidationError):
            parse_reading({"node": "n1", "ts": 1, "metrics": {"x": 1}}, now=1e9)

    def test_rejects_nan_inf(self):
        with self.assertRaises(ValidationError):
            parse_reading({"node": "n1", "metrics": {"x": float("inf")}})


if __name__ == "__main__":
    unittest.main()
