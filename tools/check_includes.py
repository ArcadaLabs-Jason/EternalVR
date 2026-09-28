#!/usr/bin/env python3
"""Check that every source file includes the standard headers for the std:: names it uses.

libc++ (macOS) pulls many headers in transitively, so a missing include can build locally and fail on
MSVC or libstdc++. Headers included directly from our own project headers count as provided.
Heuristic, not a compiler: it knows the common std:: names only. Exit code 1 lists the gaps.
"""
import glob, os, re, sys

M={r'std::array\b':'array',r'std::vector\b':'vector',r'std::string\b':'string',r'std::to_string\b':'string',r'std::string_view\b':'string_view',
r'std::optional\b|std::nullopt\b':'optional',r'std::span\b':'span',r'std::bitset\b':'bitset',r'std::map\b':'map',r'std::unordered_map\b':'unordered_map',
r'std::(find|find_if|find_if_not|min|max|clamp|sort|any_of|all_of|none_of|count_if|transform|equal|search|copy|fill|reverse|lower_bound|remove_if|minmax)\b':'algorithm',
r'std::(sqrt|abs|fabs|sin|cos|tan|atan|atan2|asin|acos|hypot|floor|ceil|round|fmod|pow|isfinite|isnan|copysign|lerp)\b':'cmath',
r'std::u?int(8|16|32|64)_t\b':'cstdint',r'std::size_t\b|std::byte\b|std::ptrdiff_t\b':'cstddef',r'std::(move|pair|swap|exchange|forward|as_const)\b':'utility',
r'std::function\b':'functional',r'std::numeric_limits\b':'limits',r'std::variant\b|std::get_if\b|std::visit\b':'variant',r'std::memcpy\b|std::strlen\b':'cstring',
r'std::initializer_list\b':'initializer_list',r'std::(unique_ptr|make_unique|shared_ptr)\b':'memory',r'std::tuple\b':'tuple',r'std::chrono\b':'chrono',
r'std::(is_same_v|enable_if_t|underlying_type_t|is_integral_v|is_floating_point_v|is_trivially_copyable_v|remove_cvref_t|conditional_t|is_enum_v)\b':'type_traits',
r'std::(ostream|ostringstream|stringstream|istringstream)\b':'sstream',r'std::(accumulate|iota)\b':'numeric',r'std::(to_integer)\b':'cstddef',r'std::set\b':'set',r'std::(errc|from_chars|to_chars)\b':'charconv',r'std::array<':'array'}


def includes(path):
    text = open(path).read()
    return text, set(re.findall(r'#include <([a-z_]+)>', text)), re.findall(r'#include "([^"]+)"', text)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    gaps = 0
    for path in sorted(glob.glob('src/**/*.[ch]pp', recursive=True) + glob.glob('tests/**/*.[ch]pp', recursive=True)):
        text, have, project = includes(path)
        for header in project:
            for base in ('src', 'tests'):
                candidate = os.path.join(base, header)
                if os.path.exists(candidate):
                    have |= includes(candidate)[1]
        code = re.sub(r'//.*', '', text)
        need = {h for pattern, h in M.items() if re.search(pattern, code)}
        if 'sstream' in need and not re.search(r'std::(ostringstream|stringstream|istringstream)', code):
            need.discard('sstream')
        missing = sorted(need - have)
        if missing:
            gaps += 1
            print(f"{path}: missing {', '.join('<' + m + '>' for m in missing)}")
    return 1 if gaps else 0


if __name__ == '__main__':
    sys.exit(main())
