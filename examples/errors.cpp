#include "demo.hpp"
#include "error_library.hpp"

namespace demo {
namespace errors = feature_library::errors;
namespace {
IntResult wrap(errors::Result<int> result) { return IntResult(std::make_unique<errors::Result<int>>(std::move(result))); }
}
IntResult::IntResult(std::unique_ptr<errors::Result<int>> result) : native_(std::move(result)) {
    if (!native_) throw std::invalid_argument("result requires native storage");
}
IntResult::IntResult(IntResult&&) noexcept = default;
IntResult::~IntResult() noexcept = default;
bool IntResult::ok() const { return native_->ok(); }
int IntResult::value() const { return native_->value(); }
int IntResult::value_or(int fallback) const { return native_->value_or(fallback); }
ErrorKind IntResult::error_kind() const { const auto* error = native_->error(); return error ? static_cast<ErrorKind>(error->kind) : ErrorKind::none; }
int IntResult::code() const { const auto* error = native_->error(); return error ? error->code : 0; }
std::string IntResult::category() const { const auto* error = native_->error(); return error ? error->category : ""; }
std::string IntResult::message() const { const auto* error = native_->error(); return error ? error->message : ""; }
std::vector<std::string> IntResult::causes() const { const auto* error = native_->error(); return error ? error->causes : std::vector<std::string>{}; }
int IntResult::live() { return errors::Result<int>::alive; }
IntResult parse_result(std::string text) { return wrap(errors::capture([&] { return errors::parse(text); })); }
IntResult divide_result(int left, int right) { return wrap(errors::capture([&] { return errors::divide(left, right); })); }
IntResult status_divide_result(int left, int right) {
    int value = 0;
    const int status = errors::status_divide(left, right, &value);
    if (!status) return wrap(errors::Result<int>::success(value));
    return wrap(errors::Result<int>::failure({errors::Kind::arithmetic, status, "demo.divide",
        status == 1 ? "division by zero" : "division overflow", {}, {}}));
}
IntResult system_result(bool fail) {
    return wrap(errors::capture([&] {
        if (fail) throw std::system_error(std::make_error_code(std::errc::permission_denied), "operation denied");
        return 42;
    }));
}
IntResult error_code_result(bool fail) {
    const auto code = fail ? std::make_error_code(std::errc::permission_denied) : std::error_code{};
    if (!code) return wrap(errors::Result<int>::success(42));
    return wrap(errors::Result<int>::failure({errors::Kind::system, code.value(), code.category().name(), code.message(), {}, {}}));
}
IntResult status_result(bool fail, std::string detail) {
    if (!fail) return wrap(errors::Result<int>::success(42));
    return wrap(errors::Result<int>::failure({errors::Kind::invalid_input, 20, "demo.status", std::move(detail), {}, {}}));
}
IntResult nested_result() {
    return wrap(errors::capture([]() -> int {
        try { (void)errors::parse("invalid"); }
        catch (...) { std::throw_with_nested(std::runtime_error("request failed")); }
        return 0;
    }, errors::Kind::nested));
}
IntResult unknown_result() { return wrap(errors::capture([]() -> int { throw 42; })); }
IntResult allocation_result() { return wrap(errors::capture([]() -> int { throw std::bad_alloc(); })); }
IntResult callback_result(IntCallback callback, int value) {
    return wrap(errors::capture([&] { return callback(value); }, errors::Kind::callback));
}
} // namespace demo
