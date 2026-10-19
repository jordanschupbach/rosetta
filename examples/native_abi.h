#ifndef ROSETTA_DEMO_NATIVE_ABI_H
#define ROSETTA_DEMO_NATIVE_ABI_H
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define RN_API __attribute__((visibility("default")))
#else
#define RN_API
#endif
#ifdef __cplusplus
#define RN_NOEXCEPT noexcept
extern "C" {
#else
#define RN_NOEXCEPT
#endif

#define RN_ABI_VERSION UINT32_C(1)
typedef int32_t rn_status;
enum { RN_OK = 0, RN_INVALID = 1, RN_OOM = 2, RN_RANGE = 3, RN_CALLBACK = 4, RN_BAD_ABI = 5 };
enum { RN_ADD = 1, RN_MULTIPLY = 2, RN_WITHOUT_EXCEPTIONS = 1, RN_WITHOUT_RTTI = 2 };
typedef struct rn_handle rn_handle;
/* Allocation callbacks never throw, return null on failure, and provide at
 * least max_align_t alignment. Their context must outlive every handle.
 * Handles own copies of input bytes; payload borrows until destroy.
 * Callbacks are synchronous and never retained. No exceptions/longjmp may
 * cross this ABI. Concurrent read-only calls require a live, unmodified owner;
 * destroy must be externally synchronized with all calls using the handle.
 * This ABI requires matching pointer/size widths and native byte order. */
typedef void* (*rn_allocate)(size_t size, void* context) RN_NOEXCEPT;
typedef void (*rn_deallocate)(void* memory, void* context) RN_NOEXCEPT;
typedef struct rn_allocator { rn_allocate allocate; rn_deallocate deallocate; void* context; } rn_allocator;
typedef struct rn_info {
    uint32_t version, flags, allocator_size, pointer_bits, size_bits, byte_order;
} rn_info;
typedef rn_status (*rn_transform)(int32_t value, void* context, int32_t* result) RN_NOEXCEPT;

RN_API rn_status rn_v1_info(uint32_t version, size_t result_size, rn_info* result) RN_NOEXCEPT;
RN_API rn_status rn_v1_create(uint32_t version, size_t allocator_size, const rn_allocator* allocator,
    uint32_t kind, int32_t parameter, const unsigned char* bytes, size_t length, rn_handle** result) RN_NOEXCEPT;
RN_API void rn_v1_destroy(rn_handle* handle) RN_NOEXCEPT;
RN_API uint64_t rn_v1_live(void) RN_NOEXCEPT;
RN_API rn_status rn_v1_apply(const rn_handle* handle, int32_t value, int32_t* result) RN_NOEXCEPT;
RN_API rn_status rn_v1_transform(const rn_handle* handle, int32_t value, rn_transform transform,
    void* context, int32_t* result) RN_NOEXCEPT;
RN_API rn_status rn_v1_payload(const rn_handle* handle, const unsigned char** bytes, size_t* length) RN_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif
