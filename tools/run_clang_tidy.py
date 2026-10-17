#!/usr/bin/env python3
"""Run clang-tidy over src/ and fail on any finding not in the baseline (docs/ARCHITECTURE.md, REQ-18).

Uses the checks in .clang-tidy and the compile commands of a configured CMake build (default: the
windows-msvc preset, build/windows-msvc/compile_commands.json; configure or build it first). Every
translation unit under src/ in that database is checked, with headers under src/ reported once.

Findings already accepted are listed in tools/clang_tidy_baseline.txt, one per line as
`<count> <file> [<check>] <message>`, without line numbers so that unrelated edits do not churn it. The
run fails (exit 1) when a file, check and message is seen more often than the baseline allows; a
finding that has gone is reported, and --update-baseline rewrites the file from this run. Fix new
findings rather than adding them to the baseline, unless the check is wrong for that code.

Exit codes: 0 no new findings, 1 new findings, 2 clang-tidy or the setup failed.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'tools', 'clang_tidy_baseline.txt')
DEFAULT_BUILD = os.path.join(ROOT, 'build', 'windows-msvc')
# winget's LLVM package installs here and does not always put itself on PATH.
WINDOWS_LLVM = r'C:\Program Files\LLVM\bin\clang-tidy.exe'
DIAGNOSTIC = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<level>warning|error): '
                        r'(?P<message>.*?) \[(?P<check>[^\]]+)\]$')


def find_clang_tidy(explicit):
    if explicit:
        return explicit
    on_path = shutil.which('clang-tidy')
    if on_path:
        return on_path
    if os.path.exists(WINDOWS_LLVM):
        return WINDOWS_LLVM
    return None


def relative_source(path, root=ROOT):
    """The repository-relative path with forward slashes, or None outside the repository."""
    full = os.path.normcase(os.path.abspath(path))
    base = os.path.normcase(os.path.abspath(root))
    if not full.startswith(base + os.sep):
        return None
    return os.path.relpath(os.path.abspath(path), root).replace(os.sep, '/')


def parse_findings(output, root=ROOT):
    """The findings in clang-tidy's output under src/, as (file, line, col, check, message) tuples."""
    found = set()
    for raw in output.splitlines():
        m = DIAGNOSTIC.match(raw.strip())
        if not m:
            continue
        rel = relative_source(m['file'], root)
        if rel is None or not rel.startswith('src/'):
            continue
        found.add((rel, int(m['line']), int(m['col']), m['check'], without_root(m['message'], root)))
    return found


def without_root(message, root=ROOT):
    """The message with paths inside the repository made relative and without their line and column (a
    lambda's name carries its place), so the baseline is the same in every checkout and after edits."""
    for base in {root, root.replace('\\', '/')}:
        message = re.sub(re.escape(base) + r'[\\/]([^\s:]+)(?::\d+)*', lambda m: m[1].replace('\\', '/'),
                         message, flags=re.IGNORECASE)
    return message


def count_keys(findings):
    """Baseline keys (file, check, message) with how often each occurs."""
    return collections.Counter((f, check, message) for f, _, _, check, message in findings)


def unit_failed(code, findings):
    """Whether a unit was not fully checked. Without warnings-as-errors clang-tidy exits non-zero only on an
    error or a crash: unless a compile error under src/ explains it (reported as one of its own), the unit
    stopped early (a crash after some findings, or an error in a header outside src/)."""
    return code != 0 and not any(f[3].startswith('clang-diagnostic-') for f in findings)


def tidy_version(clang_tidy):
    """The 'LLVM version X.Y.Z' clang-tidy reports, or None."""
    try:
        out = subprocess.run([clang_tidy, '--version'], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r'LLVM version (\S+)', out)
    return m[1] if m else None


def baseline_version(path):
    """The clang-tidy version the baseline was made with (its '# clang-tidy <version>' line), or None."""
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'^# clang-tidy (\S+)$', line.rstrip('\n'))
            if m:
                return m[1]
    return None


def read_baseline(path):
    counts = collections.Counter()
    if not os.path.exists(path):
        return counts
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            m = re.match(r'^(\d+) (\S+) \[([^\]]+)\] (.*)$', line)
            if not m:
                raise ValueError(f'{path}: unreadable line: {line}')
            counts[(m[2], m[3], m[4])] += int(m[1])
    return counts


def write_baseline(path, counts, version):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Accepted clang-tidy findings (tools/run_clang_tidy.py): <count> <file> [<check>] <message>.\n')
        f.write('# Regenerate with --update-baseline; fix new findings instead of adding them here.\n')
        f.write('# Another clang-tidy version reports other findings: compare with the one below.\n')
        f.write(f'# clang-tidy {version or "unknown"}\n')
        for (file, check, message), n in sorted(counts.items()):
            f.write(f'{n} {file} [{check}] {message}\n')


def compare(current, baseline):
    """(new, gone): the keys seen more often than the baseline allows, and those seen less often."""
    new = {k: n - baseline[k] for k, n in current.items() if n > baseline[k]}
    gone = {k: n - current[k] for k, n in baseline.items() if n > current[k]}
    return new, gone


def translation_units(build_dir, only):
    with open(os.path.join(build_dir, 'compile_commands.json'), encoding='utf-8') as f:
        entries = json.load(f)
    units = []
    for e in entries:
        path = e['file'] if os.path.isabs(e['file']) else os.path.join(e['directory'], e['file'])
        rel = relative_source(path)
        if rel is None or not rel.startswith('src/') or not rel.endswith('.cpp'):
            continue
        if only and not any(rel == o or rel.startswith(o.rstrip('/') + '/') for o in only):
            continue
        units.append(path)
    return sorted(set(units))


def run_one(clang_tidy, build_dir, path):
    header_filter = r'.*[/\\]src[/\\].*'
    # The build's warnings-as-errors (/WX) would turn clang's own compiler warnings into errors here; the
    # compilers already check those, so this run reports only the clang-tidy checks.
    cmd = [clang_tidy, '-p', build_dir, '--quiet', f'--header-filter={header_filter}', '--extra-arg=-Wno-error',
           path]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    return path, proc.returncode, proc.stdout + proc.stderr


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('paths', nargs='*', help='only these files or folders under src/ (default: all of src/)')
    parser.add_argument('-p', '--build-dir', default=DEFAULT_BUILD, help='the CMake build with compile_commands.json')
    parser.add_argument('-j', '--jobs', type=int, default=4, help='clang-tidy processes at once (default 4)')
    parser.add_argument('--clang-tidy', help='the clang-tidy executable (default: PATH, then LLVM\'s install folder)')
    parser.add_argument('--baseline', default=BASELINE, help='the accepted findings file')
    parser.add_argument('--update-baseline', action='store_true', help='rewrite the baseline from this run')
    args = parser.parse_args(argv)

    clang_tidy = find_clang_tidy(args.clang_tidy)
    if not clang_tidy:
        print('clang-tidy not found: install LLVM (winget install LLVM.LLVM) or pass --clang-tidy')
        return 2
    if not os.path.exists(os.path.join(args.build_dir, 'compile_commands.json')):
        print(f'No compile_commands.json in {args.build_dir}: configure the preset first (cmake --preset windows-msvc)')
        return 2
    only = [p.replace('\\', '/').rstrip('/') for p in args.paths]
    if args.update_baseline and only:
        print('--update-baseline needs a run over all of src/')
        return 2
    units = translation_units(args.build_dir, only)
    if not units:
        print('No translation unit under src/ matches')
        return 2
    version = tidy_version(clang_tidy)
    print(f'{clang_tidy}: LLVM {version or "version unknown"}')
    wanted = baseline_version(args.baseline)
    if not args.update_baseline and wanted and version != wanted:
        print(f'warning: the baseline was made with clang-tidy {wanted}; another version reports other findings '
              f'(install LLVM {wanted} or pass --clang-tidy)')

    findings = set()
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for path, code, output in pool.map(lambda u: run_one(clang_tidy, args.build_dir, u), units):
            got = parse_findings(output)
            findings |= got
            if unit_failed(code, got):
                failed.append((path, output.strip()))
    errors = sorted(f for f in findings if f[3].startswith('clang-diagnostic-'))
    for path, output in failed:
        print(f'clang-tidy failed on {relative_source(path)}:\n{output}')
    for f in errors:
        print(f'{f[0]}:{f[1]}:{f[2]}: compile error: {f[4]} [{f[3]}]')
    if failed or errors:
        return 2

    current = count_keys(findings)
    if args.update_baseline:
        write_baseline(args.baseline, current, version)
        print(f'{len(units)} file(s) checked; baseline rewritten with {sum(current.values())} finding(s)')
        return 0

    baseline = read_baseline(args.baseline)
    new, gone = compare(current, baseline)
    if only:
        gone = {}  # a partial run cannot tell a fixed finding from one in a file it did not check
    for f in sorted(findings):
        if (f[0], f[3], f[4]) in new:
            print(f'{f[0]}:{f[1]}:{f[2]}: {f[4]} [{f[3]}]')
    for (file, check, message), n in sorted(gone.items()):
        print(f'gone ({n}): {file} [{check}] {message}')
    print(f'{len(units)} file(s) checked, {sum(current.values())} finding(s), baseline {sum(baseline.values())}, '
          f'{sum(new.values())} new, {sum(gone.values())} gone' + (' (run --update-baseline)' if gone else ''))
    return 1 if new else 0


if __name__ == '__main__':
    sys.exit(main())
