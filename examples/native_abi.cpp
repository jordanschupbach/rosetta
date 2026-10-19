#include "native_abi.h"
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(__cpp_rtti) || defined(__GXX_RTTI)
#error This fixture must compile with exceptions and RTTI disabled
#endif

namespace { std::atomic<uint64_t> live{0}; }
struct rn_handle {
    rn_allocator allocator;
    unsigned char* bytes;
    size_t length;
    int32_t parameter;
    rn_handle(rn_allocator alloc, unsigned char* data, size_t size, int32_t value) noexcept
        : allocator(alloc), bytes(data), length(size), parameter(value) {}
    virtual rn_status apply(int32_t value, int32_t* result) const noexcept = 0;
    virtual void dispose() noexcept = 0;
    virtual ~rn_handle() noexcept = default;
};
namespace {
template<uint32_t Kind> struct Operation final : rn_handle {
    using rn_handle::rn_handle;
    rn_status apply(int32_t value, int32_t* result) const noexcept override {
        const int64_t computed = Kind == RN_ADD ? static_cast<int64_t>(parameter) + value : static_cast<int64_t>(parameter) * value;
        if (computed < std::numeric_limits<int32_t>::min() || computed > std::numeric_limits<int32_t>::max()) return RN_RANGE;
        *result = static_cast<int32_t>(computed);
        return RN_OK;
    }
    void dispose() noexcept override {
        const auto alloc = allocator;
        auto* data = bytes;
        void* storage = this;
        this->~Operation();
        if (data) alloc.deallocate(data, alloc.context);
        alloc.deallocate(storage, alloc.context);
        --live;
    }
};
static_assert(alignof(Operation<RN_ADD>) <= alignof(std::max_align_t));
static_assert(alignof(Operation<RN_MULTIPLY>) <= alignof(std::max_align_t));
template<uint32_t Kind> rn_status create(const rn_allocator& allocator, int32_t parameter,
    const unsigned char* bytes, size_t length, rn_handle** result) noexcept {
    void* storage = allocator.allocate(sizeof(Operation<Kind>), allocator.context);
    if (!storage) return RN_OOM;
    unsigned char* copy = nullptr;
    if (length) {
        copy = static_cast<unsigned char*>(allocator.allocate(length, allocator.context));
        if (!copy) { allocator.deallocate(storage, allocator.context); return RN_OOM; }
        std::memcpy(copy, bytes, length);
    }
    *result = ::new(storage) Operation<Kind>(allocator, copy, length, parameter);
    ++live;
    return RN_OK;
}
}
rn_status rn_v1_info(uint32_t version, size_t result_size, rn_info* result) noexcept {
    if (version != RN_ABI_VERSION || result_size != sizeof(rn_info)) return RN_BAD_ABI;
    if (!result) return RN_INVALID;
    const uint32_t word = UINT32_C(0x01020304);
    const auto first = *reinterpret_cast<const unsigned char*>(&word);
    *result = {RN_ABI_VERSION, RN_WITHOUT_EXCEPTIONS | RN_WITHOUT_RTTI, static_cast<uint32_t>(sizeof(rn_allocator)),
        static_cast<uint32_t>(sizeof(void*) * CHAR_BIT), static_cast<uint32_t>(sizeof(size_t) * CHAR_BIT),
        first == 4 ? UINT32_C(1) : first == 1 ? UINT32_C(2) : UINT32_C(0)};
    return RN_OK;
}
rn_status rn_v1_create(uint32_t version, size_t allocator_size, const rn_allocator* allocator, uint32_t kind,
    int32_t parameter, const unsigned char* bytes, size_t length, rn_handle** result) noexcept {
    if (!result) return RN_INVALID;
    *result = nullptr;
    if (version != RN_ABI_VERSION || allocator_size != sizeof(rn_allocator)) return RN_BAD_ABI;
    if (!allocator || !allocator->allocate || !allocator->deallocate || (length && !bytes)) return RN_INVALID;
    if (length > 1024 * 1024) return RN_RANGE;
    switch (kind) {
    case RN_ADD: return create<RN_ADD>(*allocator, parameter, bytes, length, result);
    case RN_MULTIPLY: return create<RN_MULTIPLY>(*allocator, parameter, bytes, length, result);
    default: return RN_INVALID;
    }
}
void rn_v1_destroy(rn_handle* handle) noexcept { if (handle) handle->dispose(); }
uint64_t rn_v1_live() noexcept { return live.load(); }
rn_status rn_v1_apply(const rn_handle* handle, int32_t value, int32_t* result) noexcept {
    if (!handle || !result) return RN_INVALID;
    return handle->apply(value, result);
}
rn_status rn_v1_transform(const rn_handle* handle, int32_t value, rn_transform transform, void* context, int32_t* result) noexcept {
    if (!handle || !transform || !result) return RN_INVALID;
    int32_t prepared = 0, transformed = 0;
    const auto status = handle->apply(value, &prepared);
    if (status != RN_OK) return status;
    if (transform(prepared, context, &transformed) != RN_OK) return RN_CALLBACK;
    *result = transformed;
    return RN_OK;
}
rn_status rn_v1_payload(const rn_handle* handle, const unsigned char** bytes, size_t* length) noexcept {
    if (!handle || !bytes || !length) return RN_INVALID;
    *bytes = handle->bytes; *length = handle->length;
    return RN_OK;
}
// This non-API symbol must stay hidden in the shared-library build.
extern "C" int rn_internal_marker() noexcept { return 42; }
