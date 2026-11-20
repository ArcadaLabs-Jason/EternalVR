#!/usr/bin/env python3
"""Download the clang-tidy that tools/clang_tidy_baseline.txt was made with, check it, and unpack only what it needs.

CI's clang-tidy job (.github/workflows/ci.yml) uses it, because the hosted runner's own LLVM is another version and
another version reports other findings. LLVM's release archive for 64-bit Windows is checked against the SHA256
below before anything is unpacked, and only bin/clang-tidy.exe and clang's own headers (lib/clang/<major>/include,
which every translation unit needs) are written. Needs the zstandard package (pip install zstandard==0.25.0).

A new LLVM version means a new archive name, checksum (GitHub shows it with the release's assets) and baseline,
in one change: the script refuses to run when its version and the baseline's '# clang-tidy' line differ.

Exit codes: 0 done (prints the path of clang-tidy.exe), 1 the download did not match its checksum, 2 another failure.
"""
import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_clang_tidy  # noqa: E402

VERSION = '23.1.2'
ARCHIVE = f'clang+llvm-{VERSION}-x86_64-pc-windows-msvc.tar.zst'
URL = f'https://github.com/llvm/llvm-project/releases/download/llvmorg-{VERSION}/{ARCHIVE}'
SHA256 = 'ceaee048142fece144752c6f6431cb0905a7a6160f78ab8cf5cf0b6216f99418'


def wanted(member, version=VERSION):
    """The path under the output folder for an archive member clang-tidy needs, or None. Members sit under one
    top folder (clang+llvm-<version>-<target>/), which is dropped."""
    parts = member.replace('\\', '/').split('/', 1)
    if len(parts) < 2 or '..' in parts[1].split('/'):
        return None
    rel = parts[1]
    headers = f'lib/clang/{version.split(".")[0]}/include/'
    if rel == 'bin/clang-tidy.exe' or (rel.startswith(headers) and len(rel) > len(headers)):
        return rel
    return None


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def download(url, path):
    with urllib.request.urlopen(url, timeout=120) as response, open(path, 'wb') as out:
        shutil.copyfileobj(response, out, 1 << 20)


def unpack(archive, out_dir, version=VERSION):
    """Writes the members wanted() keeps from the .tar.zst into out_dir; returns how many."""
    import zstandard  # only needed here, so the tests run without it

    count = 0
    # LLVM compresses with long-distance matching: a window past zstandard's default limit of 128 MiB.
    decompressor = zstandard.ZstdDecompressor(max_window_size=1 << 31)
    with open(archive, 'rb') as raw, decompressor.stream_reader(raw) as stream, \
            tarfile.open(fileobj=stream, mode='r|') as tar:
        for member in tar:
            rel = wanted(member.name, version)
            if rel is None or not member.isfile():
                continue
            target = os.path.join(out_dir, *rel.split('/'))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with tar.extractfile(member) as src, open(target, 'wb') as dst:
                shutil.copyfileobj(src, dst, 1 << 20)
            count += 1
    return count


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', required=True, help='the folder to unpack into (bin\\clang-tidy.exe lands there)')
    parser.add_argument('--archive', help='an archive already downloaded (it is checked all the same)')
    args = parser.parse_args(argv)

    baseline = run_clang_tidy.baseline_version(run_clang_tidy.BASELINE)
    if baseline != VERSION:
        print(f'The baseline was made with clang-tidy {baseline}, this script fetches {VERSION}: change both together')
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        archive = args.archive
        if not archive:
            archive = os.path.join(tmp, ARCHIVE)
            for attempt in (1, 2):  # one retry: a hosted runner's download can drop now and then
                print(f'Downloading {URL}')
                try:
                    download(URL, archive)
                    break
                except OSError as e:
                    print(f'Download failed: {e}')
                    if attempt == 2:
                        return 2
        got = sha256_of(archive)
        if got != SHA256:
            print(f'{ARCHIVE}: SHA256 {got}, expected {SHA256}; nothing unpacked')
            return 1
        print(f'{ARCHIVE}: SHA256 matches')
        count = unpack(archive, args.out)
    exe = os.path.join(args.out, 'bin', 'clang-tidy.exe')
    if not os.path.exists(exe):
        print(f'{ARCHIVE} has no bin/clang-tidy.exe')
        return 2
    print(f'{count} file(s) unpacked; clang-tidy {run_clang_tidy.tidy_version(exe) or "version unknown"}: {exe}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
