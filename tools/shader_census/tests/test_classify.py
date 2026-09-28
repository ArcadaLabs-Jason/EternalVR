import os
import subprocess
import tempfile
import unittest

import census
import classify as C
import spirv as S
from tests import shaders

MEMBERS_VIEWPROJ = C.ViewMembers([{'set': 0, 'binding': 1, 'offset': 0}])


class ParseTests(unittest.TestCase):
    def test_rejects_non_spirv(self):
        with self.assertRaises(ValueError):
            S.Module(b'\x00' * 20)
        with self.assertRaises(ValueError):
            S.Module(b'\x03\x02\x23')

    def test_reads_big_endian_modules(self):
        from tests.spirv_builder import Builder
        b = Builder()
        b.var(S.SC_OUTPUT, b.vec(4), builtin=S.BUILTIN_POSITION)
        m = S.Module(b.build('vertex', swap=True))
        self.assertEqual(m.entry_points[0][0], 'vertex')
        self.assertEqual(m.entry_points[0][2], 'main')


class VertexTests(unittest.TestCase):
    def test_view_matrix_to_position_is_the_position_patch(self):
        r = C.classify(shaders.vertex())
        self.assertEqual(r['stages'], ['vertex'])
        self.assertTrue(r['writes_position'])
        self.assertTrue(r['position_from_view_constants'])
        self.assertIn('position_patch', r['classes'])
        self.assertFalse(r['needs_semantic_patch'])
        self.assertEqual(r['view_constant_reads'],
                         [{'storage': 'uniform', 'set': 0, 'binding': 1, 'offset': 0, 'kind': 'mat4x4'}])

    def test_gl_per_vertex_block_and_local_variable(self):
        r = C.classify(shaders.vertex(per_vertex_block=True, via_local=True))
        self.assertTrue(r['writes_position'])
        self.assertTrue(r['position_from_view_constants'])
        self.assertTrue(r['position_from_matrix_product'])

    def test_previous_clip_output_needs_a_semantic_patch(self):
        r = C.classify(shaders.vertex(prev_clip=True))
        self.assertEqual(r['clip_like_outputs'], 1)
        self.assertTrue(r['needs_semantic_patch'])
        self.assertIn('previous-clip', r['semantic_reasons'][0])
        offsets = sorted(x['offset'] for x in r['view_constant_reads'])
        self.assertEqual(offsets, [0, 64])

    def test_member_list_narrows_view_reads(self):
        r = C.classify(shaders.vertex(prev_clip=True), MEMBERS_VIEWPROJ)
        self.assertEqual([x['offset'] for x in r['view_constant_reads']], [0])
        self.assertEqual(r['view_constant_rule'], 'member list')

    def test_no_matrix_no_patch(self):
        r = C.classify(shaders.vertex_world_only())
        self.assertTrue(r['writes_position'])
        self.assertFalse(r['position_from_view_constants'])
        self.assertEqual(r['view_constant_reads'], [])
        self.assertEqual(r['classes'], [])


class FragmentComputeTests(unittest.TestCase):
    def test_fragment_matrix_read_is_semantic(self):
        r = C.classify(shaders.fragment_reads_matrix())
        self.assertEqual(r['stages'], ['fragment'])
        self.assertTrue(r['needs_semantic_patch'])
        self.assertEqual(r['semantic_reasons'], ['non-position stage reads view constants'])

    def test_fragment_matrix_outside_the_member_list_is_not(self):
        r = C.classify(shaders.fragment_reads_matrix(member=1), MEMBERS_VIEWPROJ)
        self.assertFalse(r['needs_semantic_patch'])

    def test_fragcoord_bin_lookup(self):
        r = C.classify(shaders.fragment_bin_lookup())
        self.assertEqual(r['fragcoord']['uses'], 1)
        self.assertTrue(r['fragcoord']['bin_lookup'])
        self.assertFalse(r['fragcoord']['screen_fetch'])
        self.assertEqual(r['semantic_reasons'], ['gl_FragCoord bin lookup'])

    def test_fragcoord_screen_fetch_is_not_semantic(self):
        r = C.classify(shaders.fragment_screen_fetch())
        self.assertTrue(r['fragcoord']['screen_fetch'])
        self.assertFalse(r['fragcoord']['bin_lookup'])
        self.assertIn('fragcoord_screen_fetch', r['classes'])
        self.assertIn('images_to_promote', r['classes'])
        self.assertFalse(r['needs_semantic_patch'])

    def test_compute_images(self):
        r = C.classify(shaders.compute_screen())
        self.assertEqual(r['stages'], ['compute'])
        self.assertTrue(r['global_invocation_id'])
        self.assertIn('compute_global_id', r['classes'])
        self.assertEqual(r['images']['storage_2d'], 1)
        self.assertEqual(r['images']['runtime_arrays'], 1)
        self.assertEqual(r['images']['sampled_2d'], 1)


class SdkValidationTests(unittest.TestCase):
    """The synthetic modules are valid SPIR-V (skipped without the Vulkan SDK)."""

    def test_spirv_val(self):
        tool = census.find_sdk_tool('spirv-val')
        if not tool:
            self.skipTest('spirv-val not found')
        with tempfile.TemporaryDirectory() as tmp:
            for name, data in (('v', shaders.vertex(prev_clip=True)),
                               ('vb', shaders.vertex(per_vertex_block=True, via_local=True)),
                               ('fm', shaders.fragment_reads_matrix()), ('fb', shaders.fragment_bin_lookup()),
                               ('fs', shaders.fragment_screen_fetch()), ('c', shaders.compute_screen())):
                path = os.path.join(tmp, name + '.spv')
                with open(path, 'wb') as f:
                    f.write(data)
                result = subprocess.run([tool, '--target-env', 'vulkan1.1', path], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, name + ': ' + result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
