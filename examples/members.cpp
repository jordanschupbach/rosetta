#include "demo.hpp"
#include "member_library.hpp"

namespace demo {
MemberObject::MemberObject(int left, int right, std::string text, bool derived) {
    if (derived) native_ = std::make_shared<feature_library::members::Derived>(left, right, std::move(text));
    else native_ = std::make_shared<feature_library::members::Object>(left, right, std::move(text));
}
MemberObject::MemberObject(std::shared_ptr<feature_library::members::Object> native) : native_(std::move(native)) {}
const MemberObject& MemberObject::readonly() const { return *this; }
MemberObject MemberObject::share() const { return MemberObject(native_); }
int MemberObject::live() { return feature_library::members::Object::alive; }
MemberSelector::MemberSelector(MemberChoice choice) : native_(std::make_shared<feature_library::members::Selector>(static_cast<int>(choice))) {}
MemberSelector::MemberSelector(std::shared_ptr<feature_library::members::Selector> native) : native_(std::move(native)) {}
bool MemberSelector::null() const { return native_->null(); }
MemberKind MemberSelector::kind() const { return static_cast<MemberKind>(native_->kind()); }
int MemberSelector::read_integer(const MemberObject& object) const { return native_->read_integer(*object.native_); }
void MemberSelector::write_integer(MemberObject& object, int value) const { native_->write_integer(*object.native_, value); }
std::string MemberSelector::read_text(const MemberObject& object) const { return native_->read_text(*object.native_); }
void MemberSelector::write_text(MemberObject& object, std::string text) const { native_->write_text(*object.native_, std::move(text)); }
int MemberSelector::invoke_read(const MemberObject& object, int value) const { return native_->invoke_read(*object.native_, value); }
int MemberSelector::invoke_write(MemberObject& object, int value) const { return native_->invoke_write(*object.native_, value); }
MemberSelector MemberSelector::copy() const { return MemberSelector(std::make_shared<feature_library::members::Selector>(*native_)); }
int MemberSelector::live() { return feature_library::members::Selector::alive; }
} // namespace demo
