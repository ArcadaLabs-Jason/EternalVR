#include "vkcore/head_sweep_sites.hpp"

#include "vkcore/log.hpp"

#include <cstdint>
#include <cstring>

namespace evr::vkcore {

namespace {

constexpr const char* kTranslationSignature =
    "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 00 02 00 00 0F 29 "
    "70 "
    "B8 0F 29 78 A8 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 4C 8B B5 ?? ?? ?? ?? 4C 8B E2 4C 8B "
    "AD "
    "?? ?? ?? ?? 49 8B D8 4C 8B BD ?? ?? ?? ?? 48 8B F1 48 89 54 24 30 48 8B 95 ?? ?? ?? ??";
// Translation + 0x5AF, its synchronous path: `mov rax, [r10+disp32]` with r10 = *collision world, the
// physics object: its Havok world.
constexpr std::size_t kWorldLoadAt = 0x5AF;
constexpr const char* kWorldLoadSignature = "49 8B 82 ?? ?? ?? ?? 48 8B 88";
// The physics step's sync (RVA 0x50C7D0, `this` = the physics object): `cmp qword ptr [rbx+disp32], 0`
// (the Havok world) and `cmp byte ptr [rbx+disp32], 0` (a step in flight) before it waits for the step.
constexpr const char* kStepSyncSignature =
    "40 53 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 38 00 00 48 "
    "8B D9 BA 01 00 00 00 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 83 BB ?? ?? ?? ?? 00 74 ?? 80 BB ?? ?? ?? ?? 00 "
    "74";
constexpr std::size_t kStepSyncWorld = 0x36;
constexpr std::size_t kStepSyncFlag = 0x3F;
// Sync + 0x57: `mov rdx, [rip+disp32]`, the job it runs once the step is done (the post-step job below).
constexpr std::size_t kStepSyncJob = 0x57;
constexpr const char* kStepSyncJobSignature = "48 8B 15 ?? ?? ?? ?? 48 8D 4C 24 20 4C 8B C3 E8";
// The post-step job (RVA 0x502F30, `this` = the physics object), which every sync runs (the frame's job list
// through RVA 0x50C790 as well as RVA 0x50C7D0): `mov byte ptr [rbx+disp32], 0` clears the step's byte, then
// its own jobs run and are waited for. The hook goes on the instruction after that wait (+0x6C): the step is
// done and the world at rest, before the frame's next job.
constexpr const char* kPostStepSignature =
    "40 53 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 38 00 00 48 "
    "8B D9 33 D2 48 8D 4C 24 20 E8 ?? ?? ?? ?? 4C 8B 83 ?? ?? ?? ?? C6 83 ?? ?? ?? ?? 00 48 8B 05 ?? ?? ?? "
    "?? "
    "48 89 83 ?? ?? ?? ?? 4D 85 C0 74 ?? 48 8B 8B ?? ?? ?? ?? 48 8D 54 24 20 E8 ?? ?? ?? ?? 48 8D 4C 24 20 "
    "E8 "
    "?? ?? ?? ?? 48 8B 8C 24 60 38 00 00";
constexpr std::size_t kPostStepFlag = 0x39;
constexpr std::size_t kPostStepHook = 0x6C;
// In the post-step job: `mov r8, [rbx+disp32]` at +0x30 loads the physics object's pending task, and the call
// at +0x5D waits for it (RVA 0x50C910, below).
constexpr std::size_t kPostStepTask = 0x33;
constexpr std::size_t kPostStepWaitCall = 0x5D;
// The task wait (RVA 0x50C910, `(list, rdx, task r8)`): `mov eax, [r8+disp8]; mov ecx, eax; and cl, 7; cmp
// cl, 4; jne`: a state word whose low three bits are 4 is a finished task (no waiter is added).
constexpr const char* kTaskWaitSignature =
    "48 89 74 24 10 57 48 83 EC 20 49 8B F8 48 8B F2 41 8B 40 ?? 8B C8 80 E1 07 80 F9 04 75";
constexpr std::size_t kTaskWaitState = 0x13;
// Both begin with `mov rcx, [rip+disp32]`: the map-instance global.
constexpr const char* kThirdPersonSiteSignature =
    "48 8B 0D ?? ?? ?? ?? 8B 18 48 8B 01 FF 90 ?? ?? ?? ?? 45 33 E4 48 8D 0D ?? ?? ?? ?? 44 89 64 24 60 4C "
    "8D "
    "4D 80 4C 89 64 24 58 4C 8D 45 40 48 89 4C 24 50 48 8D 55 A8 44 89 64 24 48 48 8D 0D ?? ?? ?? ?? 89 5C "
    "24 "
    "40 89 74 24 38 48 89 4C 24 30 48 8D 4D 90 4C 89 74 24 28 48 89 4C 24 20 48 8B C8 E8 ?? ?? ?? ??";
constexpr const char* kWeaponSiteSignature = "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 44 89 64 24 60 "
                                             "4C 8D 4D A8 4C 89 64 24 58 4C 8D 45 F0 48 "
                                             "8B C8 48 8D 05 ?? ?? ?? ?? 48 89 44 24 50 8B 05 ?? ?? ?? ?? 44 "
                                             "89 64 24 48 89 44 24 40 48 8D 45 70 C7 44 "
                                             "24 38 01 00 14 00 48 89 44 24 30 48 8B 05 ?? ?? ?? ?? 48 8B 90 "
                                             "?? ?? ?? ?? 48 8D 45 88 48 89 54 24 28 48 "
                                             "8D 54 24 78 48 89 44 24 20 E8 ?? ?? ?? ??";
// In the weapon site: `mov rax, [rip+map instance]; mov rdx, [rax+disp32]` loads the shape it casts.
constexpr std::size_t kWeaponShapeLoad = 81;

std::uint32_t readU32(const std::byte* at) {
    std::uint32_t v = 0;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

} // namespace

std::optional<SweepSites> findSweepSites(const GameImage& image, const char* tag) {
    SweepSites s;
    s.translation = findUnique(image, tag, "collision Translation", kTranslationSignature);
    const std::byte* third = findUnique(image, tag, "third-person trace site", kThirdPersonSiteSignature);
    const std::byte* weapon = findUnique(image, tag, "weapon view trace site", kWeaponSiteSignature);
    if (!s.translation || !third || !weapon) {
        EVR_LOG("%s: collision query not found; no head sweep (the lean cap still applies)", tag);
        return std::nullopt;
    }
    s.mapInstanceGlobal = ripTarget(image, third + 3, third + 7);
    if (!s.mapInstanceGlobal || s.mapInstanceGlobal != ripTarget(image, weapon + 3, weapon + 7)) {
        EVR_LOG("%s: the two trace sites read different map-instance globals; no head sweep", tag);
        return std::nullopt;
    }
    // When the world may be queried (head_sweep.cpp): without the step's byte and the post-step job no query
    // is made, as a query at the wrong time walks the world while the physics step changes it.
    const std::byte* sync = findUnique(image, tag, "physics step sync", kStepSyncSignature);
    const std::byte* post = findUnique(image, tag, "physics post-step job", kPostStepSignature);
    const std::byte* worldLoad = s.translation + kWorldLoadAt;
    const std::byte* job = sync ? sync + kStepSyncJob : nullptr;
    const std::byte* jobSlot =
        matchesAt(image, job, kStepSyncJobSignature) ? ripTarget(image, job + 3, job + 7) : nullptr;
    std::uint64_t jobTarget = 0;
    if (jobSlot && image.contains(jobSlot, sizeof(jobTarget))) {
        std::memcpy(&jobTarget, jobSlot, sizeof(jobTarget));
    }
    if (!sync || !post || !matchesAt(image, worldLoad, kWorldLoadSignature) ||
        readU32(sync + kStepSyncWorld) != readU32(worldLoad + 3) ||
        jobTarget != reinterpret_cast<std::uintptr_t>(post) ||
        readU32(sync + kStepSyncFlag) != readU32(post + kPostStepFlag)) {
        EVR_LOG("%s: the physics step's sync is not where expected; no head sweep (the lean cap still "
                "applies)",
                tag);
        return std::nullopt;
    }
    const std::byte* wait = branchTarget(image, post + kPostStepWaitCall);
    if (!matchesAt(image, wait, kTaskWaitSignature)) {
        EVR_LOG("%s: the post-step job's task wait is not where expected; no head sweep (the lean cap still "
                "applies)",
                tag);
        return std::nullopt;
    }
    s.postStep = post + kPostStepHook;
    s.stepFlagDisp = readU32(sync + kStepSyncFlag);
    s.taskDisp = readU32(post + kPostStepTask);
    s.taskStateDisp = static_cast<std::uint8_t>(wait[kTaskWaitState]);
    if (s.stepFlagDisp == 0 || s.stepFlagDisp > 0x100000 || s.taskDisp == 0 || s.taskDisp > 0x100000) {
        EVR_LOG("%s: the physics step's sync reads implausible fields; no head sweep", tag);
        return std::nullopt;
    }
    const std::byte* load = weapon + kWeaponShapeLoad;
    if (load[0] == std::byte{0x48} && load[1] == std::byte{0x8B} && load[2] == std::byte{0x05} &&
        load[7] == std::byte{0x48} && load[8] == std::byte{0x8B} && load[9] == std::byte{0x90} &&
        ripTarget(image, load + 3, load + 7) == s.mapInstanceGlobal) {
        s.weaponShapeDisp = readI32(load + 10);
    } else {
        EVR_LOG("%s: the weapon trace's shape load is not where expected; shots are traced with the 0.16 m "
                "sphere",
                tag);
    }
    return s;
}

} // namespace evr::vkcore
