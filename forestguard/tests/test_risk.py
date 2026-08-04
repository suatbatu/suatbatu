import unittest

from app.config import RiskThresholds
from app.risk import score


T = RiskThresholds()


class TestRisk(unittest.TestCase):
    def test_all_nominal_is_ok(self):
        r = score({"temp_c": 20, "humidity": 60, "smoke": 100, "gas": 90}, T)
        self.assertEqual(r.level, "ok")
        self.assertEqual(r.score, 0.0)
        self.assertFalse(r.flame)

    def test_flame_forces_critical(self):
        r = score({"temp_c": 20, "humidity": 60, "flame": 1}, T)
        self.assertEqual(r.level, "critical")
        self.assertTrue(r.flame)
        self.assertEqual(r.reasons[0], "flame detected")

    def test_heat_is_monotonic(self):
        cool = score({"temp_c": 30}, T).components["heat"]
        hot = score({"temp_c": 45}, T).components["heat"]
        self.assertGreater(hot, cool)

    def test_dryness_reversed_band(self):
        humid = score({"humidity": 50}, T).components["dryness"]
        dry = score({"humidity": 15}, T).components["dryness"]
        self.assertGreater(dry, humid)
        self.assertAlmostEqual(dry, 1.0)

    def test_smoke_hard_warning_overrides_blend(self):
        # A smoke spike alone should reach at least "warning" even if other
        # signals are calm and would average it down.
        r = score({"temp_c": 20, "humidity": 60, "smoke": 500, "gas": 90}, T)
        self.assertIn(r.level, ("warning", "critical"))
        self.assertTrue(any("smoke" in reason for reason in r.reasons))

    def test_missing_metrics_scored_on_available(self):
        r = score({"temp_c": 48}, T)  # only one signal present
        self.assertEqual(set(r.components), {"heat"})
        self.assertGreater(r.score, 0)

    def test_empty_is_ok(self):
        r = score({}, T)
        self.assertEqual(r.level, "ok")
        self.assertEqual(r.components, {})

    def test_clamping(self):
        r = score({"temp_c": 999, "humidity": -5, "smoke": 99999}, T)
        for v in r.components.values():
            self.assertLessEqual(v, 1.0)
            self.assertGreaterEqual(v, 0.0)


if __name__ == "__main__":
    unittest.main()
