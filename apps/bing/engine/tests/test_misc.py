import ipaddress
import unittest

from bing_engine import discovery, netinfo, security, util, wol


class TestSecurity(unittest.TestCase):
    def test_clean_device_scores_100(self):
        rep = security.assess("10.0.0.1", [443, 22])  # 22 not risky, 443 not risky
        self.assertEqual(rep.score, 100)
        self.assertEqual(rep.grade, "A")
        self.assertEqual(rep.findings, [])

    def test_risky_ports_penalised(self):
        rep = security.assess("10.0.0.2", [23, 6379])  # telnet + redis, both high
        self.assertEqual(rep.score, 40)  # 100 - 30 - 30
        self.assertEqual(rep.grade, "F")
        self.assertEqual({f.port for f in rep.findings}, {23, 6379})

    def test_http_without_https_tip(self):
        rep = security.assess("10.0.0.3", [80])
        self.assertTrue(any("HTTPS" in a for a in rep.encrypted_alternatives))


class TestWol(unittest.TestCase):
    def test_magic_packet(self):
        pkt = wol.build_magic_packet("B8:27:EB:12:34:56")
        self.assertEqual(len(pkt), 102)
        self.assertEqual(pkt[:6], b"\xff" * 6)
        self.assertEqual(pkt[6:12], bytes.fromhex("B827EB123456"))
        # MAC repeated 16 times
        self.assertEqual(pkt[6:12], pkt[96:102])

    def test_invalid_mac_raises(self):
        with self.assertRaises(ValueError):
            wol.build_magic_packet("nope")


class TestDeviceClassification(unittest.TestCase):
    def _dev(self, **kw):
        d = discovery.Device(ip="10.0.0.9")
        for k, v in kw.items():
            setattr(d, k, v)
        return d

    def test_gateway(self):
        d = self._dev(is_gateway=True)
        discovery.guess_device(d)
        self.assertEqual(d.icon, "router")

    def test_printer_by_port(self):
        d = self._dev(open_ports=[9100, 515])
        discovery.guess_device(d)
        self.assertEqual(d.device_type, "Printer")

    def test_iphone_by_vendor_and_port(self):
        d = self._dev(vendor="Apple", open_ports=[62078])
        discovery.guess_device(d)
        self.assertEqual(d.icon, "phone")

    def test_windows_pc(self):
        d = self._dev(open_ports=[445, 139, 3389])
        discovery.guess_device(d)
        self.assertEqual(d.device_type, "Windows PC")

    def test_chromecast(self):
        d = self._dev(open_ports=[8008, 8009])
        discovery.guess_device(d)
        self.assertEqual(d.icon, "cast")

    def test_hostname_signal(self):
        d = self._dev(hostname="batu-iphone.local")
        discovery.guess_device(d)
        self.assertEqual(d.device_type, "iPhone")


class TestArpTable(unittest.TestCase):
    def test_read_arp_returns_dict(self):
        table = discovery.read_arp_table()
        self.assertIsInstance(table, dict)
        for ip, mac in table.items():
            self.assertRegex(ip, r"^\d+\.\d+\.\d+\.\d+$")
            self.assertRegex(mac, r"^[0-9A-F:]{17}$")


class TestUtil(unittest.TestCase):
    def test_human_bytes(self):
        self.assertEqual(util.human_bytes(512), "512 B")
        self.assertEqual(util.human_bytes(1536), "1.50 KB")

    def test_human_bits(self):
        self.assertEqual(util.human_bits_per_sec(125_000_000), "125.00 Mbps")

    def test_parallel_map_order(self):
        result = util.parallel_map(lambda x: x * x, [1, 2, 3, 4], workers=4)
        self.assertEqual(result, [1, 4, 9, 16])

    def test_table_renders(self):
        out = util.table([["a", "b"]], ["H1", "H2"])
        self.assertIn("H1", out)
        self.assertIn("a", out)


class TestNetinfo(unittest.TestCase):
    def test_primary_ipv4_is_valid_or_none(self):
        ip = netinfo.primary_ipv4()
        if ip is not None:
            ipaddress.IPv4Address(ip)  # raises if malformed

    def test_gather_runs(self):
        info = netinfo.gather(include_public=False)
        self.assertIsNotNone(info.hostname)
        d = info.to_dict()
        self.assertIn("interfaces", d)


if __name__ == "__main__":
    unittest.main()
