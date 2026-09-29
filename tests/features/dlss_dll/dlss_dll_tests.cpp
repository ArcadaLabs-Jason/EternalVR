#include "features/dlss_dll/dlss_dll.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

using evr::dlss_dll::checkPeHeaders;
using evr::dlss_dll::decide;
using evr::dlss_dll::Decision;
using evr::dlss_dll::FileFacts;
using evr::dlss_dll::folderOf;
using evr::dlss_dll::hasDllName;
using evr::dlss_dll::kFirstPresetVersion;
using evr::dlss_dll::kGameDllVersion;
using evr::dlss_dll::parsePreset;
using evr::dlss_dll::parseRoute;
using evr::dlss_dll::PeCheck;
using evr::dlss_dll::presetName;
using evr::dlss_dll::Route;
using evr::dlss_dll::samePath;
using evr::dlss_dll::shouldRedirect;
using evr::dlss_dll::Version;
using evr::dlss_dll::versionFromFixed;
using evr::dlss_dll::versionText;

namespace {

// The first bytes of a PE file: MZ, e_lfanew at 0x3C, then "PE\0\0" and the file header.
std::array<std::uint8_t, 0x100> peHeaders(std::uint16_t machine, std::uint16_t characteristics) {
    std::array<std::uint8_t, 0x100> b{};
    b[0] = 'M';
    b[1] = 'Z';
    b[0x3C] = 0x80;
    b[0x80] = 'P';
    b[0x81] = 'E';
    b[0x84] = static_cast<std::uint8_t>(machine);
    b[0x85] = static_cast<std::uint8_t>(machine >> 8);
    b[0x84 + 18] = static_cast<std::uint8_t>(characteristics);
    b[0x84 + 19] = static_cast<std::uint8_t>(characteristics >> 8);
    return b;
}

FileFacts goodDll(Version v) {
    FileFacts f;
    f.exists = true;
    f.nameOk = true;
    f.pe = PeCheck::Ok;
    f.hasVersion = true;
    f.version = v;
    f.company = "NVIDIA Corporation";
    return f;
}

constexpr Version kDlss310{310, 2, 1, 0};

} // namespace

TEST_CASE("versions come from the fixed file info and compare field by field") {
    const Version v = versionFromFixed(0x01360002u, 0x00010000u);
    CHECK((versionText(v) == "310.2.1.0"));
    CHECK(v > kGameDllVersion);
    CHECK(kGameDllVersion < kFirstPresetVersion);
    CHECK(Version{3, 1, 0, 0} >= kFirstPresetVersion);
    CHECK(Version{2, 5, 1, 0} < kFirstPresetVersion);
    CHECK(Version{3, 0, 13, 0} < kFirstPresetVersion);
    CHECK((versionText(kGameDllVersion) == "2.3.0.0"));
}

TEST_CASE("only a file named nvngx_dlss.dll is taken, and its folder goes to NGX") {
    CHECK(hasDllName(L"E:\\dlss\\nvngx_dlss.dll"));
    CHECK(hasDllName(L"E:/dlss/NVNGX_DLSS.DLL"));
    CHECK(hasDllName(L"nvngx_dlss.dll"));
    CHECK_FALSE(hasDllName(L"E:\\dlss\\nvngx_dlss_310.dll"));
    CHECK_FALSE(hasDllName(L"E:\\dlss\\nvngx_dlssg.dll"));
    CHECK_FALSE(hasDllName(L"E:\\nvngx_dlss.dll\\"));
    CHECK_FALSE(hasDllName(L""));

    CHECK((folderOf(L"E:\\Mods\\DLSS 310\\nvngx_dlss.dll") == L"E:\\Mods\\DLSS 310"));
    CHECK((folderOf(L"E:/Mods//nvngx_dlss.dll") == L"E:/Mods"));
    CHECK((folderOf(L"E:\\nvngx_dlss.dll") == L"E:\\"));
    CHECK(folderOf(L"nvngx_dlss.dll").empty());
    CHECK((folderOf(L"\\\\server\\share\\nvngx_dlss.dll") == L"\\\\server\\share"));

    CHECK(samePath(L"E:\\Mods\\nvngx_dlss.dll", L"e:/mods/NVNGX_DLSS.dll"));
    CHECK_FALSE(samePath(L"E:\\Mods\\nvngx_dlss.dll", L"E:\\SteamLibrary\\nvngx_dlss.dll"));
}

TEST_CASE("the PE check wants a 64-bit DLL") {
    auto dll = peHeaders(0x8664, 0x2022);
    CHECK(checkPeHeaders(dll.data(), dll.size()) == PeCheck::Ok);
    auto exe = peHeaders(0x8664, 0x0022);
    CHECK(checkPeHeaders(exe.data(), exe.size()) == PeCheck::NotDll);
    auto x86 = peHeaders(0x014C, 0x2102);
    CHECK(checkPeHeaders(x86.data(), x86.size()) == PeCheck::NotX64);
    auto text = dll;
    text[0] = 'H';
    CHECK(checkPeHeaders(text.data(), text.size()) == PeCheck::NotMz);
    auto noPe = dll;
    noPe[0x80] = 'N';
    CHECK(checkPeHeaders(noPe.data(), noPe.size()) == PeCheck::NotPe);
    CHECK(checkPeHeaders(dll.data(), 0x40) == PeCheck::TooShort);
    CHECK(checkPeHeaders(dll.data(), 0x90) == PeCheck::TooShort); // the file header is cut off
    CHECK(checkPeHeaders(nullptr, 0) == PeCheck::TooShort);
    auto far = dll;
    far[0x3C] = 0xFF;
    far[0x3F] = 0x7F; // e_lfanew far past the end
    CHECK(checkPeHeaders(far.data(), far.size()) == PeCheck::TooShort);
}

TEST_CASE("presets: default or a letter NVIDIA defines") {
    CHECK((parsePreset("") == std::optional<std::uint32_t>(0u)));
    CHECK((parsePreset("default") == std::optional<std::uint32_t>(0u)));
    CHECK((parsePreset(" Default ") == std::optional<std::uint32_t>(0u)));
    CHECK((parsePreset("K") == std::optional<std::uint32_t>(11u)));
    CHECK((parsePreset("k") == std::optional<std::uint32_t>(11u)));
    CHECK((parsePreset("J") == std::optional<std::uint32_t>(10u)));
    CHECK((parsePreset("F") == std::optional<std::uint32_t>(6u)));
    CHECK((parsePreset("A") == std::optional<std::uint32_t>(1u)));
    CHECK((parsePreset("L") == std::optional<std::uint32_t>(12u)));
    CHECK((parsePreset("m") == std::optional<std::uint32_t>(13u)));
    for (const char* doNotUse : {"G", "H", "I", "N", "O"}) {
        CHECK_FALSE(parsePreset(doNotUse).has_value());
    }
    CHECK_FALSE(parsePreset("Z").has_value());
    CHECK_FALSE(parsePreset("KK").has_value());
    CHECK_FALSE(parsePreset("11").has_value());
    CHECK_FALSE(parsePreset("transformer").has_value());
    CHECK((presetName(0) == "default"));
    CHECK((presetName(11) == "K"));
    CHECK((presetName(10) == "J"));
    CHECK((presetName(99) == "default"));
}

TEST_CASE("a newer NVIDIA DLL is used, and preset K is applied with it") {
    const Decision d = decide(goodDll(kDlss310), "K");
    CHECK(d.use);
    CHECK(d.reason.empty());
    CHECK(d.applyPreset);
    CHECK(d.preset == 11u);
    CHECK(d.presetNote.empty());

    const Decision plain = decide(goodDll(kDlss310), "default");
    CHECK(plain.use);
    CHECK_FALSE(plain.applyPreset);
    CHECK(plain.preset == 0u);
    CHECK(plain.presetNote.empty());
}

TEST_CASE("files that are not a usable DLSS DLL are refused with the reason") {
    FileFacts missing;
    CHECK_FALSE(decide(missing, "").use);
    CHECK((decide(missing, "").reason == "the file does not exist"));

    FileFacts renamed = goodDll(kDlss310);
    renamed.nameOk = false;
    CHECK_FALSE(decide(renamed, "").use);
    CHECK((decide(renamed, "").reason == "the file is not named nvngx_dlss.dll"));

    FileFacts x86 = goodDll(kDlss310);
    x86.pe = PeCheck::NotX64;
    CHECK_FALSE(decide(x86, "").use);
    CHECK((decide(x86, "").reason == "the file is not a 64-bit (x86-64) file"));

    FileFacts bare = goodDll(kDlss310);
    bare.hasVersion = false;
    CHECK_FALSE(decide(bare, "").use);

    FileFacts other = goodDll(kDlss310);
    other.company = "Someone Else";
    const Decision o = decide(other, "K");
    CHECK_FALSE(o.use);
    CHECK(o.reason.find("NVIDIA") != std::string::npos);
    // No preset without the newer DLL.
    CHECK_FALSE(o.applyPreset);
    CHECK_FALSE(o.presetNote.empty());
}

TEST_CASE("a DLL before DLSS 3.1 gets no preset, and an older one than the game's is noted") {
    const Decision noPresets = decide(goodDll({2, 5, 1, 0}), "K");
    CHECK(noPresets.use);
    CHECK(noPresets.reason.empty());
    CHECK_FALSE(noPresets.applyPreset);
    CHECK(noPresets.presetNote.find("3.1.0.0") != std::string::npos);

    const Decision old = decide(goodDll({2, 2, 6, 0}), "");
    CHECK(old.use);
    CHECK(old.reason.find("not newer") != std::string::npos);

    const Decision same = decide(goodDll(kGameDllVersion), "");
    CHECK(same.use);
    CHECK_FALSE(same.reason.empty());

    const Decision first = decide(goodDll(kFirstPresetVersion), "J");
    CHECK(first.applyPreset);
    CHECK(first.preset == 10u);
}

TEST_CASE("an unknown preset keeps the default and says so") {
    const Decision d = decide(goodDll(kDlss310), "Q");
    CHECK(d.use);
    CHECK_FALSE(d.applyPreset);
    CHECK(d.preset == 0u);
    CHECK(d.presetNote.find("unknown preset 'Q'") != std::string::npos);
}

TEST_CASE("the route: the search path by default, or the redirect of nvngx_dlss.dll loads") {
    CHECK((parseRoute("") == std::optional<Route>(Route::FolderFirst)));
    CHECK((parseRoute("path") == std::optional<Route>(Route::FolderFirst)));
    CHECK((parseRoute("redirect") == std::optional<Route>(Route::Redirect)));
    CHECK_FALSE(parseRoute("hook").has_value());

    const std::wstring chosen = L"E:\\Mods\\DLSS\\nvngx_dlss.dll";
    CHECK(shouldRedirect(L"E:\\SteamLibrary\\steamapps\\common\\DOOMEternal\\nvngx_dlss.dll", chosen));
    CHECK(shouldRedirect(L"nvngx_dlss.dll", chosen));
    CHECK(shouldRedirect(L"./NVNGX_DLSS.DLL", chosen));
    CHECK_FALSE(shouldRedirect(L"e:/mods/dlss/nvngx_dlss.dll", chosen)); // already the chosen one
    CHECK_FALSE(shouldRedirect(L"E:\\Game\\nvngx_dlssg.dll", chosen));
    CHECK_FALSE(shouldRedirect(L"C:\\Windows\\System32\\kernel32.dll", chosen));
    CHECK_FALSE(shouldRedirect(L"E:\\Game\\nvngx_dlss.dll", L""));
}
