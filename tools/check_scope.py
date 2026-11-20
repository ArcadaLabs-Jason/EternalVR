#!/usr/bin/env python3
"""Check that engine internals stay in the engine folders (docs/SCOPE.md REQ-18, docs/ARCHITECTURE.md section 15).

Engine internals are the parts of the code tied to the game's binary: where its code is, how its memory is
laid out, what its hooks' registers hold and what its classes are called. They belong in src/vkcore,
src/engine/eternal and src/game/eternal; the rest of src/ stays generic and takes them as plain values
(DECISIONS T-236). In the code of every other C++ file under src/ (comments and #include lines are
skipped, string literals are not) the check fails on:

- signature: a byte-pattern string, four or more hex bytes or ?? wildcards apart by single spaces, with a
  digit or a wildcard among them ("48 8B 05 ?? ?? ?? ??"), the way the hooks find the game's code;
- address: a hex literal in the range of the game module's RVAs past its first megabyte (0x100000 to
  0xFFFFFFF) or of its addresses at the default image base (0x140000000 to 0x14FFFFFFF); masks, hashes and
  other 32-bit constants from 0x10000000 up are not addresses;
- offset: a hex literal named as an offset, an RVA or a vtable entry (kViewOffset = 0x60), or added to or
  taken from a pointer or an address on a line that casts or holds one (reinterpret_cast<...>(p + 0x18));
- register: an x64 register's name in the code (rax, ebx, r8, xmm0), a mid hook's register contract;
- type: an id Tech class name (idPlayer, idCVar) or an engine struct name (renderView_t).

Cvar names are not engine internals here: they are the game's own console interface, set and read by name
through the engine's cvar system, and each feature lists the ones it holds next to the policy that holds
them. Game-design data (actions, bindings, dimensions) has no form a script can tell from other numbers and
names, so it is left to review.

Justified exceptions are listed in tools/scope_allow.txt, one per line as `<path> <kind> <text>: <reason>`;
`text` is what the finding shows (the literal, the name), and the entry covers it everywhere in that file.
An entry that matches nothing fails the check too, so the list stays current.

Exit codes: 0 no findings, 1 findings or stale entries, 2 the allow-list cannot be read.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW = os.path.join(ROOT, 'tools', 'scope_allow.txt')
ENGINE_FOLDERS = ('src/vkcore/', 'src/engine/eternal/', 'src/game/eternal/')
SOURCE_SUFFIXES = ('.cpp', '.hpp', '.h', '.inl', '.cpp.in', '.hpp.in')
KINDS = ('signature', 'address', 'offset', 'register', 'type')

# The game module's RVAs (its image is under 256 MB) and its addresses at the default image base.
ADDRESS_RANGES = ((0x100000, 0xFFFFFFF), (0x140000000, 0x14FFFFFFF))

HEX = r"0[xX][0-9A-Fa-f][0-9A-Fa-f']*"
SIGNATURE = re.compile(r'"(?:[0-9A-Fa-f]{2}|\?\??)(?: (?:[0-9A-Fa-f]{2}|\?\??)){3,}"')
HEX_LITERAL = re.compile(r'(?<![\w.])(' + HEX + r')(?:[uU]?[lL]{0,2}|[lL]{1,2}[uU]?)\b')
NAMED_OFFSET = re.compile(r'\b(\w*(?:Offset|offset|OFFSET|Rva|RVA|rva|Vtable|VTable|vtable|Vft|vft)\w*)'
                          r'\s*(?:=|\{)\s*' + HEX)
POINTER_LINE = re.compile(r'reinterpret_cast|\bstd::byte\s*\*|\buintptr_t\b|\bchar\s*\*|\bvoid\s*\*')
POINTER_STEP = re.compile(r'[+-]\s*(' + HEX + r')')
REGISTER = re.compile(r'\b(r[abcd]x|r[sd]i|r[bs]p|r(?:8|9|1[0-5])[dwb]?|e[abcd]x|e[sd]i|e[bs]p|'
                      r'xmm(?:[0-9]|1[0-5])|ymm(?:[0-9]|1[0-5]))\b')
TYPE_NAME = re.compile(r'\b(id(?!Tech)[A-Z][a-z]\w*|[a-z][a-z0-9]*[A-Z]\w*_t)\b')


def code_lines(text):
    """The text with comments blanked (newlines kept, so line numbers hold) and string literals kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            end = text.find('\n', i)
            i = n if end < 0 else end
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append('\n' * text.count('\n', i, end))
            i = end
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            open_paren = text.find('(', i + 2)
            delimiter = text[i + 2:open_paren] if open_paren >= 0 else ''
            end = text.find(')' + delimiter + '"', open_paren + 1) if open_paren >= 0 else -1
            end = n if end < 0 else end + len(delimiter) + 2
            out.append(text[i:end])
            i = end
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            # A quote after a letter or digit is a digit separator (0x1C'32A80), not a character literal.
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out).split('\n')


def hex_value(literal):
    return int(literal.replace("'", ''), 16)


def findings_in(text):
    """(line, kind, text) for every engine internal in a file's code."""
    found = []
    for number, line in enumerate(code_lines(text), 1):
        if line.lstrip().startswith('#include'):
            continue
        for m in SIGNATURE.finditer(line):
            if re.search(r'[0-9?]', m[0]):  # "aa bb cc dd" is more likely words than code bytes
                found.append((number, 'signature', m[0]))
        for m in HEX_LITERAL.finditer(line):
            if any(low <= hex_value(m[1]) <= high for low, high in ADDRESS_RANGES):
                found.append((number, 'address', m[1]))
        for m in NAMED_OFFSET.finditer(line):
            found.append((number, 'offset', m[1]))
        if POINTER_LINE.search(line):
            for m in POINTER_STEP.finditer(line):
                found.append((number, 'offset', m[1]))
        for m in REGISTER.finditer(line):
            found.append((number, 'register', m[1]))
        for m in TYPE_NAME.finditer(line):
            found.append((number, 'type', m[1]))
    return found


def in_scope(rel):
    return rel.startswith('src/') and rel.endswith(SOURCE_SUFFIXES) and not rel.startswith(ENGINE_FOLDERS)


def source_files(root):
    files = []
    for folder, _, names in os.walk(os.path.join(root, 'src')):
        for name in names:
            rel = os.path.relpath(os.path.join(folder, name), root).replace(os.sep, '/')
            if in_scope(rel):
                files.append(rel)
    return sorted(files)


def read_allow(path):
    """{(path, kind, text): reason}; raises ValueError on a line it cannot read."""
    entries = {}
    if not os.path.exists(path):
        return entries
    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            m = re.match(r'^(\S+) (\S+) (.+?): (\S.*)$', line)
            if not m or m[2] not in KINDS:
                raise ValueError(f'{path}: unreadable line: {line}')
            entries[(m[1], m[2], m[3])] = m[4]
    return entries


def check(root, allow):
    """(findings not allowed, allow-list entries that matched nothing, files checked)."""
    new = []
    used = set()
    files = source_files(root)
    for rel in files:
        with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as f:
            text = f.read()
        for number, kind, what in findings_in(text):
            key = (rel, kind, what)
            if key in allow:
                used.add(key)
            else:
                new.append((rel, number, kind, what))
    stale = sorted(k for k in allow if k not in used)
    return new, stale, len(files)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--root', default=ROOT, help='the repository root')
    parser.add_argument('--allow', default=ALLOW, help='the justified exceptions file')
    args = parser.parse_args(argv)
    try:
        allow = read_allow(args.allow)
    except (OSError, ValueError) as e:
        print(e)
        return 2
    new, stale, count = check(args.root, allow)
    for rel, number, kind, what in new:
        print(f'{rel}:{number}: [{kind}] {what}: engine internals belong in src/vkcore, src/engine/eternal '
              f'or src/game/eternal')
    for rel, kind, what in stale:
        print(f'{os.path.relpath(args.allow, args.root)}: {rel} {kind} {what} matches nothing: remove it')
    print(f'{count} file(s) checked outside the engine folders, {len(new)} finding(s), '
          f'{len(allow) - len(stale)} allowed exception(s), {len(stale)} stale')
    return 1 if new or stale else 0


if __name__ == '__main__':
    sys.exit(main())
