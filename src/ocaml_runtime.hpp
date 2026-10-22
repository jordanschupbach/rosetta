#pragma once

namespace rosetta::detail {
// @ERROR@ is replaced by the name the OCaml module registers its exception under.
inline constexpr const char* ocaml_runtime = R"cpp(
namespace { namespace rosetta_ocaml {
// OCaml raises with a non-local exit that skips C++ destructors, and a C++
// exception must never unwind through OCaml's root frames (CAMLparam). So C++
// work runs inside guard(), which catches every C++ exception and raises the
// OCaml exception only once its C++ objects have been destroyed.
// The order matches the OCaml type error_kind.
enum class kind : int { allocation, range, index, argument, native, closed };
struct error { kind code; std::string message; };
[[noreturn]] inline void fail(kind code, std::string message) { throw error{code, std::move(message)}; }
// Native calls, closes and finalizers are serialized per library. Calls run
// with the OCaml runtime released, so the lock is never held while waiting
// for the runtime (or a garbage collection) and threads in other domains,
// or other threads of this domain, keep running during long native calls.
inline std::mutex& mutex() { static auto* value = new std::mutex; return *value; }
// Each OCaml object is a custom block holding a pointer to a cell, which owns
// the native value and is null once closed. The cell is C++ memory, so it
// can be used while the runtime is released.
template<class T> struct Cell { T* native = nullptr; };
// Live native objects, destroyed at process exit so their destructors run
// (for example, to stop worker threads). Leaked so it outlives static destructors.
struct Registry { std::unordered_map<void*, void (*)(void*)> owners; bool finished = false; };
inline Registry& registry() { static auto* value = new Registry; return *value; }
template<class T> void destroy(void* cell) noexcept {
    auto* owned = static_cast<Cell<T>*>(cell);
    delete owned->native;
    owned->native = nullptr;
}
inline void shutdown() noexcept {
    std::lock_guard<std::mutex> hold(mutex());
    auto& owners = registry();
    owners.finished = true;
    for (auto& owner : owners.owners) owner.second(owner.first);
    owners.owners.clear();
}
// Runs FN with the OCaml runtime released and the library lock held. FN must
// not touch OCaml values.
template<class F> decltype(auto) native(F fn) {
    struct Reacquire { ~Reacquire() { caml_leave_blocking_section(); } };
    caml_enter_blocking_section();
    Reacquire reacquire;
    std::lock_guard<std::mutex> hold(mutex());
    return fn();
}
template<class F> auto guard(F fn) -> decltype(fn()) {
    int code = 0;
    value text = Val_unit;
    {
        kind failed = kind::native;
        std::string message;
        try { return fn(); }
        catch (const error& e) { failed = e.code; message = e.message; }
        catch (const std::bad_alloc&) { failed = kind::allocation; message = "native allocation failed"; }
        catch (const std::overflow_error& e) { failed = kind::range; message = e.what(); }
        catch (const std::out_of_range& e) { failed = kind::index; message = e.what(); }
        catch (const std::invalid_argument& e) { failed = kind::argument; message = e.what(); }
        catch (const std::exception& e) { message = e.what(); }
        catch (...) { message = "unknown C++ exception"; }
        code = static_cast<int>(failed);
        text = caml_alloc_initialized_string(message.size(), message.data());
    }
    // Nothing allocates between here and the raise, which roots its arguments.
    const value* exception = caml_named_value("@ERROR@");
    if (!exception) caml_failwith_value(text);
    value arguments[2] = {Val_int(code), text};
    caml_raise_with_args(*exception, 2, arguments);
}
// Defaulted parameters arrive as options; the count is the given prefix.
[[maybe_unused]] inline std::size_t present(std::initializer_list<value> options, std::size_t required,
                                            std::initializer_list<const char*> labels) {
    std::size_t given = required;
    const char* missing = nullptr;
    auto label = labels.begin();
    for (value option : options) {
        if (Is_some(option)) {
            if (missing) fail(kind::argument, std::string("optional argument ~") + *label + " requires ~" + missing);
            ++given;
        } else if (!missing) missing = *label;
        ++label;
    }
    return given;
}
template<class T> T from_int(value v) {
    const intnat number = Long_val(v);
    if (number < static_cast<intnat>(std::numeric_limits<T>::lowest()) || number > static_cast<intnat>(std::numeric_limits<T>::max()))
        fail(kind::range, "integer outside C++ type range");
    return static_cast<T>(number);
}
// Unsigned 64-bit values travel in int64 as the same bits, as Stdlib's
// Int64.unsigned_* functions expect.
template<class T> T from_int64(value v) {
    const std::int64_t number = Int64_val(v);
    if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(std::int64_t)) return static_cast<T>(static_cast<std::uint64_t>(number));
    else if constexpr (std::is_unsigned_v<T>) {
        if (number < 0 || static_cast<std::uint64_t>(number) > std::numeric_limits<T>::max()) fail(kind::range, "integer outside C++ type range");
        return static_cast<T>(number);
    } else {
        if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max()) fail(kind::range, "integer outside C++ type range");
        return static_cast<T>(number);
    }
}
template<class T> T from_float(value v) {
    const double number = Double_val(v);
    if (!std::isfinite(number)) fail(kind::range, "expected a finite number");
    if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max()) fail(kind::range, "number outside C++ type range");
    return static_cast<T>(number);
}
template<class T> T from_bool(value v) { return Bool_val(v); }
template<class T> T from_char(value v) { return static_cast<char>(static_cast<unsigned char>(Int_val(v))); }
template<class T> T from_string(value v) { return std::string(String_val(v), caml_string_length(v)); }
template<class T> T finite(T number) {
    if (!std::isfinite(number)) fail(kind::range, "non-finite C++ result");
    return number;
}
template<class T> value to_int(T number) { return Val_long(static_cast<intnat>(number)); }
template<class T> value to_int64(T number) { return caml_copy_int64(static_cast<std::int64_t>(number)); }
template<class T> value to_float(T number) { return caml_copy_double(static_cast<double>(number)); }
template<class T> value to_bool(T flag) { return Val_bool(flag); }
template<class T> value to_char(T c) { return Val_int(static_cast<unsigned char>(c)); }
template<class T> value to_string(const T& text) { return caml_alloc_initialized_string(text.size(), text.data()); }
// Read with the runtime held; the cell itself stays valid while its block is reachable.
template<class T> Cell<T>* cell(value object) { return *static_cast<Cell<T>**>(Data_custom_val(object)); }
template<class T> T& live(Cell<T>* owned) {
    if (!owned->native) fail(kind::closed, "native object is closed");
    return *owned->native;
}
// The block is allocated before the native value, so a failed constructor
// leaves a closed object for the collector instead of a leaked native one.
template<class T> value allocate(struct custom_operations* operations) {
    auto* owned = new Cell<T>;
    value object = caml_alloc_custom_mem(operations, sizeof(Cell<T>*), sizeof(T));
    *static_cast<Cell<T>**>(Data_custom_val(object)) = owned;
    return object;
}
template<class T, class F> void construct(Cell<T>* owned, F make) {
    native([&] {
        std::unique_ptr<T> created = make();
        registry().owners.emplace(owned, &destroy<T>);
        owned->native = created.release();
    });
}
// Closing twice is harmless.
template<class T> void close(Cell<T>* owned) {
    native([&] {
        if (!owned->native) return;
        registry().owners.erase(owned);
        delete owned->native;
        owned->native = nullptr;
    });
}
// Runs during a collection, with the runtime held: the lock holder never
// waits for the runtime, so taking the lock here cannot deadlock.
template<class T> void finalize(value object) {
    auto* owned = cell<T>(object);
    {
        std::lock_guard<std::mutex> hold(mutex());
        if (owned->native) {
            registry().owners.erase(owned);
            delete owned->native;
        }
    }
    delete owned;
}
} } // anonymous namespace, rosetta_ocaml
)cpp";

// The OCaml prelude. @MODULE@ is the OCaml module name and @ERROR@ the name
// the exception is registered under.
inline constexpr const char* ocaml_interface_runtime = R"ml(
type error_kind = Allocation | Range | Index | Argument | Native | Closed

exception Error of error_kind * string

let string_of_error_kind = function
  | Allocation -> "allocation" | Range -> "range" | Index -> "index"
  | Argument -> "argument" | Native -> "native" | Closed -> "closed"

let () =
  Stdlib.Callback.register_exception "@ERROR@" (Error (Native, ""));
  Stdlib.Printexc.register_printer (function
    | Error (kind, message) ->
        let name = String.capitalize_ascii (string_of_error_kind kind) in
        Some (Printf.sprintf "@MODULE@.Error(%s, %S)" name message)
    | _ -> None)
)ml";
} // namespace rosetta::detail
