"""Validate fixture selection before the live vendor gate runs in CI."""

# Verifies: ORT-INT-001, ORT-INT-002

import unittest

from capture_spdp import contains_spdp


class CaptureFilterTests(unittest.TestCase):
    def setUp(self):
        self.header = b"RTPS" + bytes([2, 3, 1, 15]) + bytes(range(12))
        self.content = b"\x00\x00\x10\x00" + bytes(4) + \
            b"\x00\x01\x00\xc2" + bytes(12)

    def test_selects_spdp_behind_info(self):
        info_ts = b"\x09\x03\x00\x00"
        data = b"\x15\x05" + len(self.content).to_bytes(2, "little")
        self.assertTrue(contains_spdp(self.header + info_ts + data + self.content))

    def test_rejects_invalid_bound_and_other_writer(self):
        data = b"\x15\x05\xff\x7f"
        self.assertFalse(contains_spdp(self.header + data + self.content))
        other = bytearray(self.content)
        other[8:12] = b"\x00\x00\x03\xc2"
        data = b"\x15\x05" + len(other).to_bytes(2, "little")
        self.assertFalse(contains_spdp(self.header + data + other))

    def test_rejects_short_and_non_rtps_input(self):
        self.assertFalse(contains_spdp(b"RTPS"))
        self.assertFalse(contains_spdp(b"XXXX" + self.header[4:]))


if __name__ == "__main__":
    unittest.main()
