# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
"""Tests of dm48serial.py's framing: cd tools && python3 -m unittest test_dm48serial."""
import unittest

from dm48serial import encode, frames


class Framing(unittest.TestCase):
    def test_channel_2_lines_only(self):
        # a screen row on channel 0 (its binary part has the top bit set), then two lines on channel 2 in one write
        # (DMCP puts the id only before the first), then a channel-1 line, then channel 2 again, split over reads
        stream = [b'\x10L 05 \x81\xff\x80\n\x12hello\nworld\n\x11sys\n\x12par', b'ted\n']
        self.assertEqual(list(frames(stream)), [b'hello', b'world', b'parted'])

    def test_encode(self):
        self.assertEqual(encode('ask 1\n'), b'\x12ask 1\n')
        self.assertEqual(encode('a\tb\r\n'), b'\x12a\tb\n')
        for bad in ('\x12', 'café', 'x' * 256):
            with self.assertRaises(ValueError):
                encode(bad)


if __name__ == '__main__':
    unittest.main()
