#include "native_abi.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("native ABI check failed"); }
struct Allocation { int budget = 100; int outstanding = 0; };
void* allocate(size_t size, void* context) noexcept {
    auto& state = *static_cast<Allocation*>(context);
    if (state.budget-- <= 0) return nullptr;
    void* memory = std::malloc(size);
    if (memory) ++state.outstanding;
    return memory;
}
void release(void* memory, void* context) noexcept {
    --static_cast<Allocation*>(context)->outstanding; std::free(memory);
}
rn_status double_value(int32_t value, void*, int32_t* result) noexcept { *result = value * 2; return RN_OK; }
rn_status failure(int32_t, void*, int32_t*) noexcept { return RN_RANGE; }
struct API {
    decltype(&rn_v1_info) info; decltype(&rn_v1_create) create; decltype(&rn_v1_destroy) destroy;
    decltype(&rn_v1_live) live; decltype(&rn_v1_apply) apply; decltype(&rn_v1_transform) transform;
    decltype(&rn_v1_payload) payload;
};
void exercise(const API& api) {
    rn_info info{};
    check(api.info(1, sizeof(info), &info) == RN_OK && info.flags == 3 && info.version == 1);
    check(info.allocator_size == sizeof(rn_allocator) && info.pointer_bits == sizeof(void*) * 8);
    info.version = 42;
    check(api.info(2, sizeof(info), &info) == RN_BAD_ABI && info.version == 42);
    check(api.info(1, 0, &info) == RN_BAD_ABI && api.info(1, sizeof(info), nullptr) == RN_INVALID);
    Allocation state;
    const rn_allocator allocator{allocate, release, &state};
    const unsigned char bytes[] = {'a', 0, 'z'};
    rn_handle* handle = nullptr;
    for (int budget = 0; budget < 3; ++budget) {
        state.budget = budget;
        const auto status = api.create(1, sizeof(allocator), &allocator, RN_ADD, 20, bytes, 3, &handle);
        check(status == (budget < 2 ? RN_OOM : RN_OK));
        if (handle) {
            check(api.live() == 1);
            int32_t result = 99;
            check(api.apply(handle, 22, &result) == RN_OK && result == 42);
            check(api.apply(handle, std::numeric_limits<int32_t>::max(), &result) == RN_RANGE && result == 42);
            check(api.transform(handle, 1, double_value, nullptr, &result) == RN_OK && result == 42);
            check(api.transform(handle, 1, failure, nullptr, &result) == RN_CALLBACK && result == 42);
            check(api.transform(handle, 1, nullptr, nullptr, &result) == RN_INVALID);
            const unsigned char* copy = nullptr; size_t length = 0;
            check(api.payload(handle, &copy, &length) == RN_OK && length == 3 && std::memcmp(copy, bytes, 3) == 0 && copy != bytes);
            api.destroy(handle); handle = nullptr;
        }
        check(state.outstanding == 0 && api.live() == 0);
    }
    state.budget = 100;
    check(api.create(2, sizeof(allocator), &allocator, RN_ADD, 1, nullptr, 0, &handle) == RN_BAD_ABI && !handle);
    check(api.create(1, 0, &allocator, RN_ADD, 1, nullptr, 0, &handle) == RN_BAD_ABI);
    check(api.create(1, sizeof(allocator), nullptr, RN_ADD, 1, nullptr, 0, &handle) == RN_INVALID);
    check(api.create(1, sizeof(allocator), &allocator, 99, 1, nullptr, 0, &handle) == RN_INVALID);
    check(api.create(1, sizeof(allocator), &allocator, RN_ADD, 1, nullptr, 1, &handle) == RN_INVALID);
    check(api.create(1, sizeof(allocator), &allocator, RN_ADD, 1, bytes, 1024*1024+1, &handle) == RN_RANGE);
    check(api.create(1, sizeof(allocator), &allocator, RN_MULTIPLY, 3, nullptr, 0, &handle) == RN_OK);
    int32_t result = 99;
    check(api.apply(handle, 14, &result) == RN_OK && result == 42);
    check(api.apply(handle, std::numeric_limits<int32_t>::min(), &result) == RN_RANGE && result == 42);
    api.destroy(handle); api.destroy(nullptr);
    check(state.outstanding == 0 && api.live() == 0);
    check(api.apply(nullptr, 0, &result) == RN_INVALID);
}
struct Library { void* handle; ~Library() { if (handle) dlclose(handle); } };
}
int main(int argc, char** argv) {
    try {
        check(argc == 2);
        exercise({rn_v1_info, rn_v1_create, rn_v1_destroy, rn_v1_live, rn_v1_apply, rn_v1_transform, rn_v1_payload});
        Library library{dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)};
        check(library.handle != nullptr);
        API api{};
#define LOAD(name) api.name = reinterpret_cast<decltype(api.name)>(dlsym(library.handle, "rn_v1_" #name)); check(api.name != nullptr)
        LOAD(info); LOAD(create); LOAD(destroy); LOAD(live); LOAD(apply); LOAD(transform); LOAD(payload);
#undef LOAD
        check(dlsym(library.handle, "rn_internal_marker") == nullptr);
        exercise(api);
        std::cout << "Static and shared native ABI tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
