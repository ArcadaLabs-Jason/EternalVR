"""Tests for tools/run_clang_tidy.py: parsing clang-tidy's output and comparing it with the baseline.

Run from the repository root: python -m unittest discover -s tools/tests
"""
import collections
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import run_clang_tidy as rct  # noqa: E402

ROOT = os.path.abspath(os.sep + 'repo')


def at(*parts):
    return os.path.join(ROOT, *parts)


class ParseTests(unittest.TestCase):
    def test_reads_warnings_under_src_only(self):
        out = '\n'.join([
            f'{at("src", "vkcore", "a.cpp")}:12:5: warning: use of a moved-from object [bugprone-use-after-move]',
            f'{at("build", "_deps", "x.hpp")}:3:1: warning: something [bugprone-foo]',
            f'{at("tests", "t.cpp")}:3:1: warning: something [bugprone-foo]',
            '    int x = y;',
            '        ^',
            '12 warnings generated.',
        ])
        self.assertEqual(rct.parse_findings(out, ROOT),
                         {('src/vkcore/a.cpp', 12, 5, 'bugprone-use-after-move', 'use of a moved-from object')})

    def test_a_header_seen_from_two_units_counts_once(self):
        line = f'{at("src", "common", "h.hpp")}:7:2: warning: copy of a big value [performance-unnecessary-value-param]'
        findings = rct.parse_findings(line, ROOT) | rct.parse_findings(line, ROOT)
        self.assertEqual(sum(rct.count_keys(findings).values()), 1)

    def test_paths_in_messages_are_made_relative(self):
        lam = at('src', 'vkcore', 'ui.cpp')
        out = (f'{lam}:143:17: warning: an exception may be thrown in function \'(lambda at {lam}:143:17)\' '
               'which should not throw exceptions [bugprone-exception-escape]')
        (finding,) = rct.parse_findings(out, ROOT)
        self.assertEqual(finding[4], "an exception may be thrown in function '(lambda at src/vkcore/ui.cpp)' "
                                     'which should not throw exceptions')

    def test_compile_errors_are_findings_of_their_own_check(self):
        out = f'{at("src", "a.cpp")}:1:10: error: \'nope.h\' file not found [clang-diagnostic-error]'
        (finding,) = rct.parse_findings(out, ROOT)
        self.assertEqual(finding[3], 'clang-diagnostic-error')


class UnitFailedTests(unittest.TestCase):
    warning = ('src/a.cpp', 3, 1, 'bugprone-x', 'm')
    compile_error = ('src/a.cpp', 4, 1, 'clang-diagnostic-error', 'unknown type')

    def test_a_crash_after_some_findings_fails_the_unit(self):
        self.assertTrue(rct.unit_failed(1, {self.warning}))

    def test_a_crash_with_no_findings_fails_the_unit(self):
        self.assertTrue(rct.unit_failed(1, set()))

    def test_a_compile_error_under_src_explains_the_exit(self):
        self.assertFalse(rct.unit_failed(1, {self.warning, self.compile_error}))

    def test_a_clean_exit_never_fails(self):
        self.assertFalse(rct.unit_failed(0, {self.warning}))


class BaselineTests(unittest.TestCase):
    def test_round_trip(self):
        counts = collections.Counter({('src/a.cpp', 'bugprone-x', 'message with [brackets] and spaces'): 2,
                                      ('src/b.hpp', 'performance-y', 'other'): 1})
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'baseline.txt')
            rct.write_baseline(path, counts, '23.1.2')
            self.assertEqual(rct.read_baseline(path), counts)
            self.assertEqual(rct.baseline_version(path), '23.1.2')

    def test_a_baseline_without_a_version_line_has_none(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'baseline.txt')
            with open(path, 'w', encoding='utf-8') as f:
                f.write('# Accepted clang-tidy findings\n1 src/a.cpp [bugprone-x] m\n')
            self.assertIsNone(rct.baseline_version(path))

    def test_missing_file_is_an_empty_baseline(self):
        self.assertEqual(rct.read_baseline(os.path.join(tempfile.gettempdir(), 'no-such-baseline.txt')),
                         collections.Counter())

    def test_line_numbers_do_not_matter(self):
        before = rct.count_keys({('src/a.cpp', 10, 1, 'bugprone-x', 'm')})
        after = rct.count_keys({('src/a.cpp', 42, 3, 'bugprone-x', 'm')})
        self.assertEqual(rct.compare(after, before), ({}, {}))

    def test_one_more_of_a_known_finding_is_new(self):
        base = collections.Counter({('src/a.cpp', 'bugprone-x', 'm'): 1})
        now = rct.count_keys({('src/a.cpp', 10, 1, 'bugprone-x', 'm'), ('src/a.cpp', 20, 1, 'bugprone-x', 'm')})
        self.assertEqual(rct.compare(now, base), ({('src/a.cpp', 'bugprone-x', 'm'): 1}, {}))

    def test_a_fixed_finding_is_gone_not_new(self):
        base = collections.Counter({('src/a.cpp', 'bugprone-x', 'm'): 2})
        now = rct.count_keys({('src/a.cpp', 10, 1, 'bugprone-x', 'm')})
        self.assertEqual(rct.compare(now, base), ({}, {('src/a.cpp', 'bugprone-x', 'm'): 1}))


if __name__ == '__main__':
    unittest.main()
