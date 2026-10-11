#include "vkcore/refraction_locate.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"

#include <cstddef>

namespace evr::vkcore {

namespace {

// ---- The device context constructor (RVA 0x1C1CD80) ----

// The mask tiles' pair (RVA 0x1C1D3ED): `lea rsi, [rdi + 0x488]`, a loop of two: `lea rdx,
// ["_refractionMaskTiles32x%d"]` (+0x16), the name formatted, `mov rcx, [rip + imageManager]` (+0x3F), `lea
// r8, [rbp + 0x1B0]` (the image description), `call create` (+0x51), `mov [rsi], rax`; after the loop the
// hook (+0xBD, `and byte ptr [rbp + 0x1E9], 0xFE`), where rdi is the device context and rbp + 0x1B0 still
// holds the description.
constexpr const char* kMaskImagesSignature =
    "48 8D B7 88 04 00 00 4C 8D 25 ?? ?? ?? ?? 0F 1F 44 00 00 44 8B C3 48 8D 15 ?? ?? ?? ?? 48 8D 8D E0 02 "
    "00 00 E8 ?? ?? ?? ?? 44 8B 47 04 48 8D 8D D0 00 00 00 48 8B D0 45 8B CD E8 ?? ?? ?? ?? 48 8B 0D ?? ?? "
    "?? ?? 4C 8D 85 B0 01 00 00 48 8B 50 08 E8 ?? ?? ?? ?? 48 89 06 48 8D 85 00 01 00 00 4C 89 A5 D0 00 00 "
    "00 48 39 85 D8 00 00 00 75 24 8B 85 E4 00 00 00 25 00 00 00 C0 4C 89 B5 D8 00 00 00 0D 00 00 00 80 44 "
    "89 B5 E0 00 00 00 89 85 E4 00 00 00 48 8D 8D D0 00 00 00 E8 ?? ?? ?? ?? FF C3 48 83 C6 08 83 FB 02 0F "
    "8C ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 80 A5 E9 01 00 00 FE";
constexpr std::size_t kMaskImagesName = 0x16;
constexpr std::size_t kMaskImagesManager = 0x3F;
constexpr std::size_t kMaskImagesCreate = 0x51;
constexpr std::size_t kMaskImagesHook = 0xBD;

// The accumulation images (RVA 0x1C1DB6D): `lea r14, [rdi + 0x440]`, the half-size description at rbp +
// 0x140 (the render size >> 1, at least 1) ...
constexpr const char* kAccHeadSignature =
    "48 8B 05 ?? ?? ?? ?? 4C 8D B7 40 04 00 00 80 A5 79 01 00 00 FE 33 DB 8B 4D 00 BA 01 00 00 00 D1 F9 3B "
    "CA 48 89 85 88 01 00 00 89 4D 70 8B C2 0F 4F C1 89 9D 40 01 00 00 89 85 4C 01 00 00";
// ... two loops of two (RVA 0x1C1DC23): `lea rdx, ["_refractionAccumulationBuffer%d%d"]` (+0x3), the name
// formatted, `mov rcx, [rip + imageManager]` (+0x42), `lea r8, [rbp + 0x140]`, `call create` (+0x54), `mov
// [r14], rax` (image [pair * 2 + update] at + 0x440), its render target at [r14 + 0x1B0] (+ 0x5F0) ...
constexpr const char* kAccCreateSignature =
    "45 8B CF 48 8D 15 ?? ?? ?? ?? 45 8B C4 48 8D 8D E0 02 00 00 E8 ?? ?? ?? ?? 48 8B D0 48 8D 8D B0 01 00 "
    "00 E8 ?? ?? ?? ?? 44 8B 47 04 48 8D 8D D0 00 00 00 48 8B 95 B8 01 00 00 45 8B CD E8 ?? ?? ?? ?? 48 8B "
    "0D ?? ?? ?? ?? 4C 8D 85 40 01 00 00 48 8B 50 08 E8 ?? ?? ?? ?? 49 89 06";
constexpr std::size_t kAccCreateName = 0x3;
constexpr std::size_t kAccCreateManager = 0x42;
constexpr std::size_t kAccCreateCall = 0x54;
// ... and the loops' end (RVA 0x1C1DD22): the target attached over the image, `inc r15d; add r14, 8; cmp
// r15d, 2; jl`, `inc r12d; cmp r12d, 2; jl`; the hook on the next instruction (+0x41, `lea edx, [rbx +
// 0x45]`), where rdi is the device context and rbp + 0x140 still holds the description.
constexpr const char* kAccTailSignature =
    "49 8B 16 33 DB 45 33 C9 89 5C 24 20 45 33 C0 48 8B CE E8 ?? ?? ?? ?? 48 8D 8D B0 01 00 00 E8 ?? ?? ?? "
    "?? 41 FF C7 49 83 C6 08 41 83 FF 02 0F 8C ?? ?? ?? ?? 41 FF C4 41 83 FC 02 0F 8C ?? ?? ?? ?? 8D 53 45 "
    "B9 "
    "30 02 00 00";
constexpr std::size_t kAccTailHook = 0x41;

// ---- The mask pass (RVA 0x1C64360; called by the mask job 0x1C63740 and by nothing else) ----

// Its entry: rcx the command context, rdx the transparency context; `mov eax, [rdx + 0xF8]` .. `and eax, 1`
// the parity, `mov rcx, [rip + deviceContext]` (+0x2F).
constexpr const char* kMaskEntrySignature =
    "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 30 8B 82 F8 00 00 00 "
    "48 8B E9 4C 8B B1 00 01 00 00 83 E0 01 48 8B 0D ?? ?? ?? ?? 45 33 C0 48 8B D5 8B D8 44 8D 60 FF 4C 8B "
    "A9 "
    "60 04 00 00 41 83 E4 01 48 8B 89 68 04 00 00";
constexpr std::size_t kMaskEntryDc = 0x2F;
// The output: `mov rcx, [rip + deviceContext]`, `lea r15, [rbx * 8 + 0x488]` (rbx the parity), then the hook
// (+0xF, `xor r8d, r8d`) before `mov rcx, [r15 + rcx]` and its write mark. r15 is read twice more, always as
// [r15 + the device context]: the output's size (kMaskSizeSignature) and its binding (kMaskOutSignature). The
// hook points r15 at the eye's own image for all three.
constexpr const char* kMaskSlotsSignature =
    "48 8B 0D ?? ?? ?? ?? 4C 8D 3C DD 88 04 00 00 45 33 C0 48 8B D5 49 8B 0C 0F";
constexpr std::size_t kMaskSlotsHook = 0xF;
constexpr const char* kMaskSizeSignature = "4C 8B 05 ?? ?? ?? ?? 4B 8B 0C 07";
// The last render's tiles: `mov r8, [rax + r12 * 8 + 0x488]` (r12 = the parity ^ 1), then the hook (+0x19,
// `add r8, 0xC8`) before their binding.
constexpr const char* kMaskLastSignature =
    "48 8B 05 ?? ?? ?? ?? 49 8B CE 48 8B 15 ?? ?? ?? ?? 4E 8B 84 E0 88 04 00 00 49 81 C0 C8 00 00 00";
constexpr std::size_t kMaskLastHook = 0x19;
constexpr const char* kMaskOutSignature =
    "48 8B 05 ?? ?? ?? ?? 49 8B CE 48 8B 15 ?? ?? ?? ?? 4D 8B 04 07 49 81 C0 C8 00 00 00";

// ---- The refraction pass (RVA 0x1C63DB0; called by the transparency job 0x1C627C0 per update) ----

// Its entry: rcx the command context, rdx the transparency context, r8d the update (0 or 1), r9b the reset
// flag (the job's: the update count changed since the last render; the blend is then 1.0, no history); `mov
// ecx, [rdx + 0xF8]` .. `and ecx, 1` the parity, `movsxd rdi, r8d`, `movzx ebx, r9b`.
constexpr const char* kPassEntrySignature =
    "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00 48 8B 05 "
    "?? ?? ?? ?? 48 33 C4 48 89 85 A0 00 00 00 48 8B 05 ?? ?? ?? ?? 4C 8B F1 8B 8A F8 00 00 00 4C 8B FA 83 "
    "E1 "
    "01 49 63 F8 48 89 4C 24 30 41 B0 03 8B 40 08 49 8B CE B2 02 89 44 24 24 41 0F B6 D9 E8 ?? ?? ?? ??";
// With the mask (RVA 0x1C63E8B): `mov rdx, [rip + deviceContext]`, the argument block at rsp + 0x40 filled:
// + 0x50 the mask tiles [parity], + 0x58 [parity ^ 1], + 0x60 the image of target [parity * 2 + update]
// (written),
// + 0x68 [(parity ^ 1) * 2 + update] (read); the hook (+0x65, `mov [rsp + 0x70], r12`) before `test bl, bl`.
constexpr const char* kPassMaskSignature =
    "48 8B 15 ?? ?? ?? ?? 33 C0 48 89 45 A0 89 45 AC 49 8B 87 E0 00 00 00 48 89 44 24 48 4C 89 74 24 40 4A "
    "8B "
    "84 EA 88 04 00 00 48 89 44 24 58 48 8B 84 CA 88 04 00 00 48 89 44 24 50 48 8D 04 4F 48 8B 8C C2 F0 05 "
    "00 00 48 8B 41 10 48 89 44 24 60 4A 8D 04 6F 48 8B 8C C2 F0 05 00 00 48 8B 41 10 48 89 44 24 68 4C 89 "
    "64 "
    "24 70 84 DB 75 0C 48 8B 05 ?? ?? ?? ?? F3 0F 10 40 0C";
constexpr std::size_t kPassMaskHook = 0x65;
// Without the mask (RVA 0x1C63FE5, r_refractionTAA on, the chain's top level): the blend ratio at rsp + 0x90,
// `mov rdx, [rip + deviceContext]` (+0x7), + 0x60 written, + 0x68 read as above; the hook (+0x46, `mov rax,
// [r15 + 0xE0]`).
constexpr const char* kPassPlainSignature = "48 8B 05 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? C6 45 9A 01 F3 0F 10 "
                                            "40 0C 48 8D 04 4E F3 0F 11 45 90 48 8B 84 "
                                            "C2 F0 05 00 00 48 8B 48 10 4A 8D 04 6E 48 89 4C 24 60 48 8B 8C "
                                            "C2 F0 05 00 00 48 8B 41 10 48 89 44 24 68 "
                                            "49 8B 87 E0 00 00 00 48 89 44 24 70";
constexpr std::size_t kPassPlainDc = 0x7;
constexpr std::size_t kPassPlainHook = 0x46;
// The transparency job's call of the pass (RVA 0x1C62A93, in 0x1C627C0): `lea r8d, [rcx - 1]` (the update),
// `mov eax, [rbx + 0x137D0]` (the render's update count, kRefractionUpdateCount), `mov rdx, rbx`, `cmp [rip
// + last render's count], eax`, `mov rcx, rdi`, `setne r9b` (the reset flag), `call pass` (+0x1A).
constexpr const char* kPassCallSignature =
    "44 8D 41 FF 8B 83 D0 37 01 00 48 8B D3 39 05 ?? ?? ?? ?? 48 8B CF 41 0F 95 C1 E8 ?? ?? ?? ??";
constexpr std::size_t kPassCallCall = 0x1A;

struct Sites {
    const std::byte* maskImages = nullptr;
    const std::byte* accHead = nullptr;
    const std::byte* accCreate = nullptr;
    const std::byte* accTail = nullptr;
    const std::byte* maskEntry = nullptr;
    const std::byte* maskSlots = nullptr;
    const std::byte* maskSize = nullptr;
    const std::byte* maskLast = nullptr;
    const std::byte* maskOut = nullptr;
    const std::byte* passEntry = nullptr;
    const std::byte* passMask = nullptr;
    const std::byte* passPlain = nullptr;
    const std::byte* passCall = nullptr;
};

bool findSites(const GameImage& image, const char* tag, Sites& s) {
    s.maskImages = findUnique(image, tag, "mask tile images", kMaskImagesSignature);
    s.accHead = findUnique(image, tag, "accumulation images", kAccHeadSignature);
    s.accCreate = findUnique(image, tag, "accumulation image loop", kAccCreateSignature);
    s.accTail = findUnique(image, tag, "accumulation targets", kAccTailSignature);
    s.maskEntry = findUnique(image, tag, "mask pass", kMaskEntrySignature);
    s.maskSlots = findUnique(image, tag, "mask pass output", kMaskSlotsSignature);
    s.maskSize = findUnique(image, tag, "mask pass output size", kMaskSizeSignature);
    s.maskLast = findUnique(image, tag, "mask pass last tiles", kMaskLastSignature);
    s.maskOut = findUnique(image, tag, "mask pass output binding", kMaskOutSignature);
    s.passEntry = findUnique(image, tag, "refraction pass", kPassEntrySignature);
    s.passMask = findUnique(image, tag, "refraction pass with the mask", kPassMaskSignature);
    s.passPlain = findUnique(image, tag, "refraction pass without the mask", kPassPlainSignature);
    s.passCall = findUnique(image, tag, "refraction pass call", kPassCallSignature);
    return s.maskImages && s.accHead && s.accCreate && s.accTail && s.maskEntry && s.maskSlots &&
           s.maskSize && s.maskLast && s.maskOut && s.passEntry && s.passMask && s.passPlain && s.passCall;
}

// Every site in its function and in order, both loops' names, image manager and create function the same,
// every pass site reading one device context global, and the job's call reaching the pass.
bool checkSites(const GameImage& image,
                const char* tag,
                const Sites& s,
                const std::byte*& manager,
                const std::byte*& create,
                const std::byte*& dc) {
    const auto rip = [&](const std::byte* at, std::size_t disp) {
        return ripTarget(image, at + disp, at + disp + 4);
    };
    manager = rip(s.maskImages, kMaskImagesManager + 3);
    create = branchTarget(image, s.maskImages + kMaskImagesCreate);
    dc = rip(s.passMask, 3);
    const std::byte* constructor = functionStart(image, s.maskImages);
    const bool built =
        stringAt(image, rip(s.maskImages, kMaskImagesName + 3)) == "_refractionMaskTiles32x%d" &&
        stringAt(image, rip(s.accCreate, kAccCreateName + 3)) == "_refractionAccumulationBuffer%d%d" &&
        manager && image.contains(manager, sizeof(void*)) &&
        rip(s.accCreate, kAccCreateManager + 3) == manager && create &&
        functionStart(image, create) == create &&
        branchTarget(image, s.accCreate + kAccCreateCall) == create && constructor &&
        functionStart(image, s.accHead) == constructor && functionStart(image, s.accCreate) == constructor &&
        functionStart(image, s.accTail + kAccTailHook) == constructor && s.maskImages < s.accHead &&
        s.accHead < s.accCreate && s.accCreate < s.accTail;
    const bool mask = functionStart(image, s.maskEntry) == s.maskEntry &&
                      functionStart(image, s.maskSlots) == s.maskEntry &&
                      functionStart(image, s.maskSize) == s.maskEntry &&
                      functionStart(image, s.maskLast) == s.maskEntry &&
                      functionStart(image, s.maskOut) == s.maskEntry && s.maskEntry < s.maskSlots &&
                      s.maskSlots < s.maskSize && s.maskSize < s.maskLast && s.maskLast < s.maskOut;
    const bool pass =
        functionStart(image, s.passEntry) == s.passEntry && functionStart(image, s.passMask) == s.passEntry &&
        functionStart(image, s.passPlain) == s.passEntry && s.passEntry < s.passMask &&
        s.passMask < s.passPlain && branchTarget(image, s.passCall + kPassCallCall) == s.passEntry;
    const bool oneDc = dc && image.contains(dc, sizeof(void*)) && rip(s.passPlain, kPassPlainDc + 3) == dc &&
                       rip(s.maskEntry, kMaskEntryDc + 3) == dc && rip(s.maskSlots, 3) == dc &&
                       rip(s.maskSize, 3) == dc && rip(s.maskLast, 3) == dc && rip(s.maskOut, 3) == dc;
    if (!built || !mask || !pass || !oneDc) {
        EVR_LOG(
            "%s: the device context's refraction images (%d), the mask pass (%d), the refraction pass (%d) "
            "or their device context (%d) did not check out; not installed",
            tag, built, mask, pass, oneDc);
        return false;
    }
    return true;
}

} // namespace

bool locateRefractionSites(const GameImage& image, const char* tag, RefractionSites& out) {
    Sites s;
    if (!findSites(image, tag, s)) {
        return false;
    }
    const std::byte* manager = nullptr;
    const std::byte* create = nullptr;
    const std::byte* dc = nullptr;
    if (!checkSites(image, tag, s, manager, create, dc)) {
        return false;
    }
    const auto at = [](const std::byte* p, std::size_t offset) {
        return const_cast<std::byte*>(p + offset);
    };
    out.maskImagesBuilt = at(s.maskImages, kMaskImagesHook);
    out.accImagesBuilt = at(s.accTail, kAccTailHook);
    out.maskEntry = at(s.maskEntry, 0);
    out.maskSlots = at(s.maskSlots, kMaskSlotsHook);
    out.maskLast = at(s.maskLast, kMaskLastHook);
    out.passEntry = at(s.passEntry, 0);
    out.passMask = at(s.passMask, kPassMaskHook);
    out.passPlain = at(s.passPlain, kPassPlainHook);
    out.imageManager = manager;
    out.createImage = create;
    out.deviceContext = dc;
    return true;
}

} // namespace evr::vkcore
