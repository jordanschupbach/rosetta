#pragma once

#include <charconv>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>
#include <type_traits>

namespace feature_library::errors {
enum class Kind { none, invalid_input, arithmetic, system, allocation, callback, unknown, nested };
struct Error {
    Kind kind;
    int code;
    std::string category, message;
    std::vector<std::string> causes;
    std::exception_ptr exception;
};
class DomainError : public std::runtime_error {
public:
    Kind kind;
    int code;
    std::string category;
    DomainError(Kind kind, int code, std::string category, std::string message)
        : std::runtime_error(std::move(message)), kind(kind), code(code), category(std::move(category)) {}
};
template<class T> class Result {
    std::variant<T, Error> result_;
    Result(std::in_place_index_t<0>, T value) : result_(std::in_place_index<0>, std::move(value)) { ++alive; }
    Result(std::in_place_index_t<1>, Error error) : result_(std::in_place_index<1>, std::move(error)) { ++alive; }
public:
    inline static int alive = 0;
    Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<decltype(result_)>)
        : result_(std::move(other.result_)) { ++alive; }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    ~Result() noexcept { --alive; }
    static Result success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result failure(Error error) { return Result(std::in_place_index<1>, std::move(error)); }
    bool ok() const { return result_.index() == 0; }
    const Error* error() const { return std::get_if<1>(&result_); }
    T value() const {
        if (ok()) return std::get<0>(result_);
        const auto& error = std::get<1>(result_);
        if (error.exception) std::rethrow_exception(error.exception);
        throw DomainError(error.kind, error.code, error.category, error.message);
    }
    T value_or(T fallback) const { return ok() ? std::get<0>(result_) : std::move(fallback); }
};
inline void causes(const std::exception& error, std::vector<std::string>& result, int depth = 0) {
    if (depth == 8) return;
    result.emplace_back(error.what());
    const auto* nested = dynamic_cast<const std::nested_exception*>(&error);
    if (!nested || !nested->nested_ptr()) return;
    try { std::rethrow_exception(nested->nested_ptr()); }
    catch (const std::exception& inner) { causes(inner, result, depth + 1); }
    catch (...) { if (result.size() < 8) result.emplace_back("unknown nested exception"); }
}
inline Error describe(Kind kind, int code, std::string category, const std::exception& error) {
    Error result{kind, code, std::move(category), error.what(), {}, std::current_exception()};
    causes(error, result.causes);
    return result;
}
template<class Function> Result<int> capture(Function&& operation, Kind fallback = Kind::unknown) {
    try { return Result<int>::success(operation()); }
    catch (const DomainError& error) { return Result<int>::failure(describe(error.kind, error.code, error.category, error)); }
    catch (const std::system_error& error) {
        return Result<int>::failure(describe(Kind::system, error.code().value(), error.code().category().name(), error));
    }
    catch (const std::bad_alloc& error) { return Result<int>::failure(describe(Kind::allocation, -2, "allocation", error)); }
    catch (const std::exception& error) { return Result<int>::failure(describe(fallback, -1, "exception", error)); }
    catch (...) {
        return Result<int>::failure({Kind::unknown, -1, "exception", "unknown C++ exception",
            {"unknown C++ exception"}, std::current_exception()});
    }
}
inline int parse(const std::string& text) {
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc::result_out_of_range)
        throw DomainError(Kind::invalid_input, 12, "demo.parse", "integer is out of range");
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw DomainError(Kind::invalid_input, 11, "demo.parse", "expected a complete decimal integer");
    return value;
}
inline int status_divide(int left, int right, int* result) noexcept {
    if (right == 0) return 1;
    if (left == std::numeric_limits<int>::min() && right == -1) return 2;
    *result = left / right;
    return 0;
}
inline int divide(int left, int right) {
    int value = 0;
    const int status = status_divide(left, right, &value);
    if (status) throw DomainError(Kind::arithmetic, status, "demo.divide", status == 1 ? "division by zero" : "division overflow");
    return value;
}
} // namespace feature_library::errors
