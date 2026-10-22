#pragma once

namespace rosetta::detail {
inline constexpr const char* go_runtime = R"cpp(
namespace { namespace rosetta_go {
// Every stub reports failures through a Status the Go caller owns; C++
// exceptions never leave a stub. Messages and string results are malloc'd
// and freed by Go. The codes match the Go ErrorKind constants.
struct Status { std::uint64_t code; char* message; std::size_t size; };
struct Bytes { char* data; std::size_t size; };
enum kind : std::uint64_t { allocation = 1, range, index, argument, native, closed };
struct error { kind code; std::string message; };
[[noreturn]] inline void fail(kind code, std::string message) { throw error{code, std::move(message)}; }
// Native calls, closes and cleanups (which Go runs on its own goroutine) are
// serialized per package. Native workers never call into Go.
inline std::mutex& mutex() { static auto* value = new std::mutex; return *value; }
// Each Go object holds a pointer to a cell, which owns the native value and
// is null once closed.
template<class T> struct Cell { T* native = nullptr; };
[[maybe_unused]] inline Bytes bytes(const std::string& text) {
    auto* data = static_cast<char*>(std::malloc(text.size() ? text.size() : 1));
    if (!data) throw std::bad_alloc();
    if (!text.empty()) std::memcpy(data, text.data(), text.size());
    return {data, text.size()};
}
inline void record(Status* status, kind code, const char* message, std::size_t size) noexcept {
    status->code = code;
    status->message = static_cast<char*>(std::malloc(size ? size : 1));
    if (!status->message) return;
    if (size) std::memcpy(status->message, message, size);
    status->size = size;
}
template<class F> auto guard(Status* status, F fn) noexcept {
    using R = decltype(fn());
    *status = {0, nullptr, 0};
    try {
        std::lock_guard<std::mutex> hold(mutex());
        return fn();
    }
    catch (const error& e) { record(status, e.code, e.message.data(), e.message.size()); }
    catch (const std::bad_alloc&) { record(status, allocation, "native allocation failed", 24); }
    catch (const std::overflow_error& e) { record(status, range, e.what(), std::strlen(e.what())); }
    catch (const std::out_of_range& e) { record(status, index, e.what(), std::strlen(e.what())); }
    catch (const std::invalid_argument& e) { record(status, argument, e.what(), std::strlen(e.what())); }
    catch (const std::exception& e) { record(status, native, e.what(), std::strlen(e.what())); }
    catch (...) { record(status, native, "unknown C++ exception", 21); }
    if constexpr (std::is_void_v<R>) return;
    else return R{};
}
template<class To, class From> bool fits(From number) {
    if constexpr (std::is_signed_v<From> == std::is_signed_v<To>)
        return number >= std::numeric_limits<To>::lowest() && number <= std::numeric_limits<To>::max();
    else if constexpr (std::is_signed_v<From>)
        return number >= 0 && static_cast<std::make_unsigned_t<From>>(number) <= std::numeric_limits<To>::max();
    else return number <= static_cast<std::make_unsigned_t<To>>(std::numeric_limits<To>::max());
}
// Converts between a C++ scalar and its fixed-width C ABI type, checking the
// range where the two differ (for example, a 32-bit long carried as int64_t).
template<class To, class From> To convert(From number) {
    if constexpr (std::is_same_v<To, bool> || std::is_same_v<From, bool> || std::is_floating_point_v<To>) return static_cast<To>(number);
    else if constexpr (std::is_same_v<To, char>) return static_cast<char>(number);
    else if constexpr (std::is_same_v<From, char>) return static_cast<To>(static_cast<unsigned char>(number));
    else {
        if (!fits<To>(number)) fail(range, "integer outside C++ type range");
        return static_cast<To>(number);
    }
}
[[maybe_unused]] inline std::string text(const char* data, std::size_t size) { return size ? std::string(data, size) : std::string(); }
template<class T> T& live(void* cell) {
    auto* owned = static_cast<Cell<T>*>(cell);
    if (!owned || !owned->native) fail(closed, "native object is closed");
    return *owned->native;
}
template<class T> void* adopt(std::unique_ptr<T> native) {
    auto cell = std::make_unique<Cell<T>>();
    cell->native = native.release();
    return cell.release();
}
// Closing twice, or closing a nil object, is harmless.
template<class T> void close(void* cell) noexcept {
    std::lock_guard<std::mutex> hold(mutex());
    if (auto* owned = static_cast<Cell<T>*>(cell)) {
        delete owned->native;
        owned->native = nullptr;
    }
}
// Go's cleanup for an unreachable object: destroys the value unless it was
// closed, then the cell.
template<class T> void release(void* cell) noexcept {
    close<T>(cell);
    delete static_cast<Cell<T>*>(cell);
}
} } // anonymous namespace, rosetta_go
)cpp";

// The Go part shared by every package. @PACKAGE@ is the package name and
// @STATUS@ the C status type.
inline constexpr const char* go_interface_runtime = R"go(
// ErrorKind classifies an Error.
type ErrorKind int

// Error kinds. KindNative covers any other C++ exception.
const (
	KindAllocation ErrorKind = iota + 1
	KindRange
	KindIndex
	KindArgument
	KindNative
	KindClosed
)

func (k ErrorKind) String() string {
	switch k {
	case KindAllocation:
		return "allocation"
	case KindRange:
		return "range"
	case KindIndex:
		return "index"
	case KindArgument:
		return "argument"
	case KindNative:
		return "native"
	case KindClosed:
		return "closed"
	}
	return "unknown"
}

// Error is a C++ failure: an exception, a range check or a closed object.
type Error struct {
	Kind     ErrorKind
	Function string // the C++ declaration
	Message  string
}

func (e *Error) Error() string {
	return "@PACKAGE@: " + e.Function + ": " + e.Message + " (" + e.Kind.String() + ")"
}

// take copies and frees a string the C++ side allocated.
func take(data *C.char, size C.size_t) string {
	defer C.free(unsafe.Pointer(data))
	return string(unsafe.Slice((*byte)(unsafe.Pointer(data)), int(size)))
}

func failure(status *C.@STATUS@, function string) error {
	if status.code == 0 {
		return nil
	}
	return &Error{Kind: ErrorKind(status.code), Function: function, Message: take(status.message, status.size)}
}

func tooMany(function string) error {
	return &Error{Kind: KindArgument, Function: function, Message: "too many optional arguments"}
}

// bytes passes a string's bytes to C++, which copies them during the call.
func bytes(s string) (*C.char, C.size_t) {
	return (*C.char)(unsafe.Pointer(unsafe.StringData(s))), C.size_t(len(s))
}
)go";
} // namespace rosetta::detail
