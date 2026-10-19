#pragma once

#include <functional>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <optional>
#include <limits>
#include <algorithm>
#include <utility>

// Ordinary C++ library code. Binding facades live in demo.cpp, so future
// backends can bind the same concrete entry points without a Lua dependency.
namespace feature_library {

template<class T> T maximum(T left, T right) { return left < right ? right : left; }

template<class T> class Box {
public:
    explicit Box(T value) : value_(value) {}
    T value() const { return value_; }
private:
    T value_;
};

template<class... T> auto sum(T... values) { return (values + ...); }

// A real multiple/virtual-inheritance hierarchy. Lua sees only the ordinary
// C++ facades in demo.hpp, so none of these layouts cross the language boundary.
struct Entity {
    inline static int alive = 0;
    Entity() { ++alive; }
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    virtual ~Entity() noexcept { --alive; }
    virtual std::string kind() const = 0;
};
struct Named : virtual Entity { virtual std::string name() const = 0; };
struct Valued : virtual Entity {
    virtual int value() const = 0;
    virtual void set_value(int value) = 0;
};
class Diamond final : public Named, public Valued {
    std::string name_;
    int value_;
public:
    Diamond(std::string name, int value) : name_(std::move(name)), value_(value) {}
    std::string kind() const override { return "diamond"; }
    std::string name() const override { return name_; }
    int value() const override { return value_; }
    void set_value(int value) override { value_ = value; }
};
class Label final : public Named {
    std::string name_;
public:
    explicit Label(std::string name) : name_(std::move(name)) {}
    std::string kind() const override { return "named"; }
    std::string name() const override { return name_; }
};
// Explicit reverse-virtual adapter. Other language backends supply the same
// std::function interface; the native hierarchy never depends on Lua.
class Scripted final : public Named, public Valued {
    std::string name_;
    int seed_;
    std::function<int(int)> override_;
public:
    Scripted(std::string name, int seed, std::function<int(int)> callback)
        : name_(std::move(name)), seed_(seed), override_(std::move(callback)) {
        if (!override_) throw std::invalid_argument("virtual override is required");
    }
    std::string kind() const override { return "scripted"; }
    std::string name() const override { return name_; }
    int value() const override { auto callback = override_; return callback(seed_); }
    void set_value(int value) override { seed_ = value; }
};

inline int checked_int(long long value) {
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::overflow_error("series arithmetic overflow");
    return static_cast<int>(value);
}

class SeriesCursor;
class Series {
    friend class SeriesCursor;
    std::vector<int> values_;
    unsigned long long version_ = 0;
public:
    inline static int alive = 0;
    explicit Series(std::vector<int> values) : values_(std::move(values)) {
        if (values_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("series is too long");
        ++alive;
    }
    Series(const Series&) = delete;
    Series& operator=(const Series&) = delete;
    ~Series() noexcept { --alive; }
    int size() const { return static_cast<int>(values_.size()); }
    const std::vector<int>& values() const { return values_; }
    int operator[](int index) const {
        if (index < 0 || index >= size()) throw std::out_of_range("series index out of range");
        return values_[static_cast<std::size_t>(index)];
    }
    void set(int index, int value) {
        (void)(*this)[index]; // validate before changing state
        if (version_ == std::numeric_limits<unsigned long long>::max())
            throw std::overflow_error("series mutation version exhausted");
        values_[static_cast<std::size_t>(index)] = value;
        ++version_;
    }
    std::vector<int> operator+(const Series& other) const {
        if (size() != other.size()) throw std::invalid_argument("series lengths differ");
        std::vector<int> result;
        result.reserve(values_.size());
        for (int i = 0; i < size(); ++i) result.push_back(checked_int(static_cast<long long>((*this)[i]) + other[i]));
        return result;
    }
    std::vector<int> operator-(const Series& other) const {
        if (size() != other.size()) throw std::invalid_argument("series lengths differ");
        std::vector<int> result;
        result.reserve(values_.size());
        for (int i = 0; i < size(); ++i) result.push_back(checked_int(static_cast<long long>((*this)[i]) - other[i]));
        return result;
    }
    std::vector<int> operator-() const { return (*this) * -1; }
    std::vector<int> operator*(int factor) const {
        std::vector<int> result;
        result.reserve(values_.size());
        for (int value : values_) result.push_back(checked_int(static_cast<long long>(value) * factor));
        return result;
    }
    enum class Division { quotient, remainder, floor_quotient, floor_modulo };
    std::vector<int> divided(int divisor, Division mode) const {
        if (divisor == 0) throw std::domain_error("series division by zero");
        std::vector<int> result;
        result.reserve(values_.size());
        for (int value : values_) {
            // Both / and % have undefined behavior for this pair in C++.
            if (value == std::numeric_limits<int>::min() && divisor == -1)
                throw std::overflow_error("series division overflow");
            const int quotient = value / divisor;
            const int remainder = value % divisor;
            const bool adjust = remainder != 0 && ((remainder < 0) != (divisor < 0));
            switch (mode) {
            case Division::quotient: result.push_back(quotient); break;
            case Division::remainder: result.push_back(remainder); break;
            case Division::floor_quotient: result.push_back(quotient - (adjust ? 1 : 0)); break;
            case Division::floor_modulo: result.push_back(adjust ? remainder + divisor : remainder); break;
            }
        }
        return result;
    }
    std::vector<int> operator/(int divisor) const { return divided(divisor, Division::quotient); }
    std::vector<int> operator%(int divisor) const { return divided(divisor, Division::remainder); }
    std::vector<int> floor_quotient(int divisor) const { return divided(divisor, Division::floor_quotient); }
    std::vector<int> floor_modulo(int divisor) const { return divided(divisor, Division::floor_modulo); }
    template<class Operation> std::vector<int> combine(const Series& other, Operation operation) const {
        if (size() != other.size()) throw std::invalid_argument("series lengths differ");
        std::vector<int> result;
        result.reserve(values_.size());
        for (int i = 0; i < size(); ++i) result.push_back(operation((*this)[i], other[i]));
        return result;
    }
    std::vector<int> operator&(const Series& other) const { return combine(other, [](int a, int b) { return a & b; }); }
    std::vector<int> operator|(const Series& other) const { return combine(other, [](int a, int b) { return a | b; }); }
    std::vector<int> operator^(const Series& other) const { return combine(other, [](int a, int b) { return a ^ b; }); }
    std::vector<int> operator~() const {
        std::vector<int> result;
        result.reserve(values_.size());
        for (int value : values_) result.push_back(~value);
        return result;
    }
    std::vector<int> shifted(int count, bool left) const {
        if (count < 0 || count >= std::numeric_limits<int>::digits)
            throw std::out_of_range("series shift count out of range");
        std::vector<int> result;
        result.reserve(values_.size());
        for (int value : values_) {
            if (value < 0) throw std::invalid_argument("series shifts require nonnegative values");
            // Multiplication checks overflow without risking signed left-shift UB.
            result.push_back(left ? checked_int(static_cast<long long>(value) * (1LL << count)) : value >> count);
        }
        return result;
    }
    std::vector<int> operator<<(int count) const { return shifted(count, true); }
    std::vector<int> operator>>(int count) const { return shifted(count, false); }
    bool operator==(const Series& other) const { return values_ == other.values_; }
    bool operator<(const Series& other) const { return values_ < other.values_; }
    bool operator<=(const Series& other) const { return !(other < *this); }
    std::vector<int> operator()(int factor) const { return (*this) * factor; }
    explicit operator bool() const { return !values_.empty(); }
    explicit operator std::string() const {
        std::string text = "[";
        for (std::size_t i = 0; i < values_.size(); ++i) {
            if (i) text += ", ";
            text += std::to_string(values_[i]);
        }
        return text + "]";
    }
};

class SeriesCursor {
    std::shared_ptr<Series> owner_;
    unsigned long long version_;
    std::vector<int>::const_iterator current_;
    std::vector<int>::const_iterator end_;
public:
    inline static int alive = 0;
    explicit SeriesCursor(std::shared_ptr<Series> owner)
        : owner_(std::move(owner)), version_(owner_->version_),
          current_(owner_->values_.cbegin()), end_(owner_->values_.cend()) { ++alive; }
    SeriesCursor(const SeriesCursor&) = delete;
    SeriesCursor& operator=(const SeriesCursor&) = delete;
    ~SeriesCursor() noexcept { --alive; }
    std::optional<int> next() {
        // Check before touching iterators: future structural mutations must also
        // update the version so invalidated iterators are never dereferenced.
        if (version_ != owner_->version_) throw std::runtime_error("series mutated during iteration");
        if (current_ == end_) return std::nullopt;
        return *current_++;
    }
};

} // namespace feature_library
