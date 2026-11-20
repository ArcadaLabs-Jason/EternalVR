"""Tests for tools/fetch_clang_tidy.py: which archive members are kept, and that a bad download unpacks nothing.

Run from the repository root: python -m unittest discover -s tools/tests
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import fetch_clang_tidy as fct  # noqa: E402
import run_clang_tidy as rct  # noqa: E402

TOP = 'clang+llvm-23.1.2-x86_64-pc-windows-msvc'


class WantedTests(unittest.TestCase):
    def test_keeps_clang_tidy_and_clangs_own_headers(self):
        self.assertEqual(fct.wanted(f'{TOP}/bin/clang-tidy.exe', '23.1.2'), 'bin/clang-tidy.exe')
        self.assertEqual(fct.wanted(f'{TOP}/lib/clang/23/include/intrin.h', '23.1.2'), 'lib/clang/23/include/intrin.h')
        self.assertEqual(fct.wanted(f'{TOP}/lib/clang/23/include/cuda_wrappers/new', '23.1.2'),
                         'lib/clang/23/include/cuda_wrappers/new')

    def test_drops_everything_else(self):
        for name in [f'{TOP}/bin/clang.exe', f'{TOP}/bin/clang-tidy.pdb', f'{TOP}/lib/clang/23/lib/windows/x.lib',
                     f'{TOP}/lib/clang/22/include/intrin.h', f'{TOP}/lib/clang/23/include/', f'{TOP}/include/llvm/a.h',
                     'bin/clang-tidy.exe', TOP, f'{TOP}/lib/clang/23/include/../../../../evil.h']:
            self.assertIsNone(fct.wanted(name, '23.1.2'), name)


class ChecksumTests(unittest.TestCase):
    def test_the_pinned_version_is_the_baselines(self):
        self.assertEqual(fct.VERSION, rct.baseline_version(rct.BASELINE))
        self.assertIn(fct.VERSION, fct.ARCHIVE)
        self.assertEqual(len(fct.SHA256), 64)

    def test_a_download_that_does_not_match_unpacks_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = os.path.join(tmp, fct.ARCHIVE)
            with open(archive, 'wb') as f:
                f.write(b'not the archive')
            out = os.path.join(tmp, 'out')
            with contextlib.redirect_stdout(io.StringIO()) as printed:
                code = fct.main(['--out', out, '--archive', archive])
            self.assertEqual(code, 1)
            self.assertIn('expected ' + fct.SHA256, printed.getvalue())
            self.assertFalse(os.path.exists(out))


if __name__ == '__main__':
    unittest.main()
