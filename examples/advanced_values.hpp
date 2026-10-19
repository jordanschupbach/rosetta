#pragma once

#include <any>
#include <cstdint>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace feature_library {

// Storage layout and nontrivial union-member lifetimes stay inside C++.
#pragma pack(push, 1)
struct PackedFields { std::uint8_t format; std::uint32_t counter; };
#pragma pack(pop)
static_assert(sizeof(PackedFields) == 5, "packed record demo requires byte packing support");

class TaggedRecord {
    union Payload {
        int integer;
        std::string text;
        Payload() : integer(0) {}
        ~Payload() noexcept {}
    } payload_;
    struct Bits { unsigned priority : 3; unsigned mask : 5; } bits_{0, 0};
    PackedFields packed_{1, 0};
    bool text_ = false;
public:
    inline static int alive = 0;
    TaggedRecord() { ++alive; }
    TaggedRecord(const TaggedRecord&) = delete;
    TaggedRecord& operator=(const TaggedRecord&) = delete;
    ~TaggedRecord() noexcept {
        if (text_) { using Text = std::string; payload_.text.~Text(); }
        --alive;
    }
    bool is_text() const { return text_; }
    int integer() const {
        if (text_) throw std::logic_error("tagged record does not contain an integer");
        return payload_.integer;
    }
    std::string text() const {
        if (!text_) throw std::logic_error("tagged record does not contain text");
        return payload_.text;
    }
    void set_integer(int value) {
        if (text_) { using Text = std::string; payload_.text.~Text(); }
        ::new (static_cast<void*>(&payload_.integer)) int(value);
        text_ = false;
    }
    void set_text(std::string value) {
        if (text_) payload_.text.swap(value);
        else ::new (static_cast<void*>(&payload_.text)) std::string(std::move(value));
        text_ = true;
    }
    int priority() const { return static_cast<int>(bits_.priority); }
    int mask() const { return static_cast<int>(bits_.mask); }
    void set_priority(int value) {
        if (value < 0 || value > 7) throw std::out_of_range("priority must be between 0 and 7");
        bits_.priority = static_cast<unsigned>(value);
    }
    void set_mask(int value) {
        if (value < 0 || value > 31) throw std::out_of_range("mask must be between 0 and 31");
        bits_.mask = static_cast<unsigned>(value);
    }
    unsigned int counter() const { return packed_.counter; }
    void set_counter(unsigned int value) { packed_.counter = value; }
};

class ValueStore {
    using Entry = std::variant<int, std::string>;
    using Pair = std::tuple<int, std::string>;
    std::map<std::string, Entry> entries_;
    std::any scratch_;
    const Entry& entry(const std::string& key) const {
        const auto found = entries_.find(key);
        if (found == entries_.end()) throw std::out_of_range("value store key is missing");
        return found->second;
    }
public:
    inline static int alive = 0;
    ValueStore() { ++alive; }
    ValueStore(const ValueStore&) = delete;
    ValueStore& operator=(const ValueStore&) = delete;
    ~ValueStore() noexcept { --alive; }
    void put_integer(std::string key, int value) { entries_.insert_or_assign(std::move(key), Entry(value)); }
    void put_text(std::string key, std::string value) {
        entries_.insert_or_assign(std::move(key), Entry(std::in_place_type<std::string>, std::move(value)));
    }
    int kind(const std::string& key) const {
        const auto found = entries_.find(key);
        if (found == entries_.end()) return 0;
        return std::holds_alternative<int>(found->second) ? 1 : 2;
    }
    int integer(const std::string& key) const { return std::get<int>(entry(key)); }
    std::string text(const std::string& key) const { return std::get<std::string>(entry(key)); }
    bool erase(const std::string& key) { return entries_.erase(key) != 0; }
    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const auto& item : entries_) result.push_back(item.first);
        return result;
    }
    // Prepare replacements before swap, preserving the old value on failure.
    void scratch_integer(int value) { std::any replacement(value); scratch_.swap(replacement); }
    void scratch_text(std::string value) { std::any replacement(std::move(value)); scratch_.swap(replacement); }
    void scratch_pair(int value, std::string text) {
        std::any replacement(std::in_place_type<Pair>, value, std::move(text));
        scratch_.swap(replacement);
    }
    int scratch_kind() const {
        if (!scratch_.has_value()) return 0;
        if (scratch_.type() == typeid(int)) return 1;
        if (scratch_.type() == typeid(std::string)) return 2;
        return 3;
    }
    int scratch_as_integer() const { return std::any_cast<int>(scratch_); }
    std::string scratch_as_text() const { return std::any_cast<const std::string&>(scratch_); }
    int pair_integer() const { return std::get<0>(std::any_cast<const Pair&>(scratch_)); }
    std::string pair_text() const { return std::get<1>(std::any_cast<const Pair&>(scratch_)); }
    void clear_scratch() { scratch_.reset(); }
};
} // namespace feature_library
