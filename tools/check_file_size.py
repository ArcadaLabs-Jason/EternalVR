#!/usr/bin/env python3
"""Check that no source file is longer than 600 lines (docs/ARCHITECTURE.md, REQ-18).

Covers the layer and its tests (C++) and the launcher (C#). A file past the limit is split by
responsibility, never by trimming comments. Exit code 1 lists the files over the limit.
"""
import os
import subprocess
import sys

LIMIT = 600
PATTERNS = ['src/*.cpp', 'src/*.hpp', 'tests/*.cpp', 'tests/*.hpp', 'launcher/*.cs']


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    files = subprocess.run(['git', 'ls-files', '--', *PATTERNS], cwd=root, capture_output=True, text=True,
                           check=True).stdout.split()
    over = []
    for path in files:
        with open(os.path.join(root, path), encoding='utf-8', errors='replace') as f:
            lines = sum(1 for _ in f)
        if lines > LIMIT:
            over.append((lines, path))
    for lines, path in sorted(over, reverse=True):
        print(f'{path}: {lines} lines (limit {LIMIT})')
    print(f'{len(files)} file(s) checked, {len(over)} over {LIMIT} lines')
    return 1 if over else 0


if __name__ == '__main__':
    sys.exit(main())
