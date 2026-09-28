#include "platform/layer_gate/layer_gate.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <string_view>

using evr::layer_gate::decide;
using evr::layer_gate::Decision;
using evr::layer_gate::isTargetExecutable;

namespace {
constexpr std::wstring_view kGame = L"E:\\SteamLibrary\\steamapps\\common\\DOOMEternal\\DOOMEternalx64vk.exe";
}

TEST_CASE("target exe is matched by base name, ignoring case") {
    CHECK(isTargetExecutable(kGame));
    CHECK(isTargetExecutable(L"doometernalx64vk.EXE"));
    CHECK(isTargetExecutable(L"C:/games/DOOMEternalx64vk.exe"));
    CHECK_FALSE(isTargetExecutable(L"E:\\x\\doomSandBox\\DOOMSandBox64vk.exe"));
    CHECK_FALSE(isTargetExecutable(L"E:\\x\\DOOMEternalx64vk.exe.bak"));
    CHECK_FALSE(isTargetExecutable(L"E:\\DOOMEternalx64vk.exe\\steam.exe"));
    CHECK_FALSE(isTargetExecutable(L""));
}

TEST_CASE("layer enables only in the game with ETERNALVR_ENABLE_LAYER=1") {
    CHECK((decide(kGame, L"1", std::nullopt) == Decision::Enable));
    CHECK((decide(kGame, L"1", L"") == Decision::Enable));
    CHECK((decide(kGame, std::nullopt, std::nullopt) == Decision::NotEnabled));
    CHECK((decide(kGame, L"0", std::nullopt) == Decision::NotEnabled));
    CHECK((decide(kGame, L"true", std::nullopt) == Decision::NotEnabled));
    CHECK((decide(L"C:\\Windows\\explorer.exe", L"1", std::nullopt) == Decision::WrongProcess));
}

TEST_CASE("any non-empty ETERNALVR_DISABLE_LAYER wins") {
    CHECK((decide(kGame, L"1", L"1") == Decision::Disabled));
    CHECK((decide(kGame, L"1", L"0") == Decision::Disabled));
    CHECK((decide(kGame, std::nullopt, L"yes") == Decision::Disabled));
}
