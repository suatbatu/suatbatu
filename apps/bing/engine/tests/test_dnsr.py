import struct
import unittest

from bing_engine import dnsr


class TestDnsWire(unittest.TestCase):
    def test_encode_decode_name(self):
        encoded = dnsr._encode_name("example.com")
        self.assertEqual(encoded, b"\x07example\x03com\x00")
        name, offset = dnsr._decode_name(encoded, 0)
        self.assertEqual(name, "example.com")
        self.assertEqual(offset, len(encoded))

    def test_build_query_structure(self):
        q = dnsr._build_query("example.com", dnsr.QTYPES["A"], qid=0x1234)
        qid, flags, qd, an, ns, ar = struct.unpack(">HHHHHH", q[:12])
        self.assertEqual(qid, 0x1234)
        self.assertEqual(flags, 0x0100)  # RD set
        self.assertEqual((qd, an, ns, ar), (1, 0, 0, 0))

    def _make_response(self, rtype, rdata):
        header = struct.pack(">HHHHHH", 0x1234, 0x8180, 1, 1, 0, 0)
        question = dnsr._encode_name("example.com") + struct.pack(">HH", rtype, 1)
        answer = (b"\xc0\x0c" + struct.pack(">HHIH", rtype, 1, 300, len(rdata)) + rdata)
        return header + question + answer

    def test_parse_a_record(self):
        import socket
        resp = self._make_response(1, socket.inet_aton("93.184.216.34"))
        records = dnsr._parse_response(resp)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].rtype, "A")
        self.assertEqual(records[0].value, "93.184.216.34")
        self.assertEqual(records[0].ttl, 300)

    def test_parse_mx_record(self):
        rdata = struct.pack(">H", 10) + dnsr._encode_name("mail.example.com")
        resp = self._make_response(15, rdata)
        records = dnsr._parse_response(resp)
        self.assertEqual(records[0].rtype, "MX")
        self.assertEqual(records[0].value, "10 mail.example.com")

    def test_parse_txt_record(self):
        rdata = b"\x0bhello world"
        resp = self._make_response(16, rdata)
        records = dnsr._parse_response(resp)
        self.assertEqual(records[0].rtype, "TXT")
        self.assertEqual(records[0].value, '"hello world"')

    def test_nxdomain_raises(self):
        header = struct.pack(">HHHHHH", 0x1234, 0x8183, 1, 0, 0, 0)  # rcode 3
        question = dnsr._encode_name("nope.invalid") + struct.pack(">HH", 1, 1)
        with self.assertRaises(dnsr.DNSError):
            dnsr._parse_response(header + question)

    def test_unsupported_type(self):
        with self.assertRaises(dnsr.DNSError):
            dnsr.query("example.com", "WKS")

    def test_os_resolve_localhost(self):
        # getaddrinfo honours /etc/hosts, so localhost should resolve.
        addrs = dnsr.resolve("localhost")
        self.assertTrue(any(a.startswith("127.") or a == "::1" for a in addrs))


if __name__ == "__main__":
    unittest.main()
