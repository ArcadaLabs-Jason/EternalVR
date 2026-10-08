import os
import struct
import tempfile
import unittest
import zlib

import eye_diff


def png_with_filters(width, height, rgb, filters):
    """A PNG whose rows use the given filter types (encoded here, decoded by eye_diff)."""
    stride = width * 3
    raw = b''
    prev = bytes(stride)
    for y in range(height):
        line = rgb[y * stride:(y + 1) * stride]
        f = filters[y]
        if f == 0:
            enc = line
        elif f == 1:
            enc = bytes((line[i] - (line[i - 3] if i >= 3 else 0)) & 0xFF for i in range(stride))
        elif f == 2:
            enc = bytes((line[i] - prev[i]) & 0xFF for i in range(stride))
        else:
            raise ValueError(f)
        raw += bytes([f]) + enc
        prev = line

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))

    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')


class EyeDiffTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.dir.cleanup()

    def path(self, name):
        return os.path.join(self.dir.name, name)

    def test_identical_pair(self):
        rgb = bytes(range(4 * 3 * 3))
        eye_diff.write_png(self.path('p1-L.png'), 4, 3, rgb)
        eye_diff.write_png(self.path('p1-R.png'), 4, 3, rgb)
        left, right = eye_diff.read_png(self.path('p1-L.png')), eye_diff.read_png(self.path('p1-R.png'))
        r = eye_diff.compare(left, right, 0)
        self.assertEqual(r['differing'], 0)
        self.assertEqual(r['max'], 0)
        self.assertIsNone(r['box'])

    def test_one_changed_pixel(self):
        rgb = bytearray(5 * 4 * 3)
        other = bytearray(rgb)
        other[(2 * 5 + 3) * 3 + 1] = 200  # pixel (3, 2), green
        eye_diff.write_png(self.path('a-L.png'), 5, 4, bytes(rgb))
        eye_diff.write_png(self.path('a-R.png'), 5, 4, bytes(other))
        r = eye_diff.compare(eye_diff.read_png(self.path('a-L.png')), eye_diff.read_png(self.path('a-R.png')), 8)
        self.assertEqual(r['differing'], 1)
        self.assertEqual(r['max'], 200)
        self.assertEqual(r['box'], (3, 2, 3, 2))

    def test_filters_decode(self):
        rgb = bytes((i * 37) & 0xFF for i in range(3 * 3 * 3))
        with open(self.path('f.png'), 'wb') as f:
            f.write(png_with_filters(3, 3, rgb, [1, 2, 0]))
        for pillow in (False, True):
            w, h, c, data = eye_diff.read_png(self.path('f.png'), pillow=pillow)
            self.assertEqual((w, h, c), (3, 3, 3))
            self.assertEqual(data, rgb)

    def test_main_lists_pairs(self):
        rgb = bytes(2 * 2 * 3)
        eye_diff.write_png(self.path('x-L.png'), 2, 2, rgb)
        eye_diff.write_png(self.path('x-R.png'), 2, 2, rgb)
        self.assertEqual(eye_diff.main([self.dir.name]), 0)
        self.assertEqual(eye_diff.main([os.path.join(self.dir.name, 'none')]), 1)


if __name__ == '__main__':
    unittest.main()
