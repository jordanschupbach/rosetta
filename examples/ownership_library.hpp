#pragma once

#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace feature_library::ownership {
struct alignas(64) Resource {
    inline static unsigned long long alive = 0, allocations = 0, releases = 0, deleted = 0;
    inline static int last_tag = 0;
    std::string text;
    Resource(std::string value, bool fail) : text(std::move(value)) {
        if (fail) throw std::runtime_error("resource construction failed");
        ++alive;
    }
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    ~Resource() noexcept { --alive; }
};
inline void release_storage(void* memory) noexcept {
    ::operator delete(memory, std::align_val_t{alignof(Resource)});
    ++Resource::releases;
}
struct Deleter {
    int tag = 0;
    void operator()(Resource* resource) const noexcept {
        if (!resource) return;
        std::destroy_at(resource);
        Resource::last_tag = tag;
        ++Resource::deleted;
        release_storage(resource);
    }
};
using Unique = std::unique_ptr<Resource, Deleter>;
inline Unique create(std::string text, int tag, bool fail) {
    void* storage = ::operator new(sizeof(Resource), std::align_val_t{alignof(Resource)});
    ++Resource::allocations;
    try { return Unique(::new (storage) Resource(std::move(text), fail), Deleter{tag}); }
    catch (...) { release_storage(storage); throw; }
}
inline const Resource& require(const Unique& resource) {
    if (!resource) throw std::runtime_error("resource slot is empty");
    return *resource;
}
class SharedSlot;
class UniqueSlot {
    Unique resource_;
public:
    UniqueSlot() = default;
    UniqueSlot(std::string text, int tag, bool fail) : resource_(create(std::move(text), tag, fail)) {}
    bool has_value() const { return static_cast<bool>(resource_); }
    std::string text() const { return require(resource_).text; }
    bool aligned() const { return reinterpret_cast<std::uintptr_t>(&require(resource_)) % alignof(Resource) == 0; }
    int tag() const { (void)require(resource_); return resource_.get_deleter().tag; }
    void reset() noexcept { resource_.reset(); }
    void move_to(UniqueSlot& destination) {
        if (this == &destination) throw std::invalid_argument("cannot transfer a slot to itself");
        (void)require(resource_);
        if (destination.has_value()) throw std::runtime_error("destination resource slot is not empty");
        destination.resource_ = std::move(resource_); // nonthrowing commit, including deleter state
    }
    void promote_to(SharedSlot& destination);
};
class SharedControl {
    friend class UniqueSlot;
    friend class SharedSlot;
    Unique resource;
public:
    SharedControl() = default;
    ~SharedControl() noexcept = default;
};
class WeakSlot;
class SharedSlot {
    friend class UniqueSlot;
    friend class WeakSlot;
    std::shared_ptr<SharedControl> owner_;
    explicit SharedSlot(std::shared_ptr<SharedControl> owner) : owner_(std::move(owner)) {}
public:
    SharedSlot() = default;
    bool has_value() const { return owner_ && owner_->resource; }
    std::string text() const {
        if (!owner_) throw std::runtime_error("shared resource slot is empty");
        return require(owner_->resource).text;
    }
    int tag() const {
        if (!owner_) throw std::runtime_error("shared resource slot is empty");
        return owner_->resource.get_deleter().tag;
    }
    void reset() noexcept { owner_.reset(); }
    SharedSlot clone() const { return SharedSlot(owner_); }
    WeakSlot weak() const;
};
class WeakSlot {
    std::weak_ptr<SharedControl> owner_;
public:
    explicit WeakSlot(const std::shared_ptr<SharedControl>& owner) : owner_(owner) {}
    bool expired() const { return owner_.expired(); }
    SharedSlot lock() const { return SharedSlot(owner_.lock()); }
};
inline WeakSlot SharedSlot::weak() const { return WeakSlot(owner_); }
inline void UniqueSlot::promote_to(SharedSlot& destination) {
    (void)require(resource_);
    if (destination.has_value()) throw std::runtime_error("destination resource slot is not empty");
    // Reserve the shared control block before consuming anything. A failed
    // allocation leaves both existing facades and the source resource intact.
    auto control = std::make_shared<SharedControl>();
    control->resource = std::move(resource_);
    destination.owner_.swap(control);
}
} // namespace feature_library::ownership
