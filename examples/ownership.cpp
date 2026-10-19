#include "demo.hpp"
#include "ownership_library.hpp"

namespace demo {
namespace ownership = feature_library::ownership;
ResourceSlot::ResourceSlot() : native_(std::make_unique<ownership::UniqueSlot>()) {}
ResourceSlot::ResourceSlot(std::string text, int tag, bool fail)
    : native_(std::make_unique<ownership::UniqueSlot>(std::move(text), tag, fail)) {}
ResourceSlot::~ResourceSlot() noexcept = default;
bool ResourceSlot::has_value() const { return native_->has_value(); }
std::string ResourceSlot::text() const { return native_->text(); }
bool ResourceSlot::aligned() const { return native_->aligned(); }
int ResourceSlot::deleter_tag() const { return native_->tag(); }
void ResourceSlot::reset() { native_->reset(); }
void ResourceSlot::move_to(ResourceSlot& destination) { native_->move_to(*destination.native_); }
void ResourceSlot::promote_to(SharedResource& destination) { native_->promote_to(*destination.native_); }
unsigned long long ResourceSlot::live() { return ownership::Resource::alive; }
unsigned long long ResourceSlot::allocations() { return ownership::Resource::allocations; }
unsigned long long ResourceSlot::releases() { return ownership::Resource::releases; }
unsigned long long ResourceSlot::deleted() { return ownership::Resource::deleted; }
int ResourceSlot::last_deleter_tag() { return ownership::Resource::last_tag; }
int ResourceSlot::alignment() { return alignof(ownership::Resource); }
SharedResource::SharedResource() : native_(std::make_unique<ownership::SharedSlot>()) {}
SharedResource::SharedResource(ownership::SharedSlot native) : native_(std::make_unique<ownership::SharedSlot>(std::move(native))) {}
SharedResource::~SharedResource() noexcept = default;
bool SharedResource::has_value() const { return native_->has_value(); }
std::string SharedResource::text() const { return native_->text(); }
int SharedResource::deleter_tag() const { return native_->tag(); }
void SharedResource::reset() { native_->reset(); }
std::unique_ptr<SharedResource> SharedResource::clone() const { return std::unique_ptr<SharedResource>(new SharedResource(native_->clone())); }
std::unique_ptr<WeakResource> SharedResource::weak() const { return std::make_unique<WeakResource>(native_->weak()); }
WeakResource::WeakResource(ownership::WeakSlot native) : native_(std::make_unique<ownership::WeakSlot>(std::move(native))) {}
WeakResource::~WeakResource() noexcept = default;
bool WeakResource::expired() const { return native_->expired(); }
std::unique_ptr<SharedResource> WeakResource::lock() const {
    auto locked = native_->lock();
    if (!locked.has_value()) return {};
    return std::unique_ptr<SharedResource>(new SharedResource(std::move(locked)));
}
std::unique_ptr<ResourceSlot> empty_resource() { return std::make_unique<ResourceSlot>(); }
std::unique_ptr<ResourceSlot> make_resource(std::string text, int tag, bool fail) { return std::make_unique<ResourceSlot>(std::move(text), tag, fail); }
} // namespace demo
