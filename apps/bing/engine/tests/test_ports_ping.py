import socket
import threading
import unittest

from bing_engine import ping, ports


class LocalServer:
    """A throwaway TCP listener on an ephemeral port for probe tests."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(5)
        self.port = self.sock.getsockname()[1]
        self._stop = False
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while not self._stop:
            try:
                self.sock.settimeout(0.5)
                conn, _ = self.sock.accept()
                conn.close()
            except (socket.timeout, OSError):
                continue

    def close(self):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass


class TestPortSpec(unittest.TestCase):
    def test_parse_list(self):
        self.assertEqual(ports.parse_port_spec("22,80,443"), [22, 80, 443])

    def test_parse_range(self):
        self.assertEqual(ports.parse_port_spec("79-82"), [79, 80, 81, 82])

    def test_parse_named(self):
        self.assertEqual(ports.parse_port_spec("common"), ports.COMMON_PORTS)
        self.assertEqual(ports.parse_port_spec("top100"), ports.TOP_100_PORTS)
        self.assertEqual(len(ports.parse_port_spec("all")), 65535)

    def test_dedup_and_bounds(self):
        self.assertEqual(ports.parse_port_spec("80,80,80"), [80])
        self.assertEqual(ports.parse_port_spec("0,80,70000"), [80])

    def test_service_name(self):
        self.assertEqual(ports.service_name(443), "https")
        self.assertEqual(ports.service_name(22), "ssh")
        self.assertIsNone(ports.service_name(12345))


class TestScanning(unittest.TestCase):
    def setUp(self):
        self.server = LocalServer()

    def tearDown(self):
        self.server.close()

    def test_open_port_detected(self):
        results = ports.scan("127.0.0.1", ports=[self.server.port], timeout=1.0, banner=False)
        self.assertEqual(len(results), 1)
        self.assertTrue(results[0].open)
        self.assertEqual(results[0].port, self.server.port)

    def test_closed_port_not_reported(self):
        # An almost-certainly-closed high port.
        results = ports.scan("127.0.0.1", ports=[8123, 8124], timeout=0.3, banner=False)
        self.assertNotIn(self.server.port, [r.port for r in results])

    def test_tcp_ping(self):
        stats = ping.ping("127.0.0.1", count=3, port=self.server.port,
                          force_tcp=True, interval=0.02)
        self.assertEqual(stats.transmitted, 3)
        self.assertEqual(stats.received, 3)
        self.assertEqual(stats.loss_pct, 0.0)
        self.assertIsNotNone(stats.summary()["avg"])


class TestPingStats(unittest.TestCase):
    def test_summary_math(self):
        stats = ping.PingStats(host="x", address="1.1.1.1", method="tcp",
                               transmitted=2, received=2)
        stats.replies = [ping.PingReply(0, True, 10.0), ping.PingReply(1, True, 20.0)]
        s = stats.summary()
        self.assertEqual(s["min"], 10.0)
        self.assertEqual(s["max"], 20.0)
        self.assertEqual(s["avg"], 15.0)

    def test_loss_calculation(self):
        stats = ping.PingStats(host="x", address=None, method="tcp",
                               transmitted=4, received=1)
        self.assertEqual(stats.loss_pct, 75.0)


if __name__ == "__main__":
    unittest.main()
