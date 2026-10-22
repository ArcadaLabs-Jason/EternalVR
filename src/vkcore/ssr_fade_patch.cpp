#include "vkcore/ssr_fade_patch.hpp"

#include "vkcore/dump_format.hpp"

#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <utility>

namespace evr::vkcore::ssr_fade {

namespace {

// SPIR-V opcodes and GLSL.std.450 instructions the patch reads or writes.
enum Op : std::uint32_t {
    kExtInstImport = 11,
    kExtInst = 12,
    kTypeFloat = 22,
    kTypeVector = 23,
    kTypeMatrix = 24,
    kTypePointer = 32,
    kConstant = 43,
    kVariable = 59,
    kLoad = 61,
    kAccessChain = 65,
    kVectorShuffle = 79,
    kCompositeConstruct = 80,
    kCompositeExtract = 81,
    kCompositeInsert = 82,
    kFNegate = 127,
    kFAdd = 129,
    kFSub = 131,
    kFMul = 133,
    kFDiv = 136,
    kDot = 148,
};
constexpr std::uint32_t kGlslFAbs = 4;
constexpr std::uint32_t kGlslSmoothStep = 49;
constexpr std::uint32_t kStorageUniform = 2;
constexpr std::size_t kHeaderWords = 5;
constexpr std::uint32_t kMagic = 0x07230203;

struct Instruction {
    std::size_t at = 0; // word offset
    std::uint32_t op = 0;
    std::uint32_t count = 0;
};

class Module {
public:
    explicit Module(std::span<const std::uint32_t> code) : m_code(code) {
        if (code.size() < kHeaderWords || code[0] != kMagic) {
            return;
        }
        for (std::size_t at = kHeaderWords; at < code.size();) {
            const std::uint32_t count = code[at] >> 16;
            if (count == 0 || at + count > code.size()) {
                m_instructions.clear();
                return;
            }
            const Instruction ins{at, code[at] & 0xFFFF, count};
            if (const std::optional<std::uint32_t> id = resultOf(ins)) {
                m_defs[*id] = m_instructions.size();
            }
            m_instructions.push_back(ins);
            at += count;
        }
        m_valid = true;
    }

    [[nodiscard]] bool valid() const { return m_valid; }
    [[nodiscard]] const std::vector<Instruction>& instructions() const { return m_instructions; }
    [[nodiscard]] std::uint32_t word(const Instruction& ins, std::size_t i) const {
        return i < ins.count ? m_code[ins.at + i] : 0;
    }
    [[nodiscard]] const Instruction* def(std::uint32_t id) const {
        const auto it = m_defs.find(id);
        return it == m_defs.end() ? nullptr : &m_instructions[it->second];
    }
    [[nodiscard]] std::size_t indexOf(const Instruction& ins) const {
        return static_cast<std::size_t>(&ins - m_instructions.data());
    }

    // A 32-bit float constant's value, or nullopt.
    [[nodiscard]] std::optional<float> floatConstant(std::uint32_t id) const {
        const Instruction* c = def(id);
        if (!c || c->op != kConstant || c->count != 4) {
            return std::nullopt;
        }
        const Instruction* type = def(word(*c, 1));
        if (!type || type->op != kTypeFloat || word(*type, 2) != 32) {
            return std::nullopt;
        }
        const std::uint32_t bits = word(*c, 3);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    [[nodiscard]] std::optional<std::uint32_t> intConstant(std::uint32_t id) const {
        const Instruction* c = def(id);
        if (!c || c->op != kConstant || c->count != 4) {
            return std::nullopt;
        }
        return word(*c, 3);
    }
    [[nodiscard]] bool isFloatVector(std::uint32_t typeId, std::uint32_t size) const {
        const Instruction* t = def(typeId);
        if (!t || t->op != kTypeVector || word(*t, 3) != size) {
            return false;
        }
        const Instruction* f = def(word(*t, 2));
        return f && f->op == kTypeFloat && word(*f, 2) == 32;
    }

private:
    [[nodiscard]] std::optional<std::uint32_t> resultOf(const Instruction& ins) const {
        switch (ins.op) {
        case kExtInstImport:
        case kTypeFloat:
        case kTypeVector:
        case kTypeMatrix:
        case kTypePointer:
            return m_code[ins.at + 1];
        case kExtInst:
        case kConstant:
        case kVariable:
        case kLoad:
        case kAccessChain:
        case kVectorShuffle:
        case kCompositeConstruct:
        case kCompositeExtract:
        case kFSub:
        case kDot:
            return ins.count > 2 ? std::optional<std::uint32_t>(m_code[ins.at + 2]) : std::nullopt;
        default:
            return std::nullopt;
        }
    }

    std::span<const std::uint32_t> m_code;
    std::vector<Instruction> m_instructions;
    std::unordered_map<std::uint32_t, std::size_t> m_defs;
    bool m_valid = false;
};

// The fade: `dot(ndc, ndc)` of two loads of one vec2 variable, `1 - dot` right after, smoothstep(0, 0.5, ...)
// within the next few instructions.
struct FadeSite {
    std::size_t dot = 0;   // instruction index of the OpDot
    std::uint32_t ndc = 0; // the first operand (a loaded vec2)
    std::uint32_t vec2Type = 0;
    std::uint32_t floatType = 0;
    std::uint32_t one = 0;     // the float constant 1.0 of the subtraction
    std::uint32_t glslSet = 0; // the GLSL.std.450 import of the smoothstep
};

std::vector<FadeSite> fadeSites(const Module& m) {
    std::vector<FadeSite> sites;
    const auto& list = m.instructions();
    for (std::size_t i = 0; i + 1 < list.size(); ++i) {
        const Instruction& dot = list[i];
        if (dot.op != kDot || dot.count != 5) {
            continue;
        }
        const Instruction* a = m.def(m.word(dot, 3));
        const Instruction* b = m.def(m.word(dot, 4));
        if (!a || !b || a->op != kLoad || b->op != kLoad || m.word(*a, 3) != m.word(*b, 3) ||
            !m.isFloatVector(m.word(*a, 1), 2)) {
            continue;
        }
        const Instruction& sub = list[i + 1];
        const std::optional<float> one = m.floatConstant(m.word(sub, 3));
        if (sub.op != kFSub || sub.count != 5 || m.word(sub, 4) != m.word(dot, 2) || !one || *one != 1.0f) {
            continue;
        }
        for (std::size_t j = i + 2; j < list.size() && j <= i + 6; ++j) {
            const Instruction& s = list[j];
            if (s.op != kExtInst || s.count != 8 || m.word(s, 4) != kGlslSmoothStep) {
                continue;
            }
            const std::optional<float> e0 = m.floatConstant(m.word(s, 5));
            const std::optional<float> e1 = m.floatConstant(m.word(s, 6));
            if (e0 && e1 && *e0 == 0.0f && *e1 == 0.5f) {
                sites.push_back(
                    FadeSite{i, m.word(dot, 3), m.word(*a, 1), m.word(dot, 1), m.word(sub, 3), m.word(s, 3)});
                break;
            }
        }
    }
    return sites;
}

// One column of a rotation: three extracts (0, 1, 2) of a vec4 loaded from a uniform block's member, directly
// or through a .xyz shuffle.
struct ColumnSource {
    std::uint32_t pointerType = 0;
    std::uint32_t block = 0;
    std::uint32_t memberConstant = 0;
    std::uint32_t member = 0;
    std::uint32_t vec4Type = 0;
};

std::optional<ColumnSource> columnSource(const Module& m, std::uint32_t column) {
    const Instruction* c = m.def(column);
    if (!c || c->op != kCompositeConstruct || c->count != 6) {
        return std::nullopt;
    }
    std::uint32_t composite = 0;
    for (std::uint32_t k = 0; k < 3; ++k) {
        const Instruction* e = m.def(m.word(*c, 3 + k));
        if (!e || e->op != kCompositeExtract || e->count != 5 || m.word(*e, 4) != k ||
            (k > 0 && m.word(*e, 3) != composite)) {
            return std::nullopt;
        }
        composite = m.word(*e, 3);
    }
    const Instruction* load = m.def(composite);
    if (load && load->op == kVectorShuffle && load->count == 8 && m.word(*load, 3) == m.word(*load, 4) &&
        m.word(*load, 5) == 0 && m.word(*load, 6) == 1 && m.word(*load, 7) == 2) {
        load = m.def(m.word(*load, 3));
    }
    if (!load || load->op != kLoad || load->count != 4 || !m.isFloatVector(m.word(*load, 1), 4)) {
        return std::nullopt;
    }
    const Instruction* chain = m.def(m.word(*load, 3));
    if (!chain || chain->op != kAccessChain || chain->count != 5) {
        return std::nullopt;
    }
    const Instruction* pointer = m.def(m.word(*chain, 1));
    const std::optional<std::uint32_t> member = m.intConstant(m.word(*chain, 4));
    if (!pointer || pointer->op != kTypePointer || m.word(*pointer, 2) != kStorageUniform || !member) {
        return std::nullopt;
    }
    return ColumnSource{m.word(*chain, 1), m.word(*chain, 3), m.word(*chain, 4), *member, m.word(*load, 1)};
}

// The rotations built as mat3(X.xyz, Y.xyz, Z.xyz) from one uniform block: the sources of their columns.
std::vector<std::array<ColumnSource, 3>> rotations(const Module& m) {
    std::vector<std::array<ColumnSource, 3>> found;
    for (const Instruction& ins : m.instructions()) {
        if (ins.op != kCompositeConstruct || ins.count != 6) {
            continue;
        }
        const Instruction* type = m.def(m.word(ins, 1));
        if (!type || type->op != kTypeMatrix || m.word(*type, 3) != 3 ||
            !m.isFloatVector(m.word(*type, 2), 3)) {
            continue;
        }
        std::array<ColumnSource, 3> columns{};
        bool ok = true;
        for (std::uint32_t k = 0; k < 3 && ok; ++k) {
            const std::optional<ColumnSource> s = columnSource(m, m.word(ins, 3 + k));
            ok = s && (k == 0 || (s->block == columns[0].block && s->member == columns[0].member + k));
            if (ok) {
                columns[k] = *s;
            }
        }
        if (ok) {
            found.push_back(columns);
        }
    }
    return found;
}

std::uint32_t header(std::uint32_t words, std::uint32_t op) {
    return (words << 16) | op;
}

} // namespace

const std::array<KnownModule, kKnownModules>& knownModules() {
    // Member indices read from each module's view block (the rotation's first column, the packed projection);
    // output hashes of the patched words as spirv-val validated them.
    static const std::array<KnownModule, kKnownModules> modules = {{
        {0x149bcf51cb081118ULL, 28, 31, 0x156bf059657d48feULL},
        {0x187b8945e8c0c2c0ULL, 16, 19, 0xf1df3802b870a223ULL},
        {0x2c6b032ba6a96b70ULL, 15, 18, 0x0c1f0e5120f7a6eeULL},
        {0x4e1f170f3536649bULL, 16, 19, 0xfa48bb049195390cULL},
        {0x6fd7ea1b33c6ce3aULL, 16, 19, 0x447986f2277c0615ULL},
        {0x7b6c688291023557ULL, 28, 31, 0x4a2f2bfc10df6a77ULL},
        {0x80729510b15cbf16ULL, 14, 17, 0x5cebe04777f6fa8eULL},
        {0x86e5aa75deec443bULL, 28, 31, 0x302ee1d81c427b6bULL},
        {0xb88df7c670b7bd66ULL, 28, 31, 0xdf852e37b7642234ULL},
        {0xba435f66b053cc6bULL, 14, 17, 0xba737f474d67e84dULL},
        {0xbde5c5ffd488e92dULL, 14, 17, 0x7f30fce995b88671ULL},
        {0xc74e6e1333635f32ULL, 16, 19, 0xd67d45eb6f2c222fULL},
        {0xcd69a20af50e5d8fULL, 28, 31, 0x9b657c853a8e6069ULL},
        {0xd6ec648c8832f8aaULL, 28, 31, 0x9f8413df66e119c0ULL},
        {0xdf49f3a12d023910ULL, 28, 31, 0x8a32a4d7ba501d4cULL},
        {0xe891548d99aa3240ULL, 28, 31, 0xb591c51b19305d14ULL},
        {0xf015da6d9bb4b375ULL, 28, 31, 0x7fb53af8d2f5a8f3ULL},
        {0xf12fbba55d698500ULL, 27, 30, 0x51ebcd5b3bf590b4ULL},
        {0x4459b765930c80caULL, 5, 11, 0x62d3c05ae2b79a67ULL},
        {0x8e532679f83c7706ULL, 4, 9, 0x81a1799d7f0afee7ULL},
        {0xaffbf023da095294ULL, 4, 9, 0xb1f032a17fb41ae7ULL},
        {0xc1a4cccfa10a98bdULL, 4, 9, 0x3baa2c82b9227915ULL},
    }};
    return modules;
}

std::optional<std::size_t> knownIndex(std::span<const std::uint32_t> code) {
    const std::uint64_t hash = dump::fnv1a64(code.data(), code.size_bytes());
    const auto& modules = knownModules();
    for (std::size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].inputHash == hash) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint32_t>> patchFade(std::span<const std::uint32_t> code,
                                                    std::uint32_t rowsMember,
                                                    std::uint32_t packedMember,
                                                    std::string& why) {
    const Module m(code);
    if (!m.valid()) {
        why = "not a SPIR-V module";
        return std::nullopt;
    }
    const std::vector<FadeSite> sites = fadeSites(m);
    if (sites.size() != 1) {
        why = std::to_string(sites.size()) + " fade(s), one expected";
        return std::nullopt;
    }
    std::optional<std::array<ColumnSource, 3>> rows;
    for (const auto& r : rotations(m)) {
        if (r[0].member == rowsMember) {
            if (rows && rows->at(0).block != r[0].block) {
                why = "rotations from two blocks";
                return std::nullopt;
            }
            rows = r;
        }
    }
    if (!rows) {
        why = "no rotation from member " + std::to_string(rowsMember);
        return std::nullopt;
    }
    const ColumnSource& x = (*rows)[0];
    std::uint32_t packedConstant = 0;
    for (const Instruction& ins : m.instructions()) {
        if (ins.op == kAccessChain && ins.count == 5 && m.word(ins, 3) == x.block &&
            m.word(ins, 1) == x.pointerType && m.intConstant(m.word(ins, 4)) == packedMember) {
            packedConstant = m.word(ins, 4);
            break;
        }
    }
    if (packedConstant == 0) {
        why = "no packed projection load from member " + std::to_string(packedMember);
        return std::nullopt;
    }

    const FadeSite& site = sites.front();
    std::uint32_t bound = code[3];
    const auto id = [&bound] {
        return bound++;
    };
    std::vector<std::uint32_t> add;
    const auto emit = [&add](std::initializer_list<std::uint32_t> words) {
        add.insert(add.end(), words);
    };
    const std::uint32_t f = site.floatType;
    const auto load = [&](std::uint32_t member) {
        const std::uint32_t chain = id();
        const std::uint32_t value = id();
        emit({header(5, kAccessChain), x.pointerType, chain, x.block, member});
        emit({header(4, kLoad), x.vec4Type, value, chain});
        return value;
    };
    const auto extract = [&](std::uint32_t composite, std::uint32_t index) {
        const std::uint32_t r = id();
        emit({header(5, kCompositeExtract), f, r, composite, index});
        return r;
    };
    const auto binary = [&](std::uint32_t op, std::uint32_t a, std::uint32_t b) {
        const std::uint32_t r = id();
        emit({header(5, op), f, r, a, b});
        return r;
    };
    // c = X.x X.z + Y.x Y.z + Z.x Z.z: the shear's [0][2] term, (S R)(S R)^T = S S^T.
    std::uint32_t c = 0;
    for (std::size_t k = 0; k < 3; ++k) {
        const std::uint32_t column = load((*rows)[k].memberConstant);
        const std::uint32_t xPart = extract(column, 0); // one statement each: the words keep their order
        const std::uint32_t zPart = extract(column, 2);
        const std::uint32_t product = binary(kFMul, xPart, zPart);
        c = k == 0 ? product : binary(kFAdd, c, product);
    }
    // m2 = c m0 = -2 packed.x c; ndc_x' = (ndc_x + m2) / (1 - |m2|).
    const std::uint32_t packed = load(packedConstant);
    const std::uint32_t packedX = extract(packed, 0);
    const std::uint32_t half = binary(kFMul, packedX, c);
    const std::uint32_t twice = binary(kFAdd, half, half);
    const std::uint32_t m2 = id();
    emit({header(4, kFNegate), f, m2, twice});
    const std::uint32_t magnitude = id();
    emit({header(6, kExtInst), f, magnitude, site.glslSet, kGlslFAbs, m2});
    const std::uint32_t width = binary(kFSub, site.one, magnitude);
    const std::uint32_t loadedX = extract(site.ndc, 0);
    const std::uint32_t ndcX = binary(kFAdd, loadedX, m2);
    const std::uint32_t shared = binary(kFDiv, ndcX, width);
    const std::uint32_t ndc = id();
    emit({header(6, kCompositeInsert), site.vec2Type, ndc, shared, site.ndc, 0});

    const Instruction& dot = m.instructions()[site.dot];
    std::vector<std::uint32_t> out;
    out.reserve(code.size() + add.size());
    out.insert(out.end(), code.begin(), code.begin() + static_cast<std::ptrdiff_t>(dot.at));
    out.insert(out.end(), add.begin(), add.end());
    out.insert(out.end(), code.begin() + static_cast<std::ptrdiff_t>(dot.at), code.end());
    const std::size_t dotAt = dot.at + add.size();
    out[dotAt + 3] = ndc;
    out[dotAt + 4] = ndc;
    out[3] = bound;
    return out;
}

bool hasFadeSignature(std::span<const std::uint32_t> code) {
    const Module m(code);
    return m.valid() && !fadeSites(m).empty() && !rotations(m).empty();
}

} // namespace evr::vkcore::ssr_fade
