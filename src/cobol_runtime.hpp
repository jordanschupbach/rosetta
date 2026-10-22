#pragma once

namespace rosetta::detail {
// The C++ side of the COBOL bindings. COBOL passes each call's parameter
// record and the status record by reference; fields are read and written
// with memcpy at the offsets the copybook defines (COBOL groups have no
// padding), and every stub returns the status code, which COBOL stores in
// RETURN-CODE.
inline constexpr const char* cobol_runtime = R"cpp(
namespace { namespace rosetta_cobol {
enum kind : std::int32_t { allocation = 1, range, index, argument, native, closed };
struct error { kind code; std::string message; };
[[noreturn]] inline void fail(kind code, std::string message) { throw error{code, std::move(message)}; }
inline std::mutex& mutex() { static auto* value = new std::mutex; return *value; }

template<class T> T load(const unsigned char* field) {
    T value;
    std::memcpy(&value, field, sizeof value);
    return value;
}
template<class T> void store(unsigned char* field, T value) { std::memcpy(field, &value, sizeof value); }

// The status record: code BINARY-LONG, kind PIC X(10), length BINARY-LONG,
// message PIC X(@MESSAGE@). OMITTED (null) skips it.
inline constexpr std::size_t message_size = @MESSAGE@;
inline void report(unsigned char* status, std::int32_t code, const char* kind, const std::string& message) noexcept {
    if (!status) return;
    store(status, code);
    std::memset(status + 4, ' ', 10);
    std::memcpy(status + 4, kind, std::strlen(kind));
    // Truncation never splits a UTF-8 sequence.
    std::size_t size = std::min(message.size(), message_size);
    if (size < message.size())
        while (size > 0 && (static_cast<unsigned char>(message[size]) & 0xC0) == 0x80) --size;
    store(status + 14, static_cast<std::int32_t>(size));
    std::memset(status + 18, ' ', message_size);
    std::memcpy(status + 18, message.data(), size);
}
inline std::int32_t record(unsigned char* status, kind code, const char* label, const char* message) noexcept {
    static const char* const names[] = {"", "allocation", "range", "index", "argument", "native", "closed"};
    try { report(status, code, names[code], std::string(label) + ": " + message); }
    catch (...) { report(status, code, names[code], std::string()); }
    return code;
}
template<class F> std::int32_t guard(unsigned char* status, const char* label, F fn) noexcept {
    try {
        std::lock_guard<std::mutex> hold(mutex());
        fn();
        report(status, 0, "", std::string());
        return 0;
    }
    catch (const error& e) { return record(status, e.code, label, e.message.c_str()); }
    catch (const std::bad_alloc&) { return record(status, allocation, label, "native allocation failed"); }
    catch (const std::overflow_error& e) { return record(status, range, label, e.what()); }
    catch (const std::out_of_range& e) { return record(status, index, label, e.what()); }
    catch (const std::invalid_argument& e) { return record(status, argument, label, e.what()); }
    catch (const std::exception& e) { return record(status, native, label, e.what()); }
    catch (...) { return record(status, native, label, "unknown C++ exception"); }
}

// Objects are numeric handles into this registry, checked on every call and
// never reused. Objects still open at exit are destroyed in creation order.
struct Object { const void* type; void* native; void (*destroy)(void*); };
struct Registry { std::map<std::uint64_t, Object> objects; std::uint64_t next = 1; };
inline Registry& registry() { static auto* value = new Registry; return *value; }
template<class T> const void* tag() { static const char value = 0; return &value; }
template<class T> void destroy(void* native) { delete static_cast<T*>(native); }
inline void destroy_all() noexcept {
    std::lock_guard<std::mutex> hold(mutex());
    auto objects = std::move(registry().objects);
    registry().objects.clear();
    for (auto& entry : objects) entry.second.destroy(entry.second.native);
}
template<class T> std::uint64_t adopt(std::unique_ptr<T> native) {
    static const bool registered = (std::atexit([] { destroy_all(); }), true);
    (void)registered;
    auto& objects = registry();
    const auto id = objects.next++;
    objects.objects.emplace(id, Object{tag<T>(), native.get(), &destroy<T>});
    native.release();
    return id;
}
template<class T> T& live(std::uint64_t id, const char* type) {
    const auto found = registry().objects.find(id);
    if (found == registry().objects.end()) fail(closed, "native object is closed");
    if (found->second.type != tag<T>()) fail(argument, std::string("object is not a ") + type);
    return *static_cast<T*>(found->second.native);
}
// Closing twice is harmless.
template<class T> void close(std::uint64_t id, const char* type) {
    const auto found = registry().objects.find(id);
    if (found == registry().objects.end()) return;
    if (found->second.type != tag<T>()) fail(argument, std::string("object is not a ") + type);
    const auto object = found->second;
    registry().objects.erase(found);
    object.destroy(object.native);
}

template<class To, class From> bool fits(From number) {
    if constexpr (std::is_signed_v<From> == std::is_signed_v<To>)
        return number >= std::numeric_limits<To>::lowest() && number <= std::numeric_limits<To>::max();
    else if constexpr (std::is_signed_v<From>)
        return number >= 0 && static_cast<std::make_unsigned_t<From>>(number) <= std::numeric_limits<To>::max();
    else return number <= static_cast<std::make_unsigned_t<To>>(std::numeric_limits<To>::max());
}
// A field holds its C++ type's fixed-width equivalent; only types narrower
// than their field (a 32-bit long) can fail this check.
template<class To, class From> To integer(From number) {
    if (!fits<To>(number)) fail(range, "integer outside C++ type range");
    return static_cast<To>(number);
}
[[maybe_unused]] inline bool boolean(std::uint8_t number) {
    if (number > 1) fail(argument, "expected a boolean (0 or 1)");
    return number == 1;
}
// A PIC X(n) field and its BINARY-LONG length: -1 (the copybook's VALUE)
// trims trailing spaces, 0 to n takes exactly that many bytes.
[[maybe_unused]] inline std::string text(const unsigned char* field, std::size_t capacity, const unsigned char* length) {
    const auto given = load<std::int32_t>(length);
    std::size_t size = 0;
    if (given == -1) {
        size = capacity;
        while (size > 0 && field[size - 1] == ' ') --size;
    } else if (given >= 0 && static_cast<std::size_t>(given) <= capacity) size = static_cast<std::size_t>(given);
    else fail(argument, "string length outside its " + std::to_string(capacity) + "-byte field");
    return std::string(reinterpret_cast<const char*>(field), size);
}
// Fills the field, padded with spaces, and sets the length to the bytes
// used. A longer result fails with the length it would have needed.
[[maybe_unused]] inline void result_text(unsigned char* field, std::size_t capacity, unsigned char* length, const std::string& value) {
    if (value.size() > capacity || value.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        if (value.size() <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) store(length, static_cast<std::int32_t>(value.size()));
        fail(range, "string result is longer than its " + std::to_string(capacity) + "-byte field");
    }
    std::memset(field, ' ', capacity);
    if (!value.empty()) std::memcpy(field, value.data(), value.size());
    store(length, static_cast<std::int32_t>(value.size()));
}
// The number of given arguments, for calls with C++ defaults.
[[maybe_unused]] inline std::size_t count(const unsigned char* field, std::size_t required, std::size_t total) {
    const auto given = load<std::int32_t>(field);
    if (given < static_cast<std::int32_t>(required) || given > static_cast<std::int32_t>(total))
        fail(argument, "argument count must be " + (required == total ? std::to_string(required)
            : "between " + std::to_string(required) + " and " + std::to_string(total)));
    return static_cast<std::size_t>(given);
}
} } // anonymous namespace, rosetta_cobol
)cpp";
} // namespace rosetta::detail
