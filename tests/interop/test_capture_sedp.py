"""Exercise SEDP capture selection without a privileged raw socket."""

# Verifies: ORT-INT-005

import unittest

from capture_sedp import is_sedp_publication, udp_payload


class SedpCaptureTests(unittest.TestCase):
    def setUp(self):
        self.header = b"RTPS" + bytes([2, 3, 1, 15]) + bytes(range(12))

    @staticmethod
    def data(writer):
        content = bytes(8) + writer + bytes(12)
        return b"\x15\x05" + len(content).to_bytes(2, "little") + content

    def test_selects_publication_after_other_data(self):
        message = self.header + self.data(b"\x00\x01\x00\xc2") + \
            self.data(b"\x00\x00\x03\xc2")
        self.assertTrue(is_sedp_publication(message))

    def test_rejects_bad_bounds_and_other_writer(self):
        self.assertFalse(is_sedp_publication(self.header + b"\x15\x05\xff\x7f"))
        self.assertFalse(is_sedp_publication(
            self.header + self.data(b"\x00\x00\x04\xc2")))
        self.assertFalse(is_sedp_publication(b"oops" + self.header[4:]))

    def test_extracts_only_complete_ipv4_udp(self):
        body = self.header + self.data(b"\x00\x00\x03\xc2")
        udp = bytes(4) + (len(body) + 8).to_bytes(2, "big") + bytes(2) + body
        ip = bytearray(20)
        ip[0] = 0x45
        ip[2:4] = (20 + len(udp)).to_bytes(2, "big")
        ip[9] = 17
        self.assertEqual(udp_payload(bytes(ip) + udp), body)
        self.assertEqual(udp_payload(bytes(ip) + udp[:-1]), b"")
        ip[6:8] = (1).to_bytes(2, "big")
        self.assertEqual(udp_payload(bytes(ip) + udp), b"")


if __name__ == "__main__":
    unittest.main()
