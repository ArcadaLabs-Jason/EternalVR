// The layer's command-line screen against the launcher's: one list of patterns
// (launcher/data/refused-args.txt, built in) and one file of cases both test suites read
// (tests/platform/mp_policy/argument-cases.txt).

#include "platform/mp_policy/mp_policy.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using evr::mp_policy::refusedArguments;
using evr::mp_policy::screenArguments;

namespace {

struct ArgumentCase {
    bool refuse = false;
    std::string arguments;
    std::string pattern; // for a refused case, the pattern that must name it
};

std::string trim(std::string_view s) {
    const std::size_t begin = s.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = s.find_last_not_of(" \t\r");
    return std::string(s.substr(begin, end - begin + 1));
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t bar = line.find('|', start);
        out.push_back(
            trim(std::string_view(line).substr(start, bar == std::string::npos ? bar : bar - start)));
        if (bar == std::string::npos) {
            return out;
        }
        start = bar + 1;
    }
}

std::vector<ArgumentCase> readCases() {
    std::ifstream file(EVR_MP_ARGUMENT_CASES);
    REQUIRE(file.good());
    std::vector<ArgumentCase> cases;
    std::string raw;
    while (std::getline(file, raw)) {
        const std::string line = trim(raw);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string> f = fields(line);
        CAPTURE(line);
        REQUIRE((f[0] == "refuse" || f[0] == "allow"));
        ArgumentCase c;
        c.refuse = f[0] == "refuse";
        REQUIRE(f.size() == (c.refuse ? 3U : 2U));
        c.arguments = f[1];
        c.pattern = c.refuse ? f[2] : std::string();
        cases.push_back(c);
    }
    return cases;
}

std::wstring widen(std::string_view s) {
    return {s.begin(), s.end()}; // the cases are ASCII
}

} // namespace

TEST_CASE("the shared argument cases are judged as the launcher judges them") {
    const std::vector<ArgumentCase> cases = readCases();
    CHECK(cases.size() > 40);
    for (const ArgumentCase& c : cases) {
        CAPTURE(c.arguments);
        const auto refused = screenArguments(widen(c.arguments));
        REQUIRE(refused.has_value() == c.refuse);
        if (refused) {
            CHECK(refused->pattern == c.pattern);
        }
    }
}

TEST_CASE("every built-in pattern is refused, also in its +set form") {
    REQUIRE(refusedArguments().size() >= 23);
    for (const auto& rule : refusedArguments()) {
        CAPTURE(rule.pattern);
        CHECK_FALSE(rule.reason.empty());
        const std::wstring pattern = widen(rule.pattern);
        CHECK(screenArguments(pattern + L" 1").has_value());
        if (!pattern.empty() && pattern.front() == L'+') {
            CHECK(screenArguments(L"+set " + pattern.substr(1) + L" 1").has_value());
        }
    }
}

TEST_CASE("the built-in list is the launcher's data file, in its order") {
    const auto rules = refusedArguments();
    REQUIRE(rules.size() >= 3);
    CHECK(rules.front().pattern == "game/pvp/");
    CHECK(rules.front().reason == "a BATTLEMODE map");
    CHECK(rules.back().pattern == "handle=");
}
