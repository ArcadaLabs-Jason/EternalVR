"""Tests for tools/check_scope.py: what counts as an engine internal, where, and the allow-list.

Run from the repository root: python -m unittest discover -s tools/tests
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import check_scope as cs  # noqa: E402


def kinds(text):
    return [(kind, what) for _, kind, what in cs.findings_in(text)]


class CodeLinesTests(unittest.TestCase):
    def test_comments_are_blanked_and_lines_kept(self):
        lines = cs.code_lines('a // rax\n/* rbx\n idPlayer */ b\n"// kept"\n')
        self.assertEqual(lines, ['a ', '', ' b', '"// kept"', ''])

    def test_a_digit_separator_is_not_a_character_literal(self):
        self.assertEqual(kinds("int x = 0x1C'32A80; // rax"), [('address', "0x1C'32A80")])

    def test_character_and_raw_string_literals(self):
        self.assertEqual(kinds("char c = '\"'; int r8 = 0;"), [('register', 'r8')])
        self.assertEqual(kinds('auto s = R"x(/* not a comment */ rax)x";'), [('register', 'rax')])


class FindingTests(unittest.TestCase):
    def test_signature_strings(self):
        self.assertEqual(kinds('const char* s = "48 8B 05 ?? ?? ?? ??";'),
                         [('signature', '"48 8B 05 ?? ?? ?? ??"')])
        self.assertEqual(kinds('const char* s = "E8 ? ? ?";'), [('signature', '"E8 ? ? ?"')])
        self.assertEqual(kinds('const char* s = "48 8B 05"; auto t = "aa bb cc dd ee";'), [])

    def test_addresses_are_the_module_range_only(self):
        self.assertEqual(kinds('f(0x1CFE2B0); g(0x14001A2B0ull); h(0x100000);'),
                         [('address', '0x1CFE2B0'), ('address', '0x14001A2B0'), ('address', '0x100000')])
        # Masks, hashes and sizes: below a megabyte, or 32-bit constants from 0x10000000 up.
        self.assertEqual(kinds('a(0xFFFFF); b(0xFFFFFFFF); c(0xEDB88320u); d(0x7FFF0000); e(0x10000000);'), [])

    def test_offsets_by_name_or_on_a_pointer(self):
        self.assertEqual(kinds('inline constexpr std::size_t kViewOffset = 0x60;'), [('offset', 'kViewOffset')])
        self.assertEqual(kinds('constexpr std::uint32_t kInitRva{0x1A2B};'), [('offset', 'kInitRva')])
        self.assertEqual(kinds('auto* p = reinterpret_cast<float*>(base + 0x18);'), [('offset', '0x18')])
        self.assertEqual(kinds('std::uintptr_t at = object - 0x40;'), [('offset', '0x40')])
        # A hex number with no pointer on the line, or an offset named by a decimal, is not one.
        self.assertEqual(kinds('int mask = flags + 0x10; constexpr std::size_t kHeaderOffset = 16;'), [])

    def test_registers(self):
        self.assertEqual(kinds('regs.rbx = regs.rax; f(r15, r8d, xmm3, eax);'),
                         [('register', 'rbx'), ('register', 'rax'), ('register', 'r15'), ('register', 'r8d'),
                          ('register', 'xmm3'), ('register', 'eax')])
        self.assertEqual(kinds('int rax2 = 0; int raxes = 0; int r16 = 0; int xmm16 = 0;'), [])

    def test_type_names(self):
        self.assertEqual(kinds('find("idPlayer"); renderView_t v;'),
                         [('type', 'idPlayer'), ('type', 'renderView_t')])
        self.assertEqual(kinds('idTechToOpenXr(q); std::size_t n; std::uint32_t u; int idle = 0; id(x);'), [])

    def test_include_lines_are_skipped(self):
        self.assertEqual(kinds('#include "features/idPlayer_rax.hpp"\nint x = 0;'), [])


class FolderAndAllowTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = self.dir.name

    def tearDown(self):
        self.dir.cleanup()

    def write(self, rel, text):
        path = os.path.join(self.root, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
        return path

    def test_the_engine_folders_are_not_checked(self):
        for folder in ('src/vkcore', 'src/engine/eternal/resolver', 'src/game/eternal'):
            self.write(f'{folder}/a.cpp', 'int rax = 0x1CFE2B0;')
        self.write('src/features/menu/a.cpp', 'int rax = 0;')
        self.write('src/features/menu/notes.md', 'rax 0x1CFE2B0')
        self.write('tests/vkcore/a_tests.cpp', 'int rax = 0;')
        new, stale, count = cs.check(self.root, {})
        self.assertEqual(count, 1)
        self.assertEqual(new, [('src/features/menu/a.cpp', 1, 'register', 'rax')])
        self.assertEqual(stale, [])

    def test_an_allowed_exception_covers_its_file_only(self):
        self.write('src/stereo_seq/a.cpp', 'int x = 0x100000;\nint y = 0x100000;')
        self.write('src/stereo_seq/b.cpp', 'int x = 0x100000;')
        allow = cs.read_allow(self.write('allow.txt', '# comment\n\n'
                                         'src/stereo_seq/a.cpp address 0x100000: a size, not an address\n'))
        new, stale, _ = cs.check(self.root, allow)
        self.assertEqual(new, [('src/stereo_seq/b.cpp', 1, 'address', '0x100000')])
        self.assertEqual(stale, [])

    def test_an_entry_that_matches_nothing_is_stale(self):
        self.write('src/common/a.cpp', 'int x = 0;')
        allow = cs.read_allow(self.write('allow.txt', 'src/common/a.cpp register rax: the old hook\n'))
        new, stale, _ = cs.check(self.root, allow)
        self.assertEqual(new, [])
        self.assertEqual(stale, [('src/common/a.cpp', 'register', 'rax')])
        self.assertEqual(cs.main(['--root', self.root, '--allow', os.path.join(self.root, 'allow.txt')]), 1)

    def test_an_unreadable_allow_line_is_refused(self):
        for line in ('src/a.cpp rax: no kind\n', 'src/a.cpp colour 0x100000: unknown kind\n',
                     'src/a.cpp register rax\n'):
            path = self.write('allow.txt', line)
            with self.assertRaises(ValueError):
                cs.read_allow(path)
            self.assertEqual(cs.main(['--root', self.root, '--allow', path]), 2)

    def test_a_clean_tree_passes(self):
        self.write('src/common/a.cpp', 'int x = 0x10; // rax at 0x1CFE2B0\n')
        self.assertEqual(cs.main(['--root', self.root, '--allow', os.path.join(self.root, 'none.txt')]), 0)


if __name__ == '__main__':
    unittest.main()
