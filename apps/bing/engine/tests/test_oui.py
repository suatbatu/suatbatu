import unittest

from bing_engine import oui


class TestOui(unittest.TestCase):
    def test_normalize_variants(self):
        self.assertEqual(oui.normalize("b8:27:eb:12:34:56"), "B8:27:EB:12:34:56")
        self.assertEqual(oui.normalize("b827eb-123456"), "B8:27:EB:12:34:56")
        self.assertEqual(oui.normalize("B827.EB12.3456"), "B8:27:EB:12:34:56")
        self.assertIsNone(oui.normalize("not-a-mac"))
        self.assertIsNone(oui.normalize("b8:27:eb:12:34"))  # too short
        self.assertIsNone(oui.normalize(""))

    def test_known_vendors(self):
        self.assertEqual(oui.lookup("B8:27:EB:00:00:01"), "Raspberry Pi")
        self.assertEqual(oui.lookup("3C:07:54:aa:bb:cc"), "Apple")
        self.assertEqual(oui.lookup("240AC4-000000"), "Espressif (ESP32)")

    def test_randomised_mac(self):
        # 2nd nibble 2/6/A/E => locally administered
        self.assertEqual(oui.lookup("AE:11:22:33:44:55"), "Locally administered (randomised)")
        self.assertEqual(oui.lookup("06:11:22:33:44:55"), "Locally administered (randomised)")

    def test_unknown_returns_none(self):
        self.assertIsNone(oui.lookup("00:99:99:99:99:99"))

    def test_csv_split(self):
        self.assertEqual(oui._csv_split('MA-L,001122,"Acme, Inc.",addr'),
                         ["MA-L", "001122", "Acme, Inc.", "addr"])


if __name__ == "__main__":
    unittest.main()
