import json
import os
import tempfile
import unittest

import census
from tests import shaders


def write_dump(folder, modules, pipelines, drawlog):
    os.makedirs(os.path.join(folder, 'modules'))
    for name, data in modules.items():
        with open(os.path.join(folder, 'modules', name + '.spv'), 'wb') as f:
            f.write(data)
    for fname, rows in (('pipelines.jsonl', pipelines), ('drawlog.jsonl', drawlog)):
        with open(os.path.join(folder, fname), 'w', encoding='utf-8') as f:
            for row in rows:
                f.write(json.dumps(row) + '\n')
            if fname == 'drawlog.jsonl':
                f.write('{"f":9,"e":"dr')  # a line cut off when the game was stopped


def stage(module, bits):
    return {'stage': bits, 'module': module, 'entry': 'main'}


class CensusTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dump = self.tmp.name
        modules = {
            'aaaa000000000001': shaders.vertex(),
            'aaaa000000000002': shaders.vertex(prev_clip=True),
            'aaaa000000000003': shaders.fragment_screen_fetch(),
            'aaaa000000000004': shaders.fragment_bin_lookup(),
            'aaaa000000000005': shaders.compute_screen(),
            'aaaa000000000006': shaders.fragment_reads_matrix(),  # never drawn
        }
        pipelines = [
            {'pipeline': '0x10', 'kind': 'graphics', 'stages': [stage('aaaa000000000001', 1),
                                                                stage('aaaa000000000003', 16)]},
            {'pipeline': '0x11', 'kind': 'graphics', 'stages': [stage('aaaa000000000002', 1),
                                                                stage('aaaa000000000004', 16)]},
            # A pipeline linked from libraries: its modules come from them.
            {'pipeline': '0x12', 'kind': 'graphics', 'stages': [], 'libraries': ['0x10']},
            {'pipeline': '0x20', 'kind': 'compute', 'stages': [stage('aaaa000000000005', 32)]},
            {'pipeline': '0x30', 'kind': 'graphics', 'stages': [stage('aaaa000000000006', 16)]},
        ]
        drawlog = []
        for frame in (0, 1):
            drawlog += [{'f': frame, 'cb': '0x1', 'e': 'pipe', 'bp': 0, 'p': '0x10'}]
            drawlog += [{'f': frame, 'cb': '0x1', 'e': 'drawIndexed', 'p': '0x10', 'n': 3, 'inst': 1}] * 6
            drawlog += [{'f': frame, 'cb': '0x1', 'e': 'draw', 'p': '0x11', 'n': 3, 'inst': 1}] * 3
            drawlog += [{'f': frame, 'cb': '0x1', 'e': 'drawIndirect', 'p': '0x12', 'count': 1}]
            drawlog += [{'f': frame, 'cb': '0x2', 'e': 'dispatch', 'p': '0x20', 'g': [8, 8, 1]}] * 2
            drawlog += [{'f': frame, 'cb': '0x2', 'e': 'draw', 'p': '0x99', 'n': 3, 'inst': 1}]
        write_dump(self.dump, modules, pipelines, drawlog)

    def tearDown(self):
        self.tmp.cleanup()

    def test_shares_follow_t105(self):
        r = census.build(self.dump)
        d = r['draws']
        self.assertEqual(r['frames'], [0, 1])
        self.assertEqual(r['modules_total'], 6)
        self.assertEqual(d['commands'], 22)
        self.assertEqual(d['commands_unknown_pipeline'], 2)
        self.assertEqual(d['modules_bound'], 4)  # the never-drawn module is not in the denominator
        self.assertEqual(d['modules_semantic'], 2)  # previous-clip vertex and bin-lookup fragment
        self.assertEqual(d['module_share_pct'], 50.0)
        self.assertEqual(d['commands_semantic'], 6)
        self.assertEqual(d['command_share_pct'], round(100 * 6 / 22, 2))
        self.assertEqual(d['commands_per_module']['aaaa000000000001'], 14)  # 12 direct + 2 via the library
        self.assertTrue(r['t105_reweigh_sequential'])
        c = r['dispatches']
        self.assertEqual((c['commands'], c['modules_bound'], c['modules_semantic']), (4, 1, 0))
        self.assertEqual(r['classes_draw_modules']['reason: gl_FragCoord bin lookup'], 1)

    def test_frame_filter(self):
        r = census.build(self.dump, frames=(1, 1))
        self.assertEqual(r['frames'], [1])
        self.assertEqual(r['draws']['commands'], 11)

    def test_cli_writes_reports(self):
        out = os.path.join(self.dump, 'out')
        self.assertEqual(census.main([self.dump, '--out', out]), 0)
        with open(os.path.join(out, 'census.json'), encoding='utf-8') as f:
            self.assertIn('modules', json.load(f))
        with open(os.path.join(out, 'census.md'), encoding='utf-8') as f:
            text = f.read()
        self.assertIn('reweigh synchronized sequential', text)
        self.assertIn('aaaa000000000004', text)

    def test_member_list_from_a_live_match(self):
        members = os.path.join(self.dump, 'members.json')
        with open(members, 'w', encoding='utf-8') as f:
            json.dump([{'set': 0, 'binding': 1, 'offset': 0}], f)
        out = os.path.join(self.dump, 'out2')
        census.main([self.dump, '--out', out, '--view-members', members])
        with open(os.path.join(out, 'census.json'), encoding='utf-8') as f:
            r = json.load(f)
        self.assertEqual(r['view_constant_rule'], 'member list')
        # The previous-clip output is still a clip-space varying, whatever the member list says.
        self.assertIn('aaaa000000000002', r['draws']['semantic_modules'])

    def test_modules_folder_without_logs(self):
        r = census.build(os.path.join(self.dump, 'modules'))
        self.assertEqual(r['modules_total'], 6)
        self.assertIsNone(r['t105_reweigh_sequential'])
        self.assertIn('no draw log', census.markdown(r))


if __name__ == '__main__':
    unittest.main()
