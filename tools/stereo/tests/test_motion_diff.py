import os
import tempfile
import unittest

try:
    import numpy as np
    import motion_diff
except ImportError:  # pragma: no cover - numpy is on the rig's venv, not in every checkout
    np = None


@unittest.skipIf(np is None, 'needs numpy')
class MotionDiffTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.dir.cleanup()

    def write_capture(self, left, right):
        stem = os.path.join(self.dir.name, 'mv-1-p000010')
        height, width = left.shape[:2]
        left.astype(np.float16).tofile(stem + '-L.raw')
        right.astype(np.float16).tofile(stem + '-R.raw')
        with open(stem + '.txt', 'w', encoding='utf-8') as f:
            for eye in 'LR':
                f.write('eye %s: %s-%s.raw, VkFormat 83, %dx%d, %d bytes\n' %
                        (eye, stem, eye, width, height, width * height * 4))
        return stem

    def test_sidecar_reads_format_and_size(self):
        stem = self.write_capture(np.zeros((2, 3, 2)), np.zeros((2, 3, 2)))
        self.assertEqual(motion_diff.sidecar(stem + '.txt'), {'L': (83, 3, 2), 'R': (83, 3, 2)})

    def test_lost_counts_eye_l_motion_that_eye_r_has_at_zero(self):
        left = np.zeros((2, 4, 2))
        right = np.zeros((2, 4, 2))
        left[0, 0] = (0.5, 0.0)   # moving in both
        right[0, 0] = (0.5, 0.0)
        left[0, 1] = (0.0, 0.25)  # moving in eye L only
        left[1, 3] = (-1.0, 0.0)  # moving in eye L only
        stem = self.write_capture(left, right)
        eyes = motion_diff.sidecar(stem + '.txt')
        r = motion_diff.compare(motion_diff.load(stem + '-L.raw', *eyes['L']),
                                motion_diff.load(stem + '-R.raw', *eyes['R']), 1e-5)
        self.assertEqual(r['moving_l'], 3)
        self.assertEqual(r['moving_r'], 1)
        self.assertAlmostEqual(r['lost'], 2 / 3)

    def test_boxes_compare_regions_and_skip_lost_when_shapes_differ(self):
        left = np.zeros((4, 4, 2))
        right = np.zeros((4, 4, 2))
        left[0, 3] = (1.0, 0.0)
        right[0, 1] = (1.0, 0.0)
        r = motion_diff.compare(left, right, 1e-5, motion_diff.box('0,1,2,4'), motion_diff.box('0,1,0,3'))
        self.assertEqual(r['moving_l'], 1)
        self.assertEqual(r['moving_r'], 1)
        self.assertIsNone(r['lost'])


if __name__ == '__main__':
    unittest.main()
