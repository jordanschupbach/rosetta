#include "demo.hpp"
#include "callable_library.hpp"
namespace demo {
namespace native = feature_library::callables;
NativeCallable::NativeCallable(std::shared_ptr<native::Slot> slot) : native_(std::move(slot)) {}
std::string NativeCallable::invoke(int value) { return native_->invoke(value); }
bool NativeCallable::empty() const { return native_->empty(); }
void NativeCallable::clear() { native_->clear(); }
bool NativeCallable::same_slot(const NativeCallable& other) const { return native_ == other.native_; }
NativeCallable NativeCallable::copy() const { return NativeCallable(std::make_shared<native::Slot>(native_->copy())); }
NativeCallable NativeCallable::share() const { return NativeCallable(native_); }
int NativeCallable::live() { return native::Slot::alive.load(); }
int NativeCallable::captures() { return native::Capture::alive.load(); }
int NativeCallable::targets() { return native::Target::alive.load(); }
NativeCallable make_native_capture(std::string prefix, int seed) {
    return NativeCallable(std::make_shared<native::Slot>(native::make_capture(std::move(prefix), seed)));
}
NativeCallable make_native_bound(std::string prefix, int seed) {
    return NativeCallable(std::make_shared<native::Slot>(native::make_bound(std::move(prefix), seed)));
}
NativeCallable make_native_pointer() { return NativeCallable(std::make_shared<native::Slot>(native::make_pointer())); }
NativeCallable make_native_empty() { return NativeCallable(std::make_shared<native::Slot>(native::make_empty())); }
NativeCallable make_native_callback(std::function<std::string(int)> callback) {
    return NativeCallable(std::make_shared<native::Slot>(std::move(callback)));
}
}
