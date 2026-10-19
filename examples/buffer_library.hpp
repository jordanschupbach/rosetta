#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace feature_library {
inline void raw_xor(void* memory, std::size_t length, unsigned char mask) {
    auto* bytes = static_cast<unsigned char*>(memory);
    for (std::size_t i = 0; i < length; ++i) bytes[i] ^= mask;
}
inline bool decode_u16(const unsigned char* bytes, std::size_t size, unsigned int* value, std::size_t& consumed) {
    consumed = 0;
    if (size < 2) return false;
    *value = static_cast<unsigned int>(bytes[0]) | (static_cast<unsigned int>(bytes[1]) << 8);
    consumed = 2;
    return true;
}
inline void create_buffer(unsigned char** output, std::size_t& length, std::size_t size, unsigned char fill) {
    auto owner = size ? std::make_unique<unsigned char[]>(size) : nullptr;
    if (size) std::fill_n(owner.get(), size, fill);
    *output = owner.release();
    length = size;
}
inline void increment(int& value) {
    if (value == std::numeric_limits<int>::max()) throw std::overflow_error("in-out increment overflow");
    ++value;
}

inline std::size_t byte_index(int value) {
    if (value < 0) throw std::out_of_range("buffer offset/length must be nonnegative");
    return static_cast<std::size_t>(value);
}
inline void byte_range(std::size_t size, std::size_t offset, std::size_t length) {
    if (offset > size || length > size - offset) throw std::out_of_range("buffer range is out of bounds");
}
inline std::string copy_bytes(std::span<const unsigned char> bytes) {
    if (bytes.empty()) return {};
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

class ByteBufferStorage {
    friend class ByteSlice;
    std::vector<unsigned char> bytes;
    unsigned long long generation = 0;
    void may_invalidate() const {
        if (generation == std::numeric_limits<unsigned long long>::max())
            throw std::overflow_error("buffer generation exhausted");
    }
public:
    inline static int alive = 0;
    explicit ByteBufferStorage(const std::string& value) {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("buffer is too large");
        bytes.assign(value.begin(), value.end());
        ++alive;
    }
    ~ByteBufferStorage() noexcept { --alive; }
    ByteBufferStorage(const ByteBufferStorage&) = delete;
    ByteBufferStorage& operator=(const ByteBufferStorage&) = delete;
    int size() const { return static_cast<int>(bytes.size()); }
    std::string read() const { return copy_bytes(bytes); }
    void write(int offset, const std::string& value) {
        const auto start = byte_index(offset);
        byte_range(bytes.size(), start, value.size());
        if (!value.empty()) std::copy(value.begin(), value.end(), bytes.begin() + start);
    }
    void append(const std::string& value) {
        if (value.empty()) return;
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) - bytes.size())
            throw std::length_error("buffer is too large");
        may_invalidate();
        bytes.insert(bytes.end(), value.begin(), value.end());
        ++generation;
    }
    void resize(int size, unsigned char fill) {
        const auto length = byte_index(size);
        if (length == bytes.size()) return;
        may_invalidate();
        bytes.resize(length, fill);
        ++generation;
    }
};

class ByteSlice {
    std::weak_ptr<ByteBufferStorage> owner_;
    unsigned long long generation_;
    std::size_t offset_, length_;
    bool readonly_;
    std::shared_ptr<ByteBufferStorage> lock(bool writing = false) const {
        auto owner = owner_.lock();
        if (!owner) throw std::runtime_error("buffer owner expired");
        if (owner->generation != generation_) throw std::runtime_error("buffer view invalidated by structural mutation");
        if (writing && readonly_) throw std::runtime_error("buffer view is readonly");
        return owner;
    }
public:
    inline static int alive = 0;
    ByteSlice(const std::shared_ptr<ByteBufferStorage>& owner, std::size_t offset, std::size_t length, bool readonly)
        : owner_(owner), generation_(0), offset_(offset), length_(length), readonly_(readonly) {
        if (!owner) throw std::invalid_argument("buffer slice requires an owner");
        byte_range(owner->bytes.size(), offset, length);
        generation_ = owner->generation;
        ++alive;
    }
    ByteSlice(const ByteSlice&) = delete;
    ByteSlice& operator=(const ByteSlice&) = delete;
    ~ByteSlice() noexcept { --alive; }
    int size() const { (void)lock(); return static_cast<int>(length_); }
    bool readonly() const { (void)lock(); return readonly_; }
    std::string read() const {
        auto owner = lock();
        if (!length_) return {};
        return copy_bytes(std::span<const unsigned char>(owner->bytes).subspan(offset_, length_));
    }
    void write(int offset, const std::string& value) {
        auto owner = lock(true);
        const auto start = byte_index(offset);
        byte_range(length_, start, value.size());
        if (!value.empty()) std::copy(value.begin(), value.end(), owner->bytes.begin() + offset_ + start);
    }
    void xor_bytes(unsigned char mask) {
        auto owner = lock(true);
        if (length_) raw_xor(owner->bytes.data() + offset_, length_, mask);
    }
};
} // namespace feature_library
