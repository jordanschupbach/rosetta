#pragma once

// C++ runtime of the full COBOL backend (--cobol-api full): emitted (as text) into every generated
// source, inside the module's namespace ROSETTA_COBOL_MODULE, so split sources of one module share
// one handle registry and two modules never accept each other's handles.
//
// COBOL passes each call's parameter record and the status record by reference. Stubs read and
// write fields with memcpy at the offsets the copybook lays out (COBOL groups have no padding) and
// return the status code, which COBOL stores in RETURN-CODE. Objects and containers are numeric
// handles into a registry; callbacks and overrides call COBOL programs through PROGRAM-POINTERs.
namespace rosetta::detail {
inline constexpr const char* cobol_full_runtime = R"cpp(
namespace ROSETTA_COBOL_MODULE { namespace rc {
// ---------------------------------------------------------------- errors and the status record
enum Kind : std::int32_t { ok = 0, allocation, range, index, argument, native, closed, callback };
struct Failure { Kind kind; std::string message; };
[[noreturn]] inline void fail(Kind kind, std::string message) { throw Failure{kind, std::move(message)}; }
// A failure reported by a COBOL callback or override program; it unwinds through C++ code.
struct CallbackFailure : std::runtime_error { using std::runtime_error::runtime_error; };

template<class T> T load(const unsigned char* field) { T value; std::memcpy(&value, field, sizeof value); return value; }
template<class T> void store(unsigned char* field, T value) { std::memcpy(field, &value, sizeof value); }

// The status record: CODE BINARY-LONG, KIND PIC X(10), LENGTH BINARY-LONG, MESSAGE PIC X(n).
inline constexpr std::size_t message_size = ROSETTA_COBOL_MESSAGE;
inline constexpr std::size_t status_size = 18 + message_size;
inline void report(unsigned char* status, std::int32_t code, const char* kind, const std::string& message) noexcept {
    if (!status) return;
    store(status, code);
    std::memset(status + 4, ' ', 10);
    std::memcpy(status + 4, kind, std::min<std::size_t>(std::strlen(kind), 10));
    // Truncation never splits a UTF-8 sequence.
    std::size_t size = std::min(message.size(), message_size);
    if (size < message.size())
        while (size > 0 && (static_cast<unsigned char>(message[size]) & 0xC0) == 0x80) --size;
    store(status + 14, static_cast<std::int32_t>(size));
    std::memset(status + 18, ' ', message_size);
    if (size) std::memcpy(status + 18, message.data(), size);
}
inline const char* kind_name(Kind kind) {
    static const char* const names[] = {"", "allocation", "range", "index", "argument", "native", "closed", "callback"};
    return names[kind];
}
// C++ and COBOL share stdout; flushing before control returns keeps output in program order.
inline void flush_output() noexcept { try { std::cout.flush(); } catch (...) {} std::fflush(stdout); }

// ---------------------------------------------------------------- handles
// Each handle type (exported class or container type) has a descriptor; cast converts a pointer
// to the handle's own type into a pointer to TO (a base class), or null.
struct Class { const char* name; void* (*cast)(void* object, const Class* to); };
struct Resolved { void* object; const Class* klass; };
// Deletes an owned object unless its ownership moved to C++ (a unique_ptr parameter).
struct Owned {
    void (*destroy)(void*);
    bool active = true;
    void operator()(void* object) const { if (active && object) destroy(object); }
};
template<class T> void destroy(void* object) { delete static_cast<T*>(object); }
struct Box {
    void* object;               // the object, as its handle type
    const Class* klass;
    std::shared_ptr<void> keep; // owns the object, or keeps its owner alive; empty for unowned references
};
struct Registry {
    std::recursive_mutex mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<Box>> boxes;
    std::uint64_t next = 1;
};
inline Registry& registry() { static auto* value = new Registry; return *value; }
// Boxes used by the calls in progress on this thread stay alive until their call returns, even
// when a callback closes their handles.
inline std::vector<std::shared_ptr<Box>>& pins() { static thread_local std::vector<std::shared_ptr<Box>> value; return value; }

// Objects still open at exit are destroyed, so destructors that flush or close files run.
inline void close_all() noexcept {
    std::unordered_map<std::uint64_t, std::shared_ptr<Box>> boxes;
    {
        std::lock_guard<std::recursive_mutex> hold(registry().mutex);
        boxes.swap(registry().boxes);
    }
    try { boxes.clear(); } catch (...) {}
    flush_output();
}
inline std::uint64_t enter(std::shared_ptr<Box> box) {
    static const bool registered = (std::atexit([] { close_all(); }), true);
    (void)registered;
    std::lock_guard<std::recursive_mutex> hold(registry().mutex);
    const auto id = registry().next++;
    registry().boxes.emplace(id, std::move(box));
    return id;
}
inline std::shared_ptr<Box> find(std::uint64_t id, const std::string& what) {
    if (id == 0) fail(argument, what + " is a null handle (0)");
    std::shared_ptr<Box> box;
    {
        std::lock_guard<std::recursive_mutex> hold(registry().mutex);
        const auto found = registry().boxes.find(id);
        if (found != registry().boxes.end()) box = found->second;
    }
    if (!box) fail(closed, what + " is closed (handle " + std::to_string(id) + ")");
    pins().push_back(box);
    return box;
}
inline void close(std::uint64_t id) {
    std::shared_ptr<Box> box;
    {
        std::lock_guard<std::recursive_mutex> hold(registry().mutex);
        const auto found = registry().boxes.find(id);
        if (found == registry().boxes.end()) return;
        box = std::move(found->second);
        registry().boxes.erase(found);
    }
    box.reset(); // destroys an owned object (outside the lock: destructors may call back)
}
inline bool open(std::uint64_t id) {
    std::lock_guard<std::recursive_mutex> hold(registry().mutex);
    return registry().boxes.count(id) != 0;
}
template<class T> T* cast(const Box& box, const Class& want, const std::string& what) {
    void* object = box.klass->cast(box.object, &want);
    if (!object) fail(argument, what + " is a " + box.klass->name + " handle, not a " + want.name);
    return static_cast<T*>(object);
}
// The object behind a handle parameter.
template<class T> T& object(std::uint64_t id, const Class& want, const std::string& what) {
    return *cast<T>(*find(id, what), want, what);
}
// A pointer parameter: 0 is null.
template<class T> T* pointer(std::uint64_t id, const Class& want, const std::string& what) {
    return id == 0 ? nullptr : &object<T>(id, want, what);
}
// A shared_ptr parameter shares ownership with the handle (it stays valid after the handle closes).
template<class T> std::shared_ptr<T> shared(std::uint64_t id, const Class& want, const std::string& what) {
    if (id == 0) return {};
    const auto box = find(id, what);
    return std::shared_ptr<T>(box->keep, cast<T>(*box, want, what));
}
// A unique_ptr parameter takes the object: the handle closes.
template<class T> std::unique_ptr<T> release(std::uint64_t id, const Class& want, const std::string& what) {
    if (id == 0) return {};
    std::lock_guard<std::recursive_mutex> hold(registry().mutex);
    const auto box = find(id, what);
    T* object = cast<T>(*box, want, what);
    auto* owner = std::get_deleter<Owned>(box->keep);
    if (!owner || !owner->active || box->keep.use_count() != 1)
        fail(argument, what + " cannot pass ownership to C++: its object is shared or borrowed");
    owner->active = false;
    registry().boxes.erase(id);
    return std::unique_ptr<T>(object);
}
// A new handle owning OBJECT.
template<class T> std::uint64_t adopt(std::unique_ptr<T> object, Resolved (*resolve)(T*)) {
    if (!object) return 0;
    const auto resolved = resolve(object.get());
    T* raw = object.release();
    std::shared_ptr<void> keep(raw, Owned{&destroy<T>});
    return enter(std::make_shared<Box>(Box{resolved.object, resolved.klass, std::move(keep)}));
}
template<class T> std::uint64_t share(std::shared_ptr<T> object, Resolved (*resolve)(T*)) {
    if (!object) return 0;
    const auto resolved = resolve(object.get());
    return enter(std::make_shared<Box>(Box{resolved.object, resolved.klass, std::shared_ptr<void>(std::move(object))}));
}
// A handle to an object owned elsewhere; KEEP (the owner's box) keeps it alive.
template<class T> std::uint64_t borrow(const T* object, Resolved (*resolve)(T*), std::shared_ptr<void> keep) {
    if (!object) return 0;
    const auto resolved = resolve(const_cast<T*>(object));
    return enter(std::make_shared<Box>(Box{resolved.object, resolved.klass, std::move(keep)}));
}
// The keep of a box: the owner a borrowed member result keeps alive.
inline std::shared_ptr<void> keep_of(std::uint64_t id) {
    std::lock_guard<std::recursive_mutex> hold(registry().mutex);
    const auto found = registry().boxes.find(id);
    return found == registry().boxes.end() ? std::shared_ptr<void>() : found->second->keep;
}

// Handles a callback receives for its arguments; they close when the callback returns.
struct Temporaries {
    std::vector<std::uint64_t> ids;
    Temporaries() = default;
    Temporaries(const Temporaries&) = delete;
    Temporaries& operator=(const Temporaries&) = delete;
    template<class T> std::uint64_t borrow(const T* object, Resolved (*resolve)(T*)) {
        const auto id = rc::borrow(object, resolve, {});
        if (id) ids.push_back(id);
        return id;
    }
    template<class T> std::uint64_t share(const std::shared_ptr<T>& object, Resolved (*resolve)(T*)) {
        const auto id = rc::share(object, resolve);
        if (id) ids.push_back(id);
        return id;
    }
    ~Temporaries() { for (const auto id : ids) { try { close(id); } catch (...) {} } }
};
// A handle a callback returns: its object is copied (or moved) and the handle closed.
template<class T> T consume(std::uint64_t id, const Class& want, const std::string& what) {
    const auto box = find(id, what);
    T* object = cast<T>(*box, want, what);
    auto* owner = std::get_deleter<Owned>(box->keep);
    if constexpr (std::is_move_constructible_v<T>) {
        if (owner && owner->active && box->keep.use_count() == 1) {
            T value(std::move(*object));
            close(id);
            return value;
        }
    }
    if constexpr (std::is_copy_constructible_v<T>) {
        T value(*object);
        close(id);
        return value;
    } else {
        fail(argument, what + " cannot be returned: its object is shared or borrowed");
    }
}

// ---------------------------------------------------------------- calls
// Runs one call: maps exceptions to the status record and returns the status code.
template<class F> std::int32_t guard(unsigned char* status, const char* label, F&& body) noexcept {
    const auto pinned = pins().size();
    std::int32_t code = 0;
    const auto failed = [&](Kind kind, const std::string& message) noexcept {
        code = kind;
        try { report(status, kind, kind_name(kind), std::string(label) + ": " + message); }
        catch (...) { report(status, kind, kind_name(kind), std::string()); }
    };
    try {
        body();
        report(status, 0, "", std::string());
    }
    catch (const Failure& e) { failed(e.kind, e.message); }
    catch (const CallbackFailure& e) { failed(callback, e.what()); }
    catch (const std::bad_alloc&) { failed(allocation, "native allocation failed"); }
    catch (const std::overflow_error& e) { failed(range, e.what()); }
    catch (const std::range_error& e) { failed(range, e.what()); }
    catch (const std::out_of_range& e) { failed(index, e.what()); }
    catch (const std::invalid_argument& e) { failed(argument, e.what()); }
    catch (const std::domain_error& e) { failed(argument, e.what()); }
    catch (const std::exception& e) { failed(native, e.what()); }
    catch (...) { failed(native, "unknown C++ exception"); }
    try { pins().resize(pinned); } catch (...) {}
    flush_output();
    return code;
}

// ---------------------------------------------------------------- field conversions
template<class To, class From> bool fits(From number) {
    if constexpr (std::is_floating_point_v<To>) return true;
    else if constexpr (std::is_signed_v<From> == std::is_signed_v<To>)
        return number >= std::numeric_limits<To>::lowest() && number <= std::numeric_limits<To>::max();
    else if constexpr (std::is_signed_v<From>)
        return number >= 0 && static_cast<std::make_unsigned_t<From>>(number) <= std::numeric_limits<To>::max();
    else return number <= static_cast<std::make_unsigned_t<To>>(std::numeric_limits<To>::max());
}
template<class To, class From> To integer(From number, const char* what) {
    if (!fits<To>(number)) fail(range, std::string(what) + ": integer outside the C++ type's range");
    return static_cast<To>(number);
}
inline bool boolean(std::uint8_t number, const char* what) {
    if (number > 1) fail(argument, std::string(what) + ": expected a boolean (0 or 1)");
    return number == 1;
}
// A PIC X(n) field and its BINARY-LONG length: -1 trims trailing spaces, 0 to n takes that many bytes.
inline std::string text(const unsigned char* field, std::size_t capacity, const unsigned char* length, const char* what) {
    const auto given = load<std::int32_t>(length);
    std::size_t size = 0;
    if (given == -1) {
        size = capacity;
        while (size > 0 && field[size - 1] == ' ') --size;
    } else if (given >= 0 && static_cast<std::size_t>(given) <= capacity) size = static_cast<std::size_t>(given);
    else fail(argument, std::string(what) + ": string length outside its " + std::to_string(capacity) + "-byte field");
    return std::string(reinterpret_cast<const char*>(field), size);
}
// Fills a string field padded with spaces and sets its length. A longer value fails, giving the
// length it needs.
inline void put_text(unsigned char* field, std::size_t capacity, unsigned char* length, const std::string& value, const char* what) {
    if (value.size() > capacity)
        fail(range, std::string(what) + ": the " + std::to_string(value.size()) + "-byte string does not fit its " +
             std::to_string(capacity) + "-byte field");
    std::memset(field, ' ', capacity);
    if (!value.empty()) std::memcpy(field, value.data(), value.size());
    store(length, static_cast<std::int32_t>(value.size()));
}
inline void put_text(unsigned char* field, std::size_t capacity, unsigned char* length, const char* value, const char* what) {
    put_text(field, capacity, length, std::string(value ? value : ""), what);
}
// The number of arguments sent, for calls with C++ defaults.
inline std::size_t given(const unsigned char* field, std::size_t required, std::size_t total) {
    const auto count = load<std::int32_t>(field);
    if (count < static_cast<std::int32_t>(required) || count > static_cast<std::int32_t>(total))
        fail(argument, "-GIVEN must be " + (required == total ? std::to_string(required)
            : "between " + std::to_string(required) + " and " + std::to_string(total)) + ", not " + std::to_string(count));
    return static_cast<std::size_t>(count);
}
// A 1-based index field into a container of SIZE elements.
inline std::size_t position(std::int64_t index, std::size_t size, const char* what) {
    if (index < 1 || static_cast<std::uint64_t>(index) > size)
        fail(rc::index, std::string(what) + ": index " + std::to_string(index) + " is outside 1.." + std::to_string(size));
    return static_cast<std::size_t>(index - 1);
}
// A table's element count against its OCCURS capacity.
inline std::size_t table_count(std::int32_t count, std::size_t capacity, const char* what) {
    if (count < 0 || static_cast<std::size_t>(count) > capacity)
        fail(argument, std::string(what) + ": -COUNT must be between 0 and " + std::to_string(capacity));
    return static_cast<std::size_t>(count);
}

// Field setters: types without copy assignment refuse at run time.
template<class T, class V> void assign(T& field, V&& value, const char* what) {
    if constexpr (std::is_assignable_v<T&, V&&>) field = std::forward<V>(value);
    else { (void)field; (void)value; fail(argument, std::string(what) + ": this C++ field cannot be assigned"); }
}

// ---------------------------------------------------------------- COBOL programs
using Program = int (*)(unsigned char*, unsigned char*);
inline void* program_of(const unsigned char* field) { return load<void*>(field); }
// COBOL is single-threaded: callbacks from other threads wait their turn.
inline std::recursive_mutex& program_mutex() { static auto* value = new std::recursive_mutex; return *value; }
// Calls a COBOL program USING ARGS and a fresh callback status record. A non-zero status code
// becomes a CallbackFailure carrying the program's message.
inline void invoke(void* program, unsigned char* args, const std::string& what) {
    if (!program) fail(argument, what + ": no COBOL program (null PROGRAM-POINTER)");
    std::vector<unsigned char> status(status_size, ' ');
    store<std::int32_t>(status.data(), 0);
    store<std::int32_t>(status.data() + 14, 0);
    flush_output();
    reinterpret_cast<Program>(program)(args, status.data());
    flush_output();
    const auto code = load<std::int32_t>(status.data());
    if (code == 0) return;
    auto length = load<std::int32_t>(status.data() + 14);
    std::string message;
    if (length > 0 && static_cast<std::size_t>(length) <= message_size)
        message.assign(reinterpret_cast<const char*>(status.data() + 18), static_cast<std::size_t>(length));
    else {
        message.assign(reinterpret_cast<const char*>(status.data() + 18), message_size);
        while (!message.empty() && message.back() == ' ') message.pop_back();
    }
    if (message.empty()) message = "failed with status code " + std::to_string(code);
    throw CallbackFailure(what + ": " + message);
}
// A field record buffer for a callback: numeric fields zero, string fields spaces (set per field).
struct Buffer {
    std::vector<unsigned char> bytes;
    explicit Buffer(std::size_t size) : bytes(size ? size : 1, 0) {}
    unsigned char* data() { return bytes.data(); }
};
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(argument, std::string("pure virtual method ") + name + " has no COBOL override");
}
} } // namespace ROSETTA_COBOL_MODULE::rc
)cpp";
} // namespace rosetta::detail
