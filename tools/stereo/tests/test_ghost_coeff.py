import unittest

try:
    import numpy as np
    import ghost_coeff
except ImportError:  # pragma: no cover - numpy is on the rig's venv, not in every checkout
    np = None


@unittest.skipIf(np is None, 'needs numpy')
class GhostCoeffTests(unittest.TestCase):
    def test_a_copy_of_the_other_eye_is_measured(self):
        rng = np.random.default_rng(1)
        left = ghost_coeff.gaussian_blur(rng.normal(size=(256, 320)), 1.5)
        right = ghost_coeff.gaussian_blur(rng.normal(size=(256, 320)), 1.5)
        ghosted = left + 0.12 * right
        into_left, into_right, control = ghost_coeff.coefficients(ghosted, right, 1.0, 6.0, 64)
        self.assertAlmostEqual(into_left, 0.12, delta=0.02)
        self.assertLess(abs(control), 0.02)

    def test_unrelated_eyes_measure_near_zero(self):
        rng = np.random.default_rng(2)
        left = ghost_coeff.gaussian_blur(rng.normal(size=(256, 320)), 1.5)
        right = ghost_coeff.gaussian_blur(rng.normal(size=(256, 320)), 1.5)
        into_left, into_right, _ = ghost_coeff.coefficients(left, right, 1.0, 6.0, 64)
        self.assertLess(abs(into_left), 0.02)
        self.assertLess(abs(into_right), 0.02)


if __name__ == '__main__':
    unittest.main()
