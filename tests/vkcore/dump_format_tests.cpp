#include "vkcore/dump_format.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

namespace dump = evr::vkcore::dump;

TEST_CASE("fnv1a64 matches the published test vectors") {
    CHECK(dump::fnv1a64("", 0) == 0xcbf29ce484222325ull);
    CHECK(dump::fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
    CHECK(dump::fnv1a64("foobar", 6) == 0x85944171f73967e8ull);
}

TEST_CASE("fnv1a64 separates modules that differ in one word") {
    const std::uint32_t a[] = {0x07230203, 0x00010000, 0, 5, 0};
    const std::uint32_t b[] = {0x07230203, 0x00010000, 0, 6, 0};
    CHECK(dump::fnv1a64(a, sizeof(a)) != dump::fnv1a64(b, sizeof(b)));
}

TEST_CASE("hex64 is sixteen zero-padded lower-case digits") {
    CHECK(dump::hex64(0) == "0000000000000000");
    CHECK(dump::hex64(0xABCull) == "0000000000000abc");
    CHECK(dump::hex64(0xFFFFFFFFFFFFFFFFull) == "ffffffffffffffff");
}

TEST_CASE("hexBytes keeps byte order") {
    const unsigned char bytes[] = {0x00, 0x7f, 0x80, 0xff};
    CHECK(dump::hexBytes(bytes, 4) == "007f80ff");
    CHECK(dump::hexBytes(bytes, 0).empty());
}

TEST_CASE("handleText prints handles without padding") {
    CHECK(dump::handleText(0) == "0x0");
    CHECK(dump::handleText(0x1a2b) == "0x1a2b");
    int object = 0;
    CHECK(dump::handleValue(&object) == reinterpret_cast<std::uintptr_t>(&object));
}

TEST_CASE("appendJsonString escapes quotes, backslashes and control characters") {
    std::string out;
    dump::appendJsonString(out, "main");
    CHECK(out == "\"main\"");
    out.clear();
    dump::appendJsonString(out, "a\"b\\c\n\x01");
    CHECK(out == "\"a\\\"b\\\\c\\u000a\\u0001\"");
}

TEST_CASE("parseCount accepts decimal counts only") {
    CHECK(dump::parseCount(L"", 60) == 60);
    CHECK(dump::parseCount(L"0", 60) == 0);
    CHECK(dump::parseCount(L"240", 60) == 240);
    CHECK(dump::parseCount(L"12x", 60) == 60);
    CHECK(dump::parseCount(L"-3", 60) == 60);
    CHECK(dump::parseCount(L"1234567890", 60) == 60);
}

TEST_CASE("the frame window covers [skip, skip + count)") {
    const dump::FrameWindow window(10, 3);
    CHECK_FALSE(window.contains(9));
    CHECK(window.contains(10));
    CHECK(window.contains(12));
    CHECK_FALSE(window.contains(13));
    CHECK_FALSE(window.finished(12));
    CHECK(window.finished(13));

    const dump::FrameWindow fromStart(0, 2);
    CHECK(fromStart.contains(0));
    CHECK(fromStart.finished(2));

    const dump::FrameWindow empty(5, 0);
    CHECK_FALSE(empty.contains(5));
    CHECK(empty.finished(5));
}

TEST_CASE("findInChain finds inline SPIR-V and module identifiers chained to a stage") {
    const std::uint32_t code[] = {0x07230203};
    VkPipelineShaderStageModuleIdentifierCreateInfoEXT id{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT};
    VkShaderModuleCreateInfo inlineCode{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, &id};
    inlineCode.codeSize = sizeof(code);
    inlineCode.pCode = code;
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, &inlineCode};

    const auto* found =
        dump::findInChain<VkShaderModuleCreateInfo>(stage.pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    REQUIRE(found == &inlineCode);
    CHECK(found->pCode == code);
    CHECK(dump::findInChain(stage.pNext,
                            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT) ==
          reinterpret_cast<const VkBaseInStructure*>(&id));
    CHECK(dump::findInChain(stage.pNext, VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR) == nullptr);
    CHECK(dump::findInChain(nullptr, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) == nullptr);
}
