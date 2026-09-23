"""Validate best-effort user DATA packet selection."""

# Verifies: ORT-INT-006

import unittest

from capture_data import user_writer_id


class DataCaptureTests(unittest.TestCase):
    def setUp(self):
        self.header = b"RTPS" + bytes([2, 3, 1, 15]) + bytes(range(12))

    @staticmethod
    def data(writer, flags=0x05):
        content = bytes(8) + writer + bytes(8) + b"\x00\x01\x00\x00"
        return bytes([0x15, flags]) + len(content).to_bytes(2, "little") + content

    def test_selects_user_writer_after_builtin_data(self):
        built_in = self.data(b"\x00\x00\x03\xc2")
        user = self.data(b"\x00\x00\x01\x03")
        self.assertEqual(user_writer_id(self.header + built_in + user),
                         b"\x00\x00\x01\x03")

    def test_accepts_keyed_and_unkeyed_user_writers(self):
        self.assertEqual(user_writer_id(
            self.header + self.data(b"\x00\x00\x02\x02")),
            b"\x00\x00\x02\x02")
        self.assertEqual(user_writer_id(
            self.header + self.data(b"\x00\x00\x03\x03")),
            b"\x00\x00\x03\x03")

    def test_rejects_key_only_truncated_and_non_rtps(self):
        self.assertIsNone(user_writer_id(
            self.header + self.data(b"\x00\x00\x01\x03", flags=0x09)))
        self.assertIsNone(user_writer_id(self.header + b"\x15\x05\xff\x7f"))
        self.assertIsNone(user_writer_id(b"nope" + self.header[4:]))


if __name__ == "__main__":
    unittest.main()
