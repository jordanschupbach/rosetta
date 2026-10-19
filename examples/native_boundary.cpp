#include "demo.hpp"
#include "native_abi.h"
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>

namespace feature_library::abi {
class Owner {
public:
    rn_handle* handle = nullptr;
    ~Owner() noexcept { rn_v1_destroy(handle); }
};
}
namespace {
void* allocate(size_t size, void*) noexcept { try { return ::operator new(size); } catch (...) { return nullptr; } }
void deallocate(void* memory, void*) noexcept { ::operator delete(memory); }
void check(rn_status status) {
    switch (status) {
    case RN_OK: return;
    case RN_OOM: throw std::bad_alloc();
    case RN_RANGE: throw std::out_of_range("native ABI value out of range");
    case RN_CALLBACK: throw std::runtime_error("native ABI callback failed");
    case RN_BAD_ABI: throw std::runtime_error("native ABI version/size mismatch");
    default: throw std::invalid_argument("native ABI invalid argument");
    }
}
int32_t wire_value(long long value) {
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        throw std::out_of_range("native ABI value out of range");
    return static_cast<int32_t>(value);
}
struct Callback {
    const std::function<long long(long long)>& function;
    std::exception_ptr error;
};
rn_status transform(int32_t value, void* context, int32_t* result) noexcept {
    auto& callback = *static_cast<Callback*>(context);
    try { *result = wire_value(callback.function(value)); return RN_OK; }
    catch (...) { callback.error = std::current_exception(); return RN_CALLBACK; }
}
}
namespace demo {
std::map<std::string, unsigned long long> native_abi_info() {
    rn_info info{};
    check(rn_v1_info(RN_ABI_VERSION, sizeof(info), &info));
    return {{"version", info.version}, {"flags", info.flags}, {"allocator_size", info.allocator_size},
        {"pointer_bits", info.pointer_bits}, {"size_bits", info.size_bits}, {"byte_order", info.byte_order}};
}
NativeBoundary::NativeBoundary(BoundaryOperation operation, long long parameter, std::string payload)
    : native_(std::make_shared<feature_library::abi::Owner>()) {
    const rn_allocator allocator{allocate, deallocate, nullptr};
    check(rn_v1_create(RN_ABI_VERSION, sizeof(allocator), &allocator, static_cast<uint32_t>(operation), wire_value(parameter),
        reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), &native_->handle));
}
NativeBoundary::NativeBoundary(std::shared_ptr<feature_library::abi::Owner> native) : native_(std::move(native)) {}
long long NativeBoundary::apply(long long value) const {
    int32_t result = 0; check(rn_v1_apply(native_->handle, wire_value(value), &result)); return result;
}
long long NativeBoundary::with_callback(std::function<long long(long long)> callback, long long value) const {
    Callback context{callback, {}};
    int32_t result = 0;
    const auto status = rn_v1_transform(native_->handle, wire_value(value), transform, &context, &result);
    if (context.error) std::rethrow_exception(context.error); // after the C/noexcept boundary returns
    check(status);
    return result;
}
std::string NativeBoundary::payload() const {
    const unsigned char* data = nullptr; size_t size = 0;
    check(rn_v1_payload(native_->handle, &data, &size));
    return size ? std::string(reinterpret_cast<const char*>(data), size) : std::string{};
}
NativeBoundary NativeBoundary::share() const { return NativeBoundary(native_); }
unsigned long long NativeBoundary::live() { return rn_v1_live(); }
}
