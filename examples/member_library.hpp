#pragma once
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace feature_library::members {
inline int checked_add(int left, int right) {
    if ((right > 0 && left > std::numeric_limits<int>::max() - right) ||
        (right < 0 && left < std::numeric_limits<int>::min() - right))
        throw std::overflow_error("member arithmetic overflow");
    return left + right;
}
struct Left { int left; };
struct Right { int right; };
class Object : public Left, public Right {
public:
    inline static int alive = 0;
    std::string label;
    Object(int left, int right, std::string text) : Left{left}, Right{right}, label(std::move(text)) { ++alive; }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    virtual ~Object() noexcept { --alive; }
    virtual int measure(int offset) const & { return checked_add(checked_add(left, right), offset); }
    int add(int delta) & { const int next = checked_add(left, delta); left = next; return next; }
};
class Derived final : public Object {
public:
    using Object::Object;
    int measure(int offset) const & override { return checked_add(Object::measure(offset), 1); }
};

class Selector {
    using Integer = int Object::*;
    using Text = std::string Object::*;
    using Read = int (Object::*)(int) const &;
    using Write = int (Object::*)(int) &;
    std::variant<Integer, Text, Read, Write> member_{Integer{nullptr}};
    void require_value() const { if (null()) throw std::invalid_argument("null member selector"); }
public:
    inline static int alive = 0;
    explicit Selector(int choice) {
        switch (choice) {
        case 0: break;
        case 1: member_ = static_cast<Integer>(&Left::left); break;
        case 2: member_ = static_cast<Integer>(&Right::right); break;
        case 3: member_ = &Object::label; break;
        case 4: member_ = static_cast<Read>(&Object::measure); break;
        case 5: member_ = static_cast<Write>(&Object::add); break;
        case 6: member_ = Read{nullptr}; break;
        default: throw std::invalid_argument("unknown member selection");
        }
        ++alive;
    }
    Selector(const Selector& other) : member_(other.member_) { ++alive; }
    Selector& operator=(const Selector&) = delete;
    ~Selector() noexcept { --alive; }
    bool null() const { return std::visit([](auto member) { return member == nullptr; }, member_); }
    int kind() const { return null() ? 0 : static_cast<int>(member_.index()) + 1; }
    int read_integer(const Object& object) const {
        require_value();
        const auto* member = std::get_if<Integer>(&member_);
        if (!member) throw std::invalid_argument("integer member selector expected");
        return std::invoke(*member, object);
    }
    void write_integer(Object& object, int value) const {
        require_value();
        const auto* member = std::get_if<Integer>(&member_);
        if (!member) throw std::invalid_argument("integer member selector expected");
        std::invoke(*member, object) = value;
    }
    std::string read_text(const Object& object) const {
        require_value();
        const auto* member = std::get_if<Text>(&member_);
        if (!member) throw std::invalid_argument("text member selector expected");
        return std::invoke(*member, object);
    }
    void write_text(Object& object, std::string value) const {
        require_value();
        const auto* member = std::get_if<Text>(&member_);
        if (!member) throw std::invalid_argument("text member selector expected");
        std::invoke(*member, object).swap(value);
    }
    int invoke_read(const Object& object, int value) const {
        require_value();
        const auto* member = std::get_if<Read>(&member_);
        if (!member) throw std::invalid_argument("const method selector expected");
        return std::invoke(*member, object, value);
    }
    int invoke_write(Object& object, int value) const {
        require_value();
        const auto* member = std::get_if<Write>(&member_);
        if (!member) throw std::invalid_argument("mutable method selector expected");
        return std::invoke(*member, object, value);
    }
};
} // namespace feature_library::members
