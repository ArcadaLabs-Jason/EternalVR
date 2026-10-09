#include "vkcore/fx_gpu_stages.hpp"

#include "stereo_seq/eye_tags.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/seq_hooks.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace evr::vkcore::fx_gpu {

namespace {

// The bind 0x1955670 (rcx the model, `mov r14, rcx` at +0xF). It loads the stage records' count `movsxd rax,
// [r14 + 0x628]` (+0x136, into r15), then loops (+0x1F0) over the records `[r14 + 0x620]` (0x50 bytes,
// `add rbp, 0x50`, `sub r15, 1`, `jne` back at +0x41D): the stage index `movsxd r10, [record + 0x1C]`, the
// stage `rbx = [r14 + 0x488] + index * 0x140`, and the GPU stage `[record + 8]` (0 for a CPU stage, `je` to
// the CPU half). A GPU stage (+0x2AB): the runtime `rcx = [r14 + 0x658]`, its entry's enabled byte `[[rcx +
// 0x10] + index * 0x38 + 8]` (`je` to the loop's step when 0), then `r9d = r8d = index`, `rdx = rbx`, `call`
// 0x1C2B670 (+0x2D9) and `jmp` to the step.
constexpr std::size_t kBindModel = 0xF;
constexpr const char* kBindModelCode = "4C 8B F1";
constexpr std::size_t kBindCount = 0x136;
constexpr const char* kBindCountCode = "49 63 86 28 06 00 00 F3 0F 59 C9 4C 8B F8";
constexpr std::size_t kBindLoop = 0x1F0;
constexpr const char* kBindLoopCode =
    "4D 8B 86 20 06 00 00 49 8B 86 E8 04 00 00 4D 63 54 28 1C 48 8B 80 10 01 "
    "00 00 49 8B CA 49 8B 54 28 08 4D 8B CA 48 C1 E1 05 4B 8D 1C 92 48 C1 E3 "
    "06 49 03 9E 88 04 00 00 4C 8B 1C 01 48 85 D2 0F 84 ?? ?? ?? ??";
constexpr std::size_t kBindGpu = 0x2AB;
constexpr const char* kBindGpuCode =
    "49 8B 83 90 06 00 00 48 89 03 49 8B 8E 58 06 00 00 4D 6B C9 38 48 8B 41 "
    "10 42 80 7C 08 08 00 0F 84 ?? ?? ?? ?? 45 8B CA 45 8B C2 48 8B D3 E8 ?? "
    "?? ?? ?? E9 ?? ?? ?? ??";
constexpr std::size_t kBindGpuSkip = 0x1F; // je rel32, within kBindGpuCode
constexpr std::size_t kBindGpuCall = 0x2E; // call 0x1C2B670
constexpr std::size_t kBindGpuNext = 0x33; // jmp rel32
constexpr std::size_t kBindStep = 0x41D;
constexpr const char* kBindStepCode = "48 83 C5 50 49 83 EF 01 0F 85 ?? ?? ?? ??";
constexpr std::size_t kBindStepBack = 8; // jne rel32 to the loop

// 0x1C2B670(runtime, stage, index, index): returns when `[runtime + 0x730]` is set or the entry's instance
// `[[runtime + 0x10] + index * 0x38 + 4]` is -1, else calls the append 0x1C295F0 (+0x3F) with the manager
// `[runtime + 8]`, the instance, the entry's GPU stage (+0x10), `[runtime + 0x728] + index * 0x188` and the
// second index. rdx is not read (edx is written before any use), the enabled byte is not checked here.
constexpr const char* kStageCode =
    "48 83 EC 38 80 B9 30 07 00 00 00 45 8B D1 75 34 41 8B C0 4C 6B C0 38 4C 03 "
    "41 10 41 8B 50 04 83 FA FF 74 20 4D 8B 40 10 4C 69 C8 88 01 00 00 44 89 54 "
    "24 20 4C 03 89 28 07 00 00 48 8B 49 08 E8 ?? ?? ?? ?? 48 83 C4 38 C3";
constexpr std::size_t kStageAppendCall = 0x3F;
// The append 0x1C295F0: draw list 0 (`lock xadd [rcx + 0x12048]`) unless the stage's +0x1BC is set, the
// light atlas list 2 (`lock xadd [rcx + 0x12050]`) when its +0x175 is set and r_particlesLightAtlas is on.
constexpr const char* kAppendCode =
    "41 83 B8 BC 01 00 00 00 41 BA 01 00 00 00 44 8B 5C 24 28 75 2A 41 8B C2 F0 0F C1 81 48 20 01 00 25 FF "
    "03 "
    "00 00 48 83 C0 03 48 8D 04 40 89 14 C1 44 89 5C C1 04 4C 89 44 C1 08 4C 89 4C C1 10 41 80 B8 75 01 00 "
    "00 "
    "00 74 3A 48 8B 05 ?? ?? ?? ?? 83 78 08 00 74 2D F0 44 0F C1 91 50 20 01 00 41 8B C2 25 FF 03 00 00 48 "
    "05 "
    "03 08 00 00 48 8D 04 40 89 14 C1 44 89 5C C1 04 4C 89 44 C1 08 4C 89 4C C1 10 C3";

// The generation 0x1953D90 (r14 the model) calls 0x1C2B6C0 with the runtime `[r14 + 0x658]` (+0x118A, the
// call at +0x1198). 0x1C2B6C0 reads the runtime's entries `[rcx + 0x10]` (+0x3C) and walks all `[rcx + 0x18]`
// of them, 0x38 bytes each, reading the instance at +4 (+0x10A).
constexpr std::size_t kGenerateRuntime = 0x118A;
constexpr const char* kGenerateRuntimeCode = "49 8B 8E 58 06 00 00 48 8D 95 D0 00 00 00 E8 ?? ?? ?? ??";
constexpr std::size_t kGenerateEmitCall = 0xE;
constexpr std::size_t kEmitEntries = 0x3C;
constexpr const char* kEmitEntriesCode = "48 63 32 4C 8B F9 48 8B 79 10";
constexpr std::size_t kEmitWalk = 0x10A;
constexpr const char* kEmitWalkCode =
    "4C 63 51 18 45 8B C6 0F 1F 40 00 66 66 66 0F 1F 84 00 00 00 00 00 48 8B 83 "
    "98 00 00 00 46 8B 0C 80 4D 85 D2 7E 1D 48 8B CF 49 8B D2 44 39 09 75 08 8B "
    "41 04 42 89 44 85 C8 48 83 C1 38 48 83 EA 01 75 E9";

// The GPU step 0x1C28DD0 (rcx the manager): flips the parity (+0x1205C), opens the upload slot, marks every
// instance not updated and reads the emitter record count (`cmp [rdi + 0x1CDF0], r10d` at +0xFC), then
// uploads the records; it writes none of the three counts. The hook is its first instruction. The frame-end
// job 0x1CBA1C0 reads a mode (`[[render system + 0xF38] + 0x3D4]`, 0x1CBA2FA): other than 1, it calls the
// per-world work 0x1CDA860 itself (0x1CBA321), which calls the step (0x1CDAA1D) inside the frame end; 1 skips
// that (0x1CBA31C) and 0x1CDAFE0 (0x1CBA366) -> 0x1CDBAE0 (0x1CDB18A) queues the step as the job 0x1CD6FA0
// (pointer at .data 0x39AB2C0, read at 0x1CDC1A1; one branch calls 0x1CDA860 at 0x1CDCBDB instead), which can
// run after the frame end has returned. The counts are zeroed only by the reset below at the start of the
// next render (and the manager's set-up, 0x1C2621D) and the step consumes the records, so it reads the render
// whose frame end called or queued it: seqFrameEndEye(), not the chain running then (with that, the rig
// showed the starved counts in eye L's row while the tiles were in eye R).
constexpr const char* kStepCode = "40 57 41 55 48 83 EC 58 8B 81 5C 20 01 00 48 8B F9 FF C0 25 01 00 00 80 "
                                  "7D 07 FF C8 83 C8 FE FF C0 F3 0F "
                                  "10 89 54 20 01 00 0F 57 E4 0F 2E CC 89 81 5C 20 01 00 66 0F 6E DA 0F 5B "
                                  "DB F3 0F 5E 1D ?? ?? ?? ?? 7A 05 "
                                  "75 03 0F 28 CB F3 0F 11 99 54 20 01 00 0F 28 C3 F3 0F 5C C1 48 98 0F 57 "
                                  "C9 0F 28 D0 F3 0F 10 05 ?? ?? ?? "
                                  "?? F3 0F 5D C2 0F 28 D0 0F 28 C1 F3 0F 5F C2 F3 0F 11 81 58 20 01 00 33 "
                                  "C9 48 89 8F 08 CE 01 00 48 6B C8 "
                                  "68 48 81 C1 70 4C 01 00 48 03 CF E8 ?? ?? ?? ?? 48 63 8F 5C 20 01 00 45 "
                                  "33 D2 48 6B D1 70 4C 8B E8 45 8B "
                                  "CA 48 8B 84 3A D0 20 01 00 48 89 44 24 70 44 39 97 98 26 01 00 7E 31 41 "
                                  "8B D2 4D 8B C5 0F 1F 40 00 48 8B "
                                  "87 D8 CD 01 00 48 8D 52 60 41 FF C1 4D 8D 40 30 8B 4C 02 CC 0F BA E9 1E "
                                  "41 89 48 D0 44 3B 8F 98 26 01 00 "
                                  "7C D9 44 89 54 24 78 44 39 97 F0 CD 01 00 0F 8E ?? ?? ?? ??";
// The reset 0x1C28320 (the world job's tail call, every render): draw lists 0 and 1 (qword at +0x12048),
// list 2 (+0x12050) and the emitter records (+0x1CDF0) to 0.
constexpr const char* kResetCode =
    "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 33 FF 48 8D 8B A8 8D "
    "01 00 48 89 BB 48 20 01 00 BA 00 04 00 00 89 BB 50 20 01 00 89 BB F0 CD 01 00";

// The particle system (the particle model).
constexpr std::size_t kModelStageInstances = 0x488; // 0x140 bytes each, by stage index
constexpr std::size_t kStageInstanceSize = 0x140;
constexpr std::size_t kModelRecords = 0x620; // the stage records the last generation wrote
constexpr std::size_t kModelRecordCount = 0x628;
constexpr std::size_t kRecordSize = 0x50;
constexpr std::size_t kRecordGpuStage = 8; // 0 for a CPU stage
constexpr std::size_t kRecordIndex = 0x1C;
constexpr std::size_t kModelRuntime = 0x658; // the GPU runtime
constexpr std::size_t kRuntimeEntries = 0x10;
constexpr std::size_t kRuntimeEntryCount = 0x18;
constexpr std::size_t kEntrySize = 0x38;
constexpr std::size_t kEntryInstance = 4; // -1: none
constexpr std::size_t kEntryEnabled = 8;
// The GPU particle manager.
constexpr std::size_t kManagerRecords = 0x1CDF0;
// Written only by the reset, the set-up and the append 0x1C295F0, which skips list 0 for a GPU stage with its
// +0x1BC set (`jne` at 0x1C29603) and so can leave list 0 empty with list 2 not.
constexpr std::size_t kManagerDrawList = 0x12048;  // list 0
constexpr std::size_t kManagerAtlasList = 0x12050; // list 2

using BindStageFn = void (*)(std::uintptr_t runtime,
                             std::uintptr_t stage,
                             std::uint32_t index,
                             std::uint32_t indexAgain);
BindStageFn g_bindStage = nullptr;
bool g_stepReadout = false;

struct StepSums {
    std::atomic<std::uint64_t> steps{0};
    std::atomic<std::uint64_t> records{0};
    std::atomic<std::uint64_t> drawn{0};
    std::atomic<std::uint64_t> atlas{0};
};
StepSums g_steps[2]; // eye L (and mono), eye R

template <typename T>
T read(std::uintptr_t at) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
    return value;
}

std::uint64_t countAt(std::uintptr_t at) {
    const std::int32_t value = read<std::int32_t>(at);
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

bool fail(const char* tag, const char* what) {
    EVR_LOG("%s: %s did not check out", tag, what);
    return false;
}

void onStep(const HookRegisters& r) {
    StepSums& sums = g_steps[stereo_seq::eyeIndex(seqFrameEndEye())];
    sums.records += countAt(r.rcx + kManagerRecords);
    sums.drawn += countAt(r.rcx + kManagerDrawList);
    sums.atlas += countAt(r.rcx + kManagerAtlasList);
    ++sums.steps;
}

} // namespace

bool locate(const GameImage& image, const std::byte* bind, const std::byte* generate, const char* tag) {
    if (!bind || !generate || !image.inText(bind, kBindStep + 0x10) ||
        !matchesAt(image, bind + kBindModel, kBindModelCode) ||
        !matchesAt(image, bind + kBindCount, kBindCountCode) ||
        !matchesAt(image, bind + kBindLoop, kBindLoopCode) ||
        !matchesAt(image, bind + kBindGpu, kBindGpuCode) ||
        !matchesAt(image, bind + kBindStep, kBindStepCode)) {
        return fail(tag, "the particle bind's GPU stage loop (0x1955860..0x1955A9B)");
    }
    const std::byte* step = bind + kBindStep;
    if (branchTarget(image, bind + kBindGpu + kBindGpuSkip) != step ||
        branchTarget(image, bind + kBindGpu + kBindGpuNext) != step ||
        branchTarget(image, step + kBindStepBack) != bind + kBindLoop) {
        return fail(tag, "the particle bind's GPU stage branches");
    }
    const std::byte* stage = branchTarget(image, bind + kBindGpu + kBindGpuCall);
    if (!matchesAt(image, stage, kStageCode) ||
        !matchesAt(image, branchTarget(image, stage + kStageAppendCall), kAppendCode)) {
        return fail(tag, "the GPU stage's bind (0x1C2B670) and its append (0x1C295F0)");
    }
    const std::byte* emit = nullptr;
    if (image.inText(generate, kGenerateRuntime + 0x20) &&
        matchesAt(image, generate + kGenerateRuntime, kGenerateRuntimeCode)) {
        emit = branchTarget(image, generate + kGenerateRuntime + kGenerateEmitCall);
    }
    if (!emit || !matchesAt(image, emit + kEmitEntries, kEmitEntriesCode) ||
        !matchesAt(image, emit + kEmitWalk, kEmitWalkCode)) {
        return fail(tag, "the GPU runtime's entries (0x1C2B6C0 from the generation)");
    }
    g_bindStage = reinterpret_cast<BindStageFn>(const_cast<std::byte*>(stage));
    return true;
}

bool hasInstance(std::uintptr_t model) {
    const auto runtime = read<std::uintptr_t>(model + kModelRuntime);
    if (runtime == 0) {
        return false;
    }
    const auto entries = read<std::uintptr_t>(runtime + kRuntimeEntries);
    const std::int32_t count = read<std::int32_t>(runtime + kRuntimeEntryCount);
    for (std::int32_t i = 0; entries != 0 && i < count; ++i) {
        if (read<std::int32_t>(entries + static_cast<std::uintptr_t>(i) * kEntrySize + kEntryInstance) !=
            -1) {
            return true;
        }
    }
    return false;
}

unsigned bindStages(std::uintptr_t model, bool call) {
    const auto runtime = read<std::uintptr_t>(model + kModelRuntime);
    const auto records = read<std::uintptr_t>(model + kModelRecords);
    const std::int32_t count = read<std::int32_t>(model + kModelRecordCount);
    if (runtime == 0 || records == 0) {
        return 0;
    }
    const auto entries = read<std::uintptr_t>(runtime + kRuntimeEntries);
    const std::int32_t entryCount = read<std::int32_t>(runtime + kRuntimeEntryCount);
    const auto stages = read<std::uintptr_t>(model + kModelStageInstances);
    unsigned bound = 0;
    for (std::int32_t i = 0; entries != 0 && i < count; ++i) {
        const std::uintptr_t record = records + static_cast<std::uintptr_t>(i) * kRecordSize;
        if (read<std::uintptr_t>(record + kRecordGpuStage) == 0) {
            continue; // a CPU stage: eye L's binding stays
        }
        // The bind does not check the index; a runtime entry outside the runtime's is skipped here.
        const std::int32_t index = read<std::int32_t>(record + kRecordIndex);
        if (index < 0 || index >= entryCount) {
            continue;
        }
        const auto at = static_cast<std::uintptr_t>(index);
        if (read<std::uint8_t>(entries + at * kEntrySize + kEntryEnabled) == 0) {
            continue;
        }
        if (call) {
            g_bindStage(runtime, stages + at * kStageInstanceSize, static_cast<std::uint32_t>(index),
                        static_cast<std::uint32_t>(index));
        }
        ++bound;
    }
    return bound;
}

bool installStepReadout(const GameImage& image, const char* tag) {
    const std::byte* step = findUnique(image, tag, "GPU particle step", kStepCode);
    const std::byte* reset = findUnique(image, tag, "GPU particle manager's reset", kResetCode);
    if (!step || !reset) {
        return fail(tag,
                    "the GPU particle step (0x1C28DD0) or the manager's reset (0x1C28320); no GPU readout");
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(step), &onStep, error)) {
        EVR_LOG("%s: GPU particle step hook (RVA 0x%X) failed: %s; no GPU readout", tag, image.rva(step),
                error.c_str());
        return false;
    }
    g_stepReadout = true;
    return true;
}

std::string takeStepReadout() {
    if (!g_stepReadout) {
        return {};
    }
    double average[2][3] = {};
    unsigned long long steps[2] = {};
    for (int eye = 0; eye < 2; ++eye) {
        StepSums& sums = g_steps[eye];
        steps[eye] = static_cast<unsigned long long>(sums.steps.exchange(0));
        const std::uint64_t sum[3] = {sums.records.exchange(0), sums.drawn.exchange(0),
                                      sums.atlas.exchange(0)};
        for (int i = 0; i < 3; ++i) {
            average[eye][i] =
                steps[eye] ? static_cast<double>(sum[i]) / static_cast<double>(steps[eye]) : 0.0;
        }
    }
    char text[256];
    std::snprintf(
        text, sizeof(text),
        "; GPU particle manager per render (emitter records / draw list / light atlas): eye L %.1f / "
        "%.1f / %.1f, eye R %.1f / %.1f / %.1f (%llu / %llu GPU steps)",
        average[0][0], average[0][1], average[0][2], average[1][0], average[1][1], average[1][2], steps[0],
        steps[1]);
    return text;
}

} // namespace evr::vkcore::fx_gpu
