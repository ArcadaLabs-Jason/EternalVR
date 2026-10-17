#include "vkcore/game_text.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

using evr::vkcore::callReturns;
using evr::vkcore::findLeaReferences;
using evr::vkcore::GameImage;

namespace {

// A module of nops: 0x40 bytes of headers, a .text of `textSize` bytes, then 0x40 bytes of data. The
// bytes after .text exist, so an instruction written across its end reads real bytes and only the bounds
// keep it out.
struct FakeModule {
    explicit FakeModule(std::size_t textSize) : bytes(0x40 + textSize + 0x40, std::byte{0x90}) {
        image.base = bytes.data();
        image.size = bytes.size();
        image.text = evr::resolver::ByteSpan(bytes.data() + 0x40, textSize);
    }

    std::byte* text(std::size_t offset) { return bytes.data() + 0x40 + offset; }
    const std::byte* data(std::size_t offset) const {
        return bytes.data() + 0x40 + image.text.size() + offset;
    }

    void put(std::byte* at, std::initializer_list<std::uint8_t> values) {
        for (const std::uint8_t v : values) {
            *at++ = static_cast<std::byte>(v);
        }
    }
    void putRel32(std::byte* at, std::int32_t value) { std::memcpy(at, &value, sizeof(value)); }

    // `prefix 8D modrm disp32` at `at`, its disp32 pointing at `target`.
    void putLea(std::byte* at, std::uint8_t prefix, std::uint8_t modrm, const std::byte* target) {
        put(at, {prefix, 0x8D, modrm});
        putRel32(at + 3, static_cast<std::int32_t>(target - (at + 7)));
    }
    // `call target` at `at`.
    void putCall(std::byte* at, const std::byte* target) {
        put(at, {0xE8});
        putRel32(at + 1, static_cast<std::int32_t>(target - (at + 5)));
    }

    std::vector<std::byte> bytes;
    GameImage image;
};

std::uintptr_t after(const std::byte* at, std::size_t n) {
    return reinterpret_cast<std::uintptr_t>(at + n);
}

} // namespace

TEST_CASE("lea references at the first and the last start in .text, none past it") {
    FakeModule m(0x100);
    const std::byte* target = m.data(0x10);
    m.putLea(m.text(0), 0x48, 0x05, target);         // first byte of .text
    m.putLea(m.text(0x100 - 7), 0x4C, 0x0D, target); // ends on the last byte of .text
    CHECK(findLeaReferences(m.image, target) == std::vector<const std::byte*>{m.text(0), m.text(0x100 - 7)});

    FakeModule past(0x100);
    const std::byte* pastTarget = past.data(0x10);
    past.putLea(past.text(0x100 - 6), 0x48, 0x05, pastTarget); // its last disp byte is past .text
    CHECK(findLeaReferences(past.image, pastTarget).empty());
}

TEST_CASE("lea references take REX.W 48 and 4C with a RIP-relative ModRM only") {
    FakeModule m(0x100);
    const std::byte* target = m.data(0x20);
    m.putLea(m.text(0x10), 0x48, 0x05, target); // lea rax, [rip+disp32]
    m.putLea(m.text(0x20), 0x4C, 0x0D, target); // lea r9, [rip+disp32]
    m.putLea(m.text(0x30), 0x48, 0x3D, target); // lea rdi, [rip+disp32]
    m.putLea(m.text(0x40), 0x48, 0x45, target); // lea rax, [rbp+disp8]: not RIP-relative
    m.putLea(m.text(0x50), 0x49, 0x05, target); // REX.WB: not one of the two prefixes
    m.putLea(m.text(0x60), 0x48, 0x05, m.data(0x21));
    CHECK(findLeaReferences(m.image, target) ==
          std::vector<const std::byte*>{m.text(0x10), m.text(0x20), m.text(0x30)});

    FakeModule tiny(6);
    CHECK(findLeaReferences(tiny.image, tiny.data(0)).empty());
}

TEST_CASE("call returns at the first and the last start, none past .text or the span") {
    FakeModule m(0x100);
    const std::byte* target = m.text(0x80);
    m.putCall(m.text(0), target);         // first byte of .text
    m.putCall(m.text(0x100 - 5), target); // ends on the last byte of .text
    const std::vector<std::uintptr_t> both{after(m.text(0), 5), after(m.text(0x100 - 5), 5)};
    CHECK(callReturns(m.image, m.text(0), target, 0x100) == both);
    CHECK(callReturns(m.image, m.text(0), target, 0x100 + 0x40) == both); // a span past .text
    CHECK(callReturns(m.image, m.text(0), target, 0x100 - 1) == std::vector<std::uintptr_t>{both.front()});
    CHECK(callReturns(m.image, m.text(0), target, 5) == std::vector<std::uintptr_t>{both.front()});
    CHECK(callReturns(m.image, m.text(0), target, 4).empty());

    FakeModule past(0x100);
    const std::byte* pastTarget = past.text(0x80);
    past.putCall(past.text(0x100 - 4), pastTarget); // its last rel32 byte is past .text
    CHECK(callReturns(past.image, past.text(0), pastTarget, 0x200).empty());
    CHECK(callReturns(past.image, past.text(0x100 - 4), pastTarget, 0x200).empty());
}

TEST_CASE("call returns include a call inside another call's rel32") {
    FakeModule m(0x100);
    // E8 10 00 E8 00 00 00 00: a call whose rel32's third byte is the E8 of a call three bytes on.
    m.put(m.text(0x20), {0xE8, 0x10, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00});
    const std::byte* outer = m.text(0x20) + 5 + 0x00E80010;
    const std::byte* inner = m.text(0x23) + 5;
    CHECK(callReturns(m.image, m.text(0), outer, 0x100) ==
          std::vector<std::uintptr_t>{after(m.text(0x20), 5)});
    CHECK(callReturns(m.image, m.text(0), inner, 0x100) ==
          std::vector<std::uintptr_t>{after(m.text(0x23), 5)});
}

TEST_CASE("call returns are empty for a function outside .text") {
    FakeModule m(0x100);
    const std::byte* target = m.text(0x80);
    m.put(m.bytes.data() + 0x3F, {0xE8}); // the header byte before .text: a call into .text
    m.putRel32(m.bytes.data() + 0x40, static_cast<std::int32_t>(target - (m.bytes.data() + 0x3F + 5)));
    m.putCall(m.text(0x10), target);
    CHECK(callReturns(m.image, m.bytes.data() + 0x3F, target, 0x100).empty());
    CHECK(callReturns(m.image, m.text(0x100), target, 0x10).empty());
    CHECK(callReturns(m.image, m.text(0x100 - 4), target, 0x10).empty());
}
