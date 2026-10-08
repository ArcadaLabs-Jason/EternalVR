#include "vkcore/fx_sync_hooks.hpp"

#include "stereo_seq/fx_sync.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seq_hooks.hpp"

#include "engine/eternal/resolver/pattern.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

using stereo_seq::FxAction;

constexpr const char* kTag = "seq-fx";

// The world's prepare 0x18E78D0 (RVA 0x18E78FB): `mov rcx, [rdi + 0x25060]` (the ring), the stores that reset
// the deferred fill's handle and count, `test rcx, rcx`, `je`, `call` the ring's advance (+0x21, the hook),
// then (+0x26, where eye R resumes) the previous slot's address from the ring again, the ring + 0x4D8 +
// ((index [+0x7C8] + 2) % 3) * 0x68. Nothing after the call reads a register the call set.
constexpr const char* kPrepareSignature =
    "48 8B 8F 60 50 02 00 48 89 87 58 DA 71 00 C7 87 50 DA 71 00 00 00 00 00 "
    "48 85 C9 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B 8F 60 50 02 00 B8 56 "
    "55 55 55 45 8B 81 C8 07 00 00 41 83 C0 02 41 F7 E8 8B C2 C1 E8 1F 03 "
    "D0 B8 56 55 55 55 8D 0C 52 44 2B C1 49 63 C8 4D 8D 81 D8 04 00 00 48 "
    "6B D1 68";
constexpr std::size_t kPrepareHook = 0x21;
constexpr std::size_t kPrepareResume = 0x26;

// The ring's advance 0x1A0F5D0: the previous pointer (+0x7C0) and fill (+0x7D0) take the current slot's,
// which is opened first when it is not (`P + 0x4D8 + index * 0x68`, call the opener at +0x38), then the index
// steps and the new current slot is opened.
constexpr const char* kAdvanceCode =
    "48 89 5C 24 08 57 48 83 EC 20 48 8B 81 B8 07 00 00 48 8B D9 48 85 C0 74 08 "
    "8B B9 CC 07 00 00 EB 1C 48 63 81 C8 07 00 00 33 FF 48 6B C8 68 48 81 C1 "
    "D8 04 00 00 48 03 CB E8 ?? ?? ?? ?? 48 89 83 C0 07 00 00 89 BB D0 07 00 "
    "00";
constexpr std::size_t kAdvanceOpenCall = 0x38;

// The slot opener 0x1BFC280: the slot's mapped pointer (`[slot + 0x48] + [slot + 4]`, 0 when unmapped) and
// its open flag (bit 0 of +0xA) set.
constexpr const char* kOpenerCode = "48 8B 51 48 33 C0 48 85 D2 74 07 48 63 41 04 48 03 C2 80 49 0A 01 C3";

// The frame-middle job 0x18E0CE0 (RVA 0x18E0D20) closes the previous slot (call 0x1A0F580 at +0x21) before it
// queues the deferred fill: the close zeroes +0x7C0 and clears the previous slot's open flag (`jmp` 0x1BFD7A0
// at +0x4A), so a reopened slot is closed again by eye R's own frame middle.
constexpr const char* kMiddleSignature =
    "48 8B 8E 60 50 02 00 48 89 86 10 5A 71 00 48 8B 05 ?? ?? ?? ?? 48 89 86 "
    "18 5A 71 00 48 85 C9 74 28 E8 ?? ?? ?? ?? 48 8B 86 60 50 02 00 8B 88 "
    "D0 07 00 00 03 88 CC 07 00 00";
constexpr std::size_t kMiddleCloseCall = 0x21;
constexpr const char* kCloseCode =
    "44 8B 81 C8 07 00 00 B8 56 55 55 55 41 83 C0 02 48 C7 81 C0 07 00 00 00 00 00 "
    "00 41 F7 E8 4C 8B C9 8B C2 C1 E8 1F 03 D0 8D 04 52 44 2B C0 49 63 C0 48 6B "
    "C8 68 42 F6 84 09 E2 04 00 00 01 74 0F 49 81 C1 D8 04 00 00 49 03 C9 E9 ?? "
    "?? ?? ?? C3";
constexpr std::size_t kCloseJump = 0x4A;
constexpr const char* kCloserCode = "80 61 0A FE C3";

// Further on in the same prepare (RVA 0x18E7A9C, its hook 0x1A1 bytes after the advance's): the ring and the
// particle light pool (`[rdi + 0x25070]`, in rcx) stored for the update jobs, `mov [rsp + 0x40], rbx`, then
// `call` the pool's reset (+0x21, the hook; 0x1953D80: `mov dword [rcx + 0x408], 0`, `ret`, so skipping it
// leaves every register as the call would) and (+0x26, where eye R resumes) `movsxd rbp, [rsi + 0x20970]`.
constexpr const char* kPoolSignature =
    "48 8B 87 60 50 02 00 48 89 8F 48 5A 71 00 48 8B 8F 70 50 02 00 48 89 87 40 "
    "5A 71 00 48 89 5C 24 40 E8 ?? ?? ?? ?? 48 63 AE 70 09 02 00";
constexpr std::size_t kPoolHook = 0x21;
constexpr std::size_t kPoolResume = 0x26;
constexpr std::ptrdiff_t kPoolFromPrepare = 0x1A1; // 0x18E7ABD - 0x18E791C
constexpr const char* kPoolResetCode = "C7 81 08 04 00 00 00 00 00 00 C3";
// The pool's count (+0x408) has two other users. The particle lights 0x1955290 (from the bind, the call at
// +0x16 of the particle signature) load the pool (`mov rbp, [rax + 0x25070]` at +0x39) and take a slot with
// `lock xadd [rbp + 0x408], eax`, `cmp eax, 0x80` (+0x10D). The show pass 0x19525D0, called by the job the
// world job queues every render (0x18DF6A0, call at +0x12 of its signature), shows the slots below the count
// (0x18D88E0) and hides the rest (0x18D8AF0).
constexpr std::size_t kParticleLightsCall = 0x16;
constexpr std::size_t kLightsPool = 0x39;
constexpr const char* kLightsPoolCode = "48 8B A8 70 50 02 00";
constexpr std::size_t kLightsTake = 0x10D;
constexpr const char* kLightsTakeCode = "F0 0F C1 85 08 04 00 00 3D 80 00 00 00";
constexpr const char* kShowSignature = "48 8B 31 4C 8B E9 48 89 74 24 30 48 8B 8E 70 50 02 00 E8 ?? ?? ?? ??";
constexpr std::size_t kShowCall = 0x12;
constexpr const char* kShowCode =
    "48 89 5C 24 10 48 89 6C 24 18 48 89 7C 24 20 41 56 48 83 EC 20 8B 91 08 04 00 00 BF 80 00 00 00 3B D7";

// The particle update 0x1955150 (RVA 0x195523D): `mov rax, [rdi]` (the ring), the model's stamp (+0x61C)
// against the ring's frame (+0x4D0) - 1, `jne`, the particle lights (0x1955290) and the bind to the previous
// slot (call at +0x24, 0x1955670) only for a model generated in the render before, then `mov r8, rdi`, `mov
// rdx, rsi`, `mov rcx, rbx` (the model), the generation (call 0x1953D90 at +0x32, the hook; it stamps the
// model with the ring's frame on every path, 0x19545ED) and the epilogue (+0x37, where eye R resumes; the
// update returns nothing). At the hook rcx is the model and [r8] the ring.
constexpr const char* kParticleSignature =
    "48 8B 07 8B 88 D0 04 00 00 FF C9 39 8B 1C 06 00 00 75 16 48 8B CB E8 ?? "
    "?? ?? ?? 4C 8B C7 48 8B D6 48 8B CB E8 ?? ?? ?? ?? 4C 8B C7 48 8B D6 "
    "48 8B CB E8 ?? ?? ?? ?? 48 8B 5C 24 30 48 8B 74 24 38 48 83 C4 20 5F "
    "C3";
constexpr std::size_t kParticleBindCall = 0x24;
constexpr std::size_t kParticleHook = 0x32;
constexpr std::size_t kParticleResume = 0x37;
constexpr const char* kBindPrologue = "48 8B C4 41 56 48 81 EC D0 00 00 00 4D 8B 08";
constexpr const char* kGeneratePrologue =
    "4C 8B DC 49 89 5B 20 55 56 57 41 54 41 55 41 56 41 57 49 8D AB 78 FD FF FF 48 81 EC 50 03 00 00";

// The effect update 0x19511E0 from its start: the frame (`lea rbp, [rsp + 0x30]`, the callee-saved registers
// and the cookie under rbp), r_skipEffectParticles' check (`jle` at +0x7D, `jmp` at +0xD0, both to the
// epilogue), then the effect's stamp (+0x4528) against the ring's frame - 1: `jne` (+0xF2) to the generation
// (the hook), past the bind otherwise. The epilogue restores everything from rbp, so the hook can jump there
// as r_skipEffectParticles does; the generation's alloca (0x195174D) comes after the hook. At the hook r15 is
// the effect (`mov r15, rcx`) and [rbp + 0x28] the update's r9 (`mov [rbp + 0x28], r9`, not written again),
// whose first qword is the ring (`mov r8, [r9]` before the stamp check); the generation stamps the effect
// with the ring's frame read the same way on every path (0x1951998..0x19519AC).
constexpr const char* kEffectSignature = "40 55 41 54 41 55 41 56 41 57 48 81 EC 10 01 00 00 48 8D 6C 24 30 "
                                         "48 89 9D 10 01 00 00 48 89 B5 18 01 00 "
                                         "00 48 89 BD 20 01 00 00 0F 29 B5 D0 00 00 00 0F 29 BD C0 00 00 00 "
                                         "48 8B 05 ?? ?? ?? ?? 48 33 C5 48 89 85 "
                                         "B0 00 00 00 48 8D 05 ?? ?? ?? ?? 4C 89 4D 28 48 89 41 40 45 33 E4 "
                                         "48 8B 05 ?? ?? ?? ?? 49 8B F0 4C 89 45 "
                                         "58 4C 8B F9 83 78 08 01 75 62 45 8B C4 44 39 A1 90 04 00 00 0F 8E "
                                         "?? ?? ?? ?? 41 8B D4 66 66 0F 1F 84 00 "
                                         "00 00 00 00 41 80 8F B0 00 00 00 80 48 8D 92 40 01 00 00 49 8B 8F "
                                         "88 04 00 00 41 FF C0 48 8B 84 0A C8 FE "
                                         "FF FF 44 89 A0 08 01 00 00 48 8B 84 0A C8 FE FF FF 44 89 A0 04 01 "
                                         "00 00 45 3B 87 90 04 00 00 7C C0 E9 ?? "
                                         "?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 41 40 4D 8B 01 41 8B 80 D0 04 "
                                         "00 00 FF C8 39 81 28 45 00 00 0F 85 ?? "
                                         "?? ?? ?? 41 8B 88 C8 07 00 00";
constexpr std::size_t kEffectSkipBranch = 0x7D;  // jle rel32
constexpr std::size_t kEffectSkipJump = 0xD0;    // jmp rel32
constexpr std::size_t kEffectStampBranch = 0xF2; // jne rel32
// The generation's first instructions: `mov rcx, [r15 + 0x4518]`, `movsd xmm0, [rsi + 0x28A64]`.
constexpr const char* kEffectGenerateCode = "49 8B 8F 18 45 00 00 F2 0F 10 86 64 8A 02 00";
constexpr const char* kEffectEpilogue =
    "48 8B 8D B0 00 00 00 48 33 CD E8 ?? ?? ?? ?? 48 8B 9D 10 01 00 00 48 8B B5 "
    "18 01 00 00 48 8B BD 20 01 00 00 0F 28 B5 D0 00 00 00 0F 28 BD C0 00 00 "
    "00 48 8D A5 E0 00 00 00 41 5F 41 5E 41 5D 41 5C 5D C3";

// The ring (world + 0x25060).
constexpr std::size_t kRingSlots = 0x4D8;
constexpr std::size_t kSlotSize = 0x68;
constexpr std::size_t kRingPrevious = 0x7C0;  // the previous slot's mapped pointer
constexpr std::size_t kRingIndex = 0x7C8;     // the current slot's index
constexpr std::size_t kRingFrame = 0x4D0;     // one up per advance
constexpr std::size_t kParticleStamp = 0x61C; // the ring frame of the model's last generation
constexpr std::size_t kEffectStamp = 0x4528;
constexpr std::size_t kEffectRingArg = 0x28; // rbp + 0x28: the effect update's r9, [r9] the ring

using SlotOpenFn = std::byte* (*)(std::byte* slot);

std::once_flag g_once;
bool g_installed = false;
// Set once all four hooks are in: until then they run the engine's code, so a failed install never keeps the
// ring without the light count, or skips the generation of an advanced ring.
std::atomic<bool> g_live{false};
stereo_seq::FxSyncMode g_mode = stereo_seq::FxSyncMode::On;
SlotOpenFn g_openSlot = nullptr;
std::uintptr_t g_prepareResume = 0;
std::uintptr_t g_particleResume = 0;
std::uintptr_t g_effectExit = 0;
std::uintptr_t g_poolResume = 0;

// What the ring hook did in the prepare running on this thread, for the pool hook further on in the same
// call.
struct PrepareNote {
    std::uintptr_t world = 0;
    std::optional<FxAction> ring;
};
thread_local PrepareNote t_prepare;

// Rows: eye L (and mono), eye R.
struct Counters {
    std::atomic<std::uint64_t> advanced[2]{};    // ring advances the engine ran
    std::atomic<std::uint64_t> reused{0};        // eye R prepares that kept eye L's ring (or would, counting)
    std::atomic<std::uint64_t> particles[2]{};   // particle generations the engine ran
    std::atomic<std::uint64_t> effects[2]{};     // effect generations the engine ran
    std::atomic<std::uint64_t> particlesLeft{0}; // eye R particle generations left to eye L (or would be)
    std::atomic<std::uint64_t> effectsLeft{0};
    std::atomic<std::uint64_t> poolKept{0};     // eye R prepares that kept eye L's light count (or would)
    std::atomic<std::uint64_t> particlesOwn{0}; // eye R generations of models eye L had not generated
    std::atomic<std::uint64_t> effectsOwn{0};
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;

// ETERNALVR_STEREO_FX_SYNC as set (empty when unset).
std::string requestedValue() {
    std::wstring value;
    std::string narrow;
    if (readEnv(L"ETERNALVR_STEREO_FX_SYNC", value)) {
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    }
    return narrow;
}

unsigned long long take(std::atomic<std::uint64_t>& counter) {
    return static_cast<unsigned long long>(counter.exchange(0, std::memory_order_relaxed));
}

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    EVR_LOG(
        "%s: eye R reused eye L's particles %llu time(s) (%llu particle system(s), %llu effect(s)), light "
        "pool kept %llu; eye R generated %llu / %llu itself (eye L had not); generated by eye L %llu / %llu, "
        "eye R %llu / %llu (particle systems / effects); ring advances eye L %llu, eye R %llu%s",
        kTag, take(g_counters.reused), take(g_counters.particlesLeft), take(g_counters.effectsLeft),
        take(g_counters.poolKept), take(g_counters.particlesOwn), take(g_counters.effectsOwn),
        take(g_counters.particles[0]), take(g_counters.effects[0]), take(g_counters.particles[1]),
        take(g_counters.effects[1]), take(g_counters.advanced[0]), take(g_counters.advanced[1]),
        g_mode == stereo_seq::FxSyncMode::Count ? " (ETERNALVR_STEREO_FX_SYNC=count: nothing reused)" : "");
}

FxAction actionNow(stereo_seq::Eye eye) {
    return stereo_seq::fxActionFor(g_mode, eye, mp_guard::allowsGameTouch());
}

template <typename T>
T read(std::uintptr_t at) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
    return value;
}

// One model's generation in a render whose answer is `render`, from the model's stamp and the ring's frame.
FxAction generationFor(FxAction render, std::uintptr_t ring, std::uint32_t stamp) {
    return stereo_seq::fxGenerationFor(render, stamp, read<std::uint32_t>(ring + kRingFrame));
}

// On the prepare's `call` of the advance: rcx is the ring (not null; the `je` before skips a world without).
void onPrepare(HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire)) {
        return;
    }
    const stereo_seq::Eye eye = seqChainEye();
    const FxAction action = actionNow(eye);
    t_prepare = PrepareNote{r.rdi, action};
    if (action != FxAction::UseEyeL) {
        ++g_counters.advanced[stereo_seq::eyeIndex(eye)];
        if (action == FxAction::RunCounted) {
            ++g_counters.reused;
        }
        report();
        return;
    }
    // Eye R: index, frame, fills and the atlas stay as eye L left them. Eye L's frame middle closed the
    // previous slot (its pointer 0); eye R's lens flares and tracers write into it, so it is opened again the
    // way the advance opens a slot, and eye R's own frame middle closes it.
    auto* ring = reinterpret_cast<std::byte*>(r.rcx);
    std::int32_t index = 0;
    std::memcpy(&index, ring + kRingIndex, sizeof(index));
    std::byte* slot = ring + kRingSlots +
                      static_cast<std::ptrdiff_t>(stereo_seq::fxPreviousSlot(index)) *
                          static_cast<std::ptrdiff_t>(kSlotSize);
    std::byte* previous = g_openSlot(slot);
    std::memcpy(ring + kRingPrevious, &previous, sizeof(previous));
    r.resumeAt = g_prepareResume;
    ++g_counters.reused;
    report();
}

// On the prepare's `call` of the light pool's reset (rdi is the world). It follows the ring hook of the same
// prepare (same thread, same world), so a world whose ring was advanced, or whose prepare had no ring hook
// act (no ring, or the hooks went live in between), gets the engine's reset.
void onPool(HookRegisters& r) {
    const PrepareNote note = t_prepare;
    t_prepare = PrepareNote{};
    if (!g_live.load(std::memory_order_acquire)) {
        return;
    }
    const FxAction action =
        stereo_seq::fxPoolResetFor(note.world == r.rdi ? note.ring : std::optional<FxAction>{});
    if (action == FxAction::Run) {
        return;
    }
    ++g_counters.poolKept;
    if (action == FxAction::UseEyeL) {
        r.resumeAt = g_poolResume; // eye R's show pass shows eye L's lights
    }
}

// Counts one generation; true when eye R leaves it to eye L.
bool leaveToEyeL(stereo_seq::Eye eye,
                 FxAction render,
                 FxAction generation,
                 std::atomic<std::uint64_t> (&ran)[2],
                 std::atomic<std::uint64_t>& left,
                 std::atomic<std::uint64_t>& own) {
    if (generation == FxAction::UseEyeL) {
        ++left;
        return true;
    }
    ++ran[stereo_seq::eyeIndex(eye)];
    if (generation == FxAction::RunCounted) {
        ++left;
    } else if (render != FxAction::Run) {
        ++own; // eye L did not generate this model in this tick
    }
    return false;
}

// On the particle update's `call` of the generation: rcx is the model, [r8] the ring.
void onParticles(HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire)) {
        return;
    }
    const stereo_seq::Eye eye = seqChainEye();
    const FxAction render = actionNow(eye);
    const FxAction generation =
        render == FxAction::Run
            ? FxAction::Run
            : generationFor(render, read<std::uintptr_t>(r.r8), read<std::uint32_t>(r.rcx + kParticleStamp));
    if (leaveToEyeL(eye, render, generation, g_counters.particles, g_counters.particlesLeft,
                    g_counters.particlesOwn)) {
        r.resumeAt = g_particleResume;
    }
}

// On the effect update's generation: r15 is the effect, rbp the update's frame ([[rbp + 0x28]] the ring).
void onEffects(HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire)) {
        return;
    }
    const stereo_seq::Eye eye = seqChainEye();
    const FxAction render = actionNow(eye);
    const FxAction generation =
        render == FxAction::Run
            ? FxAction::Run
            : generationFor(render, read<std::uintptr_t>(read<std::uintptr_t>(r.rbp + kEffectRingArg)),
                            read<std::uint32_t>(r.r15 + kEffectStamp));
    if (leaveToEyeL(eye, render, generation, g_counters.effects, g_counters.effectsLeft,
                    g_counters.effectsOwn)) {
        r.resumeAt = g_effectExit;
    }
}

bool matchesAt(const GameImage& image, const std::byte* at, const char* signature) {
    auto pattern = resolver::Pattern::parse(signature);
    return pattern && at && image.inText(at, pattern->size()) &&
           pattern->matchesAt(image.text, static_cast<std::size_t>(at - image.text.data()));
}

// The target of the rel32 `call` / `jmp` (E8 / E9) or `jcc` (0F 8x) at `at`; nullptr outside .text.
const std::byte* branchTarget(const GameImage& image, const std::byte* at) {
    if (!image.inText(at, 6)) {
        return nullptr;
    }
    const auto opcode = std::to_integer<unsigned>(at[0]);
    const std::byte* target = nullptr;
    if (opcode == 0xE8 || opcode == 0xE9) {
        target = at + 5 + readI32(at + 1);
    } else if (opcode == 0x0F && (std::to_integer<unsigned>(at[1]) & 0xF0) == 0x80) {
        target = at + 6 + readI32(at + 2);
    }
    return target && image.inText(target) ? target : nullptr;
}

struct Sites {
    const std::byte* prepare = nullptr;
    const std::byte* pool = nullptr;
    const std::byte* particle = nullptr;
    const std::byte* effect = nullptr; // the effect generation (the hook)
    const std::byte* effectExit = nullptr;
    const std::byte* opener = nullptr;
};

bool fail(const char* what) {
    EVR_LOG("%s: %s did not check out; not installed, eye L is a tick behind on CPU particles", kTag, what);
    return false;
}

bool locate(const GameImage& image, Sites& s) {
    const std::byte* prepare = findUnique(image, kTag, "world prepare's ring advance", kPrepareSignature);
    const std::byte* middle = findUnique(image, kTag, "frame middle's ring close", kMiddleSignature);
    const std::byte* particle = findUnique(image, kTag, "particle update's generation", kParticleSignature);
    const std::byte* effect = findUnique(image, kTag, "effect update", kEffectSignature);
    const std::byte* pool = findUnique(image, kTag, "world prepare's light pool reset", kPoolSignature);
    const std::byte* show = findUnique(image, kTag, "particle light show pass", kShowSignature);
    if (!prepare || !middle || !particle || !effect || !pool || !show) {
        return fail("a signature");
    }
    const std::byte* advance = branchTarget(image, prepare + kPrepareHook);
    if (!matchesAt(image, advance, kAdvanceCode)) {
        return fail("the ring advance (0x1A0F5D0)");
    }
    const std::byte* opener = branchTarget(image, advance + kAdvanceOpenCall);
    if (!matchesAt(image, opener, kOpenerCode)) {
        return fail("the slot opener (0x1BFC280)");
    }
    const std::byte* close = branchTarget(image, middle + kMiddleCloseCall);
    if (!matchesAt(image, close, kCloseCode) ||
        !matchesAt(image, branchTarget(image, close + kCloseJump), kCloserCode)) {
        return fail("the previous slot's close (0x1A0F580)");
    }
    if (!matchesAt(image, branchTarget(image, particle + kParticleBindCall), kBindPrologue) ||
        !matchesAt(image, branchTarget(image, particle + kParticleHook), kGeneratePrologue)) {
        return fail("the particle update's bind and generation calls");
    }
    const std::byte* lights = branchTarget(image, particle + kParticleLightsCall);
    if (pool + kPoolHook - (prepare + kPrepareHook) != kPoolFromPrepare ||
        !matchesAt(image, branchTarget(image, pool + kPoolHook), kPoolResetCode) || !lights ||
        !matchesAt(image, lights + kLightsPool, kLightsPoolCode) ||
        !matchesAt(image, lights + kLightsTake, kLightsTakeCode) ||
        !matchesAt(image, branchTarget(image, show + kShowCall), kShowCode)) {
        return fail("the particle light pool's reset (0x1953D80), slots or show pass");
    }
    const std::byte* exit = branchTarget(image, effect + kEffectSkipJump);
    const std::byte* generate = branchTarget(image, effect + kEffectStampBranch);
    if (!exit || exit != branchTarget(image, effect + kEffectSkipBranch) ||
        !matchesAt(image, exit, kEffectEpilogue) || !matchesAt(image, generate, kEffectGenerateCode) ||
        generate <= effect || generate >= exit) {
        return fail("the effect update's generation and epilogue");
    }
    s.prepare = prepare + kPrepareHook;
    s.pool = pool + kPoolHook;
    s.particle = particle + kParticleHook;
    s.effect = generate;
    s.effectExit = exit;
    s.opener = opener;
    g_prepareResume = reinterpret_cast<std::uintptr_t>(prepare + kPrepareResume);
    g_poolResume = reinterpret_cast<std::uintptr_t>(pool + kPoolResume);
    g_particleResume = reinterpret_cast<std::uintptr_t>(particle + kParticleResume);
    g_effectExit = reinterpret_cast<std::uintptr_t>(exit);
    g_openSlot = reinterpret_cast<SlotOpenFn>(const_cast<std::byte*>(opener));
    return true;
}

} // namespace

bool installFxSyncHooks() {
    std::call_once(g_once, [] {
        const std::string value = requestedValue();
        g_mode = stereo_seq::fxSyncMode(value);
        if (g_mode == stereo_seq::FxSyncMode::Off) {
            EVR_LOG("%s: off (ETERNALVR_STEREO_FX_SYNC=%s): eye L is a tick behind on CPU particles", kTag,
                    value.c_str());
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        GameImage image;
        Sites sites;
        if (!locateGameImage(image, kTag) || !locate(image, sites)) {
            return;
        }
        const struct {
            const std::byte* at;
            MidHookEditCallback callback;
        } hooks[] = {
            {sites.particle, &onParticles},
            {sites.effect, &onEffects},
            {sites.pool, &onPool},
            {sites.prepare, &onPrepare},
        };
        std::string error;
        for (const auto& h : hooks) {
            if (!installMidHookEdit(const_cast<std::byte*>(h.at), h.callback, error)) {
                // Hooks that went in stay in (they cannot be removed) but run the engine's code: g_live stays
                // false.
                EVR_LOG("%s: hook at RVA 0x%X failed: %s; not installed, eye L is a tick behind on CPU "
                        "particles",
                        kTag, image.rva(h.at), error.c_str());
                return;
            }
        }
        g_installed = true;
        g_live.store(true, std::memory_order_release);
        EVR_LOG(
            "%s: ring advance (RVA 0x%X), light pool reset (0x%X), particle (0x%X) and effect (0x%X, exit "
            "0x%X) generation hooked: %s",
            kTag, image.rva(sites.prepare), image.rva(sites.pool), image.rva(sites.particle),
            image.rva(sites.effect), image.rva(sites.effectExit),
            g_mode == stereo_seq::FxSyncMode::Count
                ? "counting only (ETERNALVR_STEREO_FX_SYNC=count); eye L is a tick behind on CPU particles"
                : "eye R draws eye L's particles and effects, so both eyes show them alike");
    });
    return g_installed;
}

} // namespace evr::vkcore
