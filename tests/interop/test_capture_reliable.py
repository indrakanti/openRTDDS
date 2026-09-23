"""Validate bounded reliable-control packet selection."""

# Verifies: ORT-INT-007

import unittest

from capture_reliable import ACKNACK, HEARTBEAT, reliability_controls


class ReliableCaptureTests(unittest.TestCase):
    def setUp(self):
        self.source = bytes(range(12))
        self.header = b"RTPS" + bytes([2, 3, 1, 15]) + self.source

    @staticmethod
    def submessage(kind, content, flags=1):
        return (bytes([kind, flags]) + len(content).to_bytes(2, "little") +
                content)

    def test_selects_heartbeat_and_acknack_identities(self):
        heartbeat = bytes.fromhex("00000000 00000103") + bytes(20)
        acknack = bytes.fromhex("00000204 00000103") + bytes(16)
        controls = reliability_controls(
            self.header + self.submessage(HEARTBEAT, heartbeat) +
            self.submessage(ACKNACK, acknack))
        self.assertEqual(len(controls), 2)
        self.assertEqual(controls[0][2], bytes.fromhex("00000103"))
        self.assertEqual(controls[1][1], bytes.fromhex("00000204"))

    def test_info_source_changes_control_origin(self):
        effective = bytes(range(20, 32))
        info = bytes(4) + bytes([2, 3, 1, 16]) + effective
        heartbeat = bytes.fromhex("00000000 00000103") + bytes(20)
        controls = reliability_controls(
            self.header + self.submessage(0x0C, info) +
            self.submessage(HEARTBEAT, heartbeat))
        self.assertEqual(controls[0][3], effective)

    def test_rejects_malformed_control_sizes(self):
        self.assertEqual(reliability_controls(
            self.header + self.submessage(HEARTBEAT, bytes(27))), [])
        self.assertEqual(reliability_controls(
            self.header + self.submessage(ACKNACK, bytes(25))), [])


if __name__ == "__main__":
    unittest.main()
