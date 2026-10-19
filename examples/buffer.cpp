#include "demo.hpp"
#include "buffer_library.hpp"
#include <limits>

namespace demo {
ByteBuffer::ByteBuffer(std::string bytes) : native_(std::make_shared<feature_library::ByteBufferStorage>(bytes)) {}
ByteBuffer::ByteBuffer(std::shared_ptr<feature_library::ByteBufferStorage> storage) : native_(std::move(storage)) {}
int ByteBuffer::size() const { return native_->size(); }
std::string ByteBuffer::read() const { return native_->read(); }
void ByteBuffer::write(int offset, std::string bytes) { native_->write(offset, bytes); }
void ByteBuffer::append(std::string bytes) { native_->append(bytes); }
void ByteBuffer::resize(int size, unsigned char fill) { native_->resize(size, fill); }
ByteSlice ByteBuffer::slice(int offset, int length, bool readonly) const {
    return ByteSlice(std::make_shared<feature_library::ByteSlice>(native_,
        feature_library::byte_index(offset), feature_library::byte_index(length), readonly));
}
ByteBuffer ByteBuffer::share() const { return ByteBuffer(native_); }
int ByteBuffer::live() { return feature_library::ByteBufferStorage::alive; }
ByteSlice::ByteSlice(std::shared_ptr<feature_library::ByteSlice> native) : native_(std::move(native)) {
    if (!native_) throw std::invalid_argument("buffer slice requires a native view");
}
int ByteSlice::size() const { return native_->size(); }
bool ByteSlice::readonly() const { return native_->readonly(); }
std::string ByteSlice::read() const { return native_->read(); }
void ByteSlice::write(int offset, std::string bytes) { native_->write(offset, bytes); }
void ByteSlice::xor_bytes(unsigned char mask) { native_->xor_bytes(mask); }
int ByteSlice::live() { return feature_library::ByteSlice::alive; }
std::optional<unsigned int> decode_u16(std::string bytes) {
    unsigned int value = 0;
    std::size_t consumed = 0;
    if (!feature_library::decode_u16(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), &value, consumed))
        return std::nullopt;
    return value;
}
std::vector<unsigned int> decode_u16_with_count(std::string bytes) {
    unsigned int value = 0;
    std::size_t consumed = 0;
    if (!feature_library::decode_u16(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), &value, consumed)) return {};
    return {value, static_cast<unsigned int>(consumed)};
}
std::string out_buffer(int size, unsigned char fill) {
    const auto count = feature_library::byte_index(size);
    unsigned char* pointer = nullptr;
    std::size_t length = 0;
    feature_library::create_buffer(&pointer, length, count, fill);
    std::unique_ptr<unsigned char[]> owner(pointer);
    if (!length) return {};
    return {reinterpret_cast<const char*>(owner.get()), length};
}
int inout_increment(int value) { feature_library::increment(value); return value; }
} // namespace demo
