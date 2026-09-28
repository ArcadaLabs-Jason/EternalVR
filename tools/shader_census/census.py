#!/usr/bin/env python3
"""Stereo shader census over a layer dump (ETERNALVR_DUMP_SHADERS; README.md in this folder).

    python census.py <dump dir> [--out DIR] [--frames FIRST:LAST] [--view-members FILE] [--disasm]

Classifies every module in <dump dir>/modules (classify.py), joins the pipeline index and the draw log,
and writes census.json and census.md to --out (default: <dump dir>/census). The T-105 shares are the
modules needing a semantic patch over the distinct modules bound by draws, and the draws whose pipeline
holds such a module over all draws; dispatches are reported the same way beside them.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import classify as C  # noqa: E402

THRESHOLD = 15.0
SDK_BIN_DEFAULT = r'E:\VulkanSDK\1.4.357.0\Bin'


def find_sdk_tool(name):
    """A Vulkan SDK tool (spirv-dis, spirv-cross, ...) from VULKAN_SDK, the rig's SDK or PATH; or None."""
    exe = name + ('.exe' if os.name == 'nt' else '')
    for base in (os.environ.get('VULKAN_SDK'), SDK_BIN_DEFAULT):
        if not base:
            continue
        for candidate in (os.path.join(base, 'Bin', exe), os.path.join(base, exe)):
            if os.path.isfile(candidate):
                return candidate
    return shutil.which(name)


def read_jsonl(path):
    if not os.path.isfile(path):
        return []
    out = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    pass  # a line cut off by a crash or a kill
    return out


def module_dir(dump):
    sub = os.path.join(dump, 'modules')
    return sub if os.path.isdir(sub) else dump


def classify_modules(folder, view_members):
    modules = {}
    for path in sorted(glob.glob(os.path.join(folder, '*.spv'))):
        name = os.path.splitext(os.path.basename(path))[0]
        with open(path, 'rb') as f:
            data = f.read()
        try:
            modules[name] = C.classify(data, view_members)
        except (ValueError, IndexError, KeyError) as e:
            modules[name] = {'error': str(e), 'needs_semantic_patch': False, 'stages': [], 'classes': [],
                             'semantic_reasons': []}
    return modules


def pipeline_modules(pipelines):
    """pipeline handle -> set of module names, libraries included (last definition of a handle wins)."""
    direct, libs = {}, {}
    for p in pipelines:
        direct[p['pipeline']] = {s['module'] for s in p.get('stages', [])}
        libs[p['pipeline']] = p.get('libraries', [])

    def resolve(handle, depth=0):
        mods = set(direct.get(handle, set()))
        if depth < 8:
            for lib in libs.get(handle, []):
                mods |= resolve(lib, depth + 1)
        return mods

    return {h: resolve(h) for h in direct}


def parse_frames(text):
    if not text:
        return None
    first, _, last = text.partition(':')
    return int(first or 0), int(last) if last else None


def count_commands(drawlog, frames):
    draws, dispatches, seen_frames = {}, {}, set()
    for e in drawlog:
        f = e.get('f', 0)
        if frames and (f < frames[0] or (frames[1] is not None and f > frames[1])):
            continue
        kind = e.get('e', '')
        if kind.startswith('draw'):
            draws[e.get('p')] = draws.get(e.get('p'), 0) + 1
            seen_frames.add(f)
        elif kind.startswith('dispatch'):
            dispatches[e.get('p')] = dispatches.get(e.get('p'), 0) + 1
            seen_frames.add(f)
    return draws, dispatches, seen_frames


def share(part, whole):
    return round(100.0 * part / whole, 2) if whole else 0.0


def summarize(modules, pipe_mods, counts):
    """Shares for one command kind (draws or dispatches)."""
    bound, weighted, semantic_cmds, total, unknown_pipes = set(), {}, 0, 0, 0
    for handle, n in counts.items():
        total += n
        mods = pipe_mods.get(handle)
        if mods is None:
            unknown_pipes += n
            continue
        bound |= mods
        if any(modules.get(m, {}).get('needs_semantic_patch') for m in mods):
            semantic_cmds += n
        for m in mods:
            weighted[m] = weighted.get(m, 0) + n
    known = {m for m in bound if m in modules}
    semantic = sorted(m for m in known if modules[m]['needs_semantic_patch'])
    return {
        'commands': total,
        'commands_unknown_pipeline': unknown_pipes,
        'modules_bound': len(known),
        'modules_bound_unresolved': sorted(bound - known),
        'modules_semantic': len(semantic),
        'module_share_pct': share(len(semantic), len(known)),
        'commands_semantic': semantic_cmds,
        'command_share_pct': share(semantic_cmds, total),
        'semantic_modules': semantic,
        'commands_per_module': dict(sorted(weighted.items(), key=lambda kv: -kv[1])),
    }


def class_counts(modules, names):
    out = {}
    for name in names:
        m = modules.get(name)
        if not m:
            continue
        for key in m.get('classes', []) + ['reason: ' + r for r in m.get('semantic_reasons', [])]:
            out[key] = out.get(key, 0) + 1
        for stage in m.get('stages', []):
            out['stage: ' + stage] = out.get('stage: ' + stage, 0) + 1
    return dict(sorted(out.items()))


def build(dump, frames=None, view_members=None):
    modules = classify_modules(module_dir(dump), view_members)
    pipe_mods = pipeline_modules(read_jsonl(os.path.join(dump, 'pipelines.jsonl')))
    draws, dispatches, seen = count_commands(read_jsonl(os.path.join(dump, 'drawlog.jsonl')), frames)
    d, c = summarize(modules, pipe_mods, draws), summarize(modules, pipe_mods, dispatches)
    bound = set().union(*(pipe_mods.get(h, set()) for h in draws)) if draws else set()
    reweigh = d['module_share_pct'] > THRESHOLD or d['command_share_pct'] > THRESHOLD
    return {
        'dump': os.path.abspath(dump),
        'frames': sorted(seen),
        'view_constant_rule': 'member list' if view_members is not None else 'any matrix (upper bound)',
        'modules_total': len(modules),
        'modules_semantic_total': sum(1 for m in modules.values() if m['needs_semantic_patch']),
        'pipelines_indexed': len(pipe_mods),
        'draws': d,
        'dispatches': c,
        'classes_all_modules': class_counts(modules, modules),
        'classes_draw_modules': class_counts(modules, bound),
        't105_reweigh_sequential': reweigh if draws else None,
        'modules': modules,
    }


def markdown(r):
    d, c = r['draws'], r['dispatches']
    frames = r['frames']
    lines = ['# Stereo shader census', '',
             'Dump: `%s`. Frames: %s. View-constant rule: %s.' % (
                 r['dump'], '%d (%d to %d)' % (len(frames), frames[0], frames[-1]) if frames else 'none',
                 r['view_constant_rule']), '',
             '| | Draws | Dispatches |', '|---|---|---|']
    for label, key in (('Commands', 'commands'), ('Commands with an unindexed pipeline', 'commands_unknown_pipeline'),
                       ('Distinct modules bound', 'modules_bound'), ('Modules needing a semantic patch',
                                                                     'modules_semantic'),
                       ('Module share (%)', 'module_share_pct'), ('Commands with such a module', 'commands_semantic'),
                       ('Command share (%)', 'command_share_pct')):
        lines.append('| %s | %s | %s |' % (label, d[key], c[key]))
    verdict = r['t105_reweigh_sequential']
    lines += ['', 'All modules: %d, of which %d need a semantic patch. Pipelines indexed: %d.' % (
        r['modules_total'], r['modules_semantic_total'], r['pipelines_indexed']), '',
        'T-105 (15.0%% on draws): %s' % ('no draw log' if verdict is None else
                                          'reweigh synchronized sequential' if verdict else 'multiview holds'),
        '', '## Classes (modules bound by draws)', '']
    lines += ['- %s: %d' % kv for kv in r['classes_draw_modules'].items()] or ['- none']
    lines += ['', '## Classes (all modules)', '']
    lines += ['- %s: %d' % kv for kv in r['classes_all_modules'].items()] or ['- none']
    lines += ['', '## Semantic modules bound by draws', '']
    for name in d['semantic_modules']:
        lines.append('- `%s` (%d draws): %s' % (name, d['commands_per_module'].get(name, 0),
                                                '; '.join(r['modules'][name]['semantic_reasons'])))
    return '\n'.join(lines) + '\n'


def disassemble(dump, names, out):
    tool = find_sdk_tool('spirv-dis')
    if not tool:
        print('spirv-dis not found; skipping disassembly')
        return
    folder = os.path.join(out, 'disasm')
    os.makedirs(folder, exist_ok=True)
    for name in names:
        src = os.path.join(module_dir(dump), name + '.spv')
        if os.path.isfile(src):
            subprocess.run([tool, src, '-o', os.path.join(folder, name + '.spvasm')], check=False)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump')
    ap.add_argument('--out')
    ap.add_argument('--frames', help='FIRST:LAST frame numbers of the draw log to count (inclusive)')
    ap.add_argument('--view-members', help='JSON list of {"set","binding","offset"[,"size"]} or '
                                           '{"storage":"push","offset"} view-constant members')
    ap.add_argument('--disasm', action='store_true', help='spirv-dis every semantic module (Vulkan SDK)')
    args = ap.parse_args(argv)
    members = None
    if args.view_members:
        with open(args.view_members, encoding='utf-8') as f:
            members = C.ViewMembers(json.load(f))
    report = build(args.dump, parse_frames(args.frames), members)
    out = args.out or os.path.join(args.dump, 'census')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'census.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    with open(os.path.join(out, 'census.md'), 'w', encoding='utf-8') as f:
        f.write(markdown(report))
    if args.disasm:
        disassemble(args.dump, sorted(n for n, m in report['modules'].items() if m['needs_semantic_patch']), out)
    d = report['draws']
    print('modules %d, bound by draws %d, semantic %d (%.2f%%), draws %d, semantic draws %d (%.2f%%)' % (
        report['modules_total'], d['modules_bound'], d['modules_semantic'], d['module_share_pct'],
        d['commands'], d['commands_semantic'], d['command_share_pct']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
