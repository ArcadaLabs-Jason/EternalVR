#pragma once

// A minimal value-or-error type.
//
// std::expected would do the job, but it is C++23 and the project targets C++20 on three toolchains.
// This type covers only what the codebase needs: construct from a value or an error, test, and read.

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace evr {

// An error code from a module-specific enum together with a human-readable explanation. The message
// ends up in logs and diagnostics reports, so it should say what was wrong and where.
template <typename Code>
struct Error {
    Code code;
    std::string message;
};

template <typename T, typename Code>
class Result {
public:
    using ErrorType = Error<Code>;

    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(ErrorType error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool ok() const { return storage_.index() == 0; }
    explicit operator bool() const { return ok(); }

    [[nodiscard]] const T& value() const& {
        assert(ok());
        return std::get<0>(storage_);
    }
    [[nodiscard]] T& value() & {
        assert(ok());
        return std::get<0>(storage_);
    }
    [[nodiscard]] T&& value() && {
        assert(ok());
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] const ErrorType& error() const {
        assert(!ok());
        return std::get<1>(storage_);
    }

    const T& operator*() const& { return value(); }
    T& operator*() & { return value(); }
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }

private:
    std::variant<T, ErrorType> storage_;
};

// Shorthand for building the error side: `return fail(Code::Truncated, "section table past end");`
template <typename Code>
Error<Code> fail(Code code, std::string message) {
    return Error<Code>{code, std::move(message)};
}

} // namespace evr
