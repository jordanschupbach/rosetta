#include "demo.hpp"
#include "coroutine_library.hpp"

namespace demo {
namespace coroutines = feature_library::coroutines;
Generator::Generator(int start, int count, std::string prefix, int fail_after)
    : native_(std::make_unique<coroutines::Sequence>(coroutines::create(start, count, std::move(prefix), fail_after))) {}
Generator::~Generator() noexcept = default;
bool Generator::advance() { return native_->advance(); }
std::string Generator::value() const { return native_->value(); }
bool Generator::done() const { return native_->done(); }
bool Generator::cancelled() const { return native_->cancelled(); }
bool Generator::cancel() { return native_->cancel(); }
unsigned long long Generator::frames() { return coroutines::Sequence::frames; }
unsigned long long Generator::bodies() { return coroutines::Sequence::bodies; }
unsigned long long Generator::allocations() { return coroutines::Sequence::allocations; }
unsigned long long Generator::releases() { return coroutines::Sequence::releases; }
} // namespace demo
