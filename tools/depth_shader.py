#!/usr/bin/env python3
"""Make or check the depth layer's embedded SPIR-V (src/vkcore/presenter_depth_shader.hpp).

The GLSL is src/vkcore/shaders/depth_copy.comp. The words are what
`glslc --target-env=vulkan1.0 -O -o <tmp>.spv src/vkcore/shaders/depth_copy.comp` makes (the Vulkan SDK's glslc,
1.4.363 when they were made), checked with `spirv-val --target-env vulkan1.0`.

  python tools/depth_shader.py           check: compiles the GLSL and compares the words with the header
  python tools/depth_shader.py --write   writes the header from the GLSL

glslc and spirv-val are found on PATH or in the Vulkan SDK (VULKAN_SDK). Exit codes: 0 the words match (or were
written), 1 they differ, 2 no compiler or it failed.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLSL = os.path.join(ROOT, 'src', 'vkcore', 'shaders', 'depth_copy.comp')
HEADER = os.path.join(ROOT, 'src', 'vkcore', 'presenter_depth_shader.hpp')


def tool(name):
    found = shutil.which(name)
    if found:
        return found
    sdk = os.environ.get('VULKAN_SDK')
    for base in ([sdk] if sdk else []) + [os.path.join('C:\\VulkanSDK', d) for d in
                                          (os.listdir('C:\\VulkanSDK') if os.path.isdir('C:\\VulkanSDK') else [])]:
        path = os.path.join(base, 'Bin', name + '.exe')
        if os.path.isfile(path):
            return path
    return None


def compile_words():
    glslc, val = tool('glslc'), tool('spirv-val')
    if not glslc or not val:
        print('glslc or spirv-val not found (VULKAN_SDK, PATH)')
        sys.exit(2)
    with tempfile.TemporaryDirectory() as tmp:
        spv = os.path.join(tmp, 'depth_copy.spv')
        for cmd in ([glslc, '--target-env=vulkan1.0', '-O', '-o', spv, GLSL], [val, '--target-env', 'vulkan1.0', spv]):
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                print(' '.join(cmd), r.stdout, r.stderr)
                sys.exit(2)
        data = open(spv, 'rb').read()
    return list(struct.unpack('<%dI' % (len(data) // 4), data))


def header_words(text):
    body = text[text.index('kDepthCopySpirv[] = {'):text.index('};')]
    return [int(w, 16) for w in re.findall(r'0x[0-9a-fA-F]{8}', body)]


def header_text(words):
    lines = [
        '#pragma once',
        '',
        "// The depth layer's copy shader (presenter_depth_setup.cpp): SPIR-V 1.0 of src/vkcore/shaders/depth_copy.comp,",
        '// made with `glslc --target-env=vulkan1.0 -O` and checked with `spirv-val --target-env vulkan1.0` (Vulkan',
        '// SDK 1.4.363). Never edited by hand: `python tools/depth_shader.py --write` writes it from the GLSL, and',
        '// `python tools/depth_shader.py` checks that it matches.',
        '',
        '#include <cstdint>',
        '',
        'namespace evr::vkcore {',
        '',
        '// clang-format off',
        'inline constexpr std::uint32_t kDepthCopySpirv[] = {',
    ]
    for i in range(0, len(words), 8):
        lines.append('    ' + ', '.join('0x%08x' % w for w in words[i:i + 8]) + ',')
    lines += ['};', '// clang-format on', '', '} // namespace evr::vkcore', '']
    return '\n'.join(lines)


def main():
    words = compile_words()
    if '--write' in sys.argv:
        with open(HEADER, 'w', newline='\n') as f:
            f.write(header_text(words))
        print(f'wrote {len(words)} words to {HEADER}')
        return 0
    have = header_words(open(HEADER).read())
    if have != words:
        print(f'the header\'s {len(have)} words do not match the GLSL\'s {len(words)}: run with --write')
        return 1
    print(f'depth shader: the header matches the GLSL ({len(words)} words)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
