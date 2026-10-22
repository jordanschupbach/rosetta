#pragma once

namespace rosetta::detail {
inline constexpr const char* guile_runtime = R"cpp(
namespace { namespace rosetta_guile {
// Guile reports errors with a non-local exit that skips C++ destructors, so
// C++ code never calls a raising libguile function. Conversions check first;
// failures are C++ exceptions that guard() turns into a Guile error once every
// C++ object it owns has been destroyed.
struct error { const char* kind; std::string message; };
[[noreturn]] inline void fail(const char* kind, std::string message) { throw error{kind, std::move(message)}; }
// Native calls, explicit closes and finalizers (which run on Guile's
// finalizer thread) are serialized per extension. Recursive, because a
// collection inside a call never waits for this thread.
inline std::recursive_mutex& mutex() { static auto* value = new std::recursive_mutex; return *value; }
// Live native objects, destroyed at process exit so their destructors run
// (for example, to stop worker threads). Leaked so it outlives static destructors.
struct Registry { std::unordered_map<void*, void (*)(void*)> owners; bool finished = false; };
inline Registry& registry() { static auto* value = new Registry; return *value; }
template<class T> void destroy(void* native) noexcept { delete static_cast<T*>(native); }
inline void shutdown() noexcept {
    std::lock_guard<std::recursive_mutex> hold(mutex());
    auto& owners = registry();
    owners.finished = true;
    for (auto& owner : owners.owners) owner.second(owner.first);
    owners.owners.clear();
}
// Strict UTF-8: no overlong forms, surrogates or values beyond U+10FFFF.
inline bool valid_utf8(const std::string& text) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    for (std::size_t i = 0, n = text.size(); i < n;) {
        const unsigned c = bytes[i];
        std::size_t extra = 0;
        unsigned point = 0, minimum = 0;
        if (c < 0x80) { ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF) { extra = 1; point = c & 0x1F; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; point = c & 0x0F; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; point = c & 0x07; minimum = 0x10000; }
        else return false;
        if (n - i <= extra) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((bytes[i + k] & 0xC0) != 0x80) return false;
            point = (point << 6) | (bytes[i + k] & 0x3F);
        }
        if (point < minimum || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}
[[maybe_unused]] inline SCM text(const std::string& value) {
    return valid_utf8(value) ? scm_from_utf8_stringn(value.data(), value.size()) : scm_from_latin1_stringn(value.data(), value.size());
}
// Raises (rosetta-error SUBR MESSAGE () (KIND)), the shape scm-error uses.
template<class F> SCM guard(const char* subr, F fn) {
    SCM kind_symbol = SCM_BOOL_F, message_text = SCM_BOOL_F;
    {
        const char* kind = "native";
        std::string message;
        try {
            std::lock_guard<std::recursive_mutex> hold(mutex());
            return fn();
        }
        catch (const error& e) { kind = e.kind; message = e.message; }
        catch (const std::bad_alloc&) { kind = "allocation"; message = "native allocation failed"; }
        catch (const std::overflow_error& e) { kind = "range"; message = e.what(); }
        catch (const std::out_of_range& e) { kind = "index"; message = e.what(); }
        catch (const std::invalid_argument& e) { kind = "argument"; message = e.what(); }
        catch (const std::exception& e) { message = e.what(); }
        catch (...) { message = "unknown C++ exception"; }
        kind_symbol = scm_from_utf8_symbol(kind);
        message_text = text(message);
    }
    scm_error_scm(scm_from_utf8_symbol("rosetta-error"), scm_from_utf8_string(subr), message_text, SCM_EOL,
        scm_list_1(kind_symbol));
    return SCM_UNSPECIFIED;
}
// Optional arguments arrive unbound; the count is the bound prefix.
[[maybe_unused]] inline std::size_t count(std::initializer_list<SCM> arguments, std::size_t required) {
    std::size_t present = required;
    for (auto it = arguments.begin() + static_cast<std::ptrdiff_t>(required); it != arguments.end() && !SCM_UNBNDP(*it); ++it) ++present;
    return present;
}
template<class T> T from(SCM value) {
    if constexpr (std::is_same_v<T, bool>) {
        if (!scm_is_bool(value)) fail("conversion", "expected a boolean");
        return scm_is_true(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!scm_is_string(value)) fail("conversion", "expected a string");
        std::size_t size = 0;
        char* bytes = scm_to_utf8_stringn(value, &size);
        std::unique_ptr<char, void (*)(void*)> owned(bytes, std::free);
        return std::string(bytes, size);
    } else if constexpr (std::is_integral_v<T>) {
        if (!scm_is_exact_integer(value)) fail("conversion", "expected an exact integer");
        if constexpr (std::is_signed_v<T>) {
            if (!scm_is_signed_integer(value, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()))
                fail("range", "integer outside C++ type range");
            return static_cast<T>(scm_to_int64(value));
        } else {
            if (!scm_is_unsigned_integer(value, 0, std::numeric_limits<T>::max())) fail("range", "integer outside C++ type range");
            return static_cast<T>(scm_to_uint64(value));
        }
    } else {
        if (!scm_is_real(value)) fail("conversion", "expected a real number");
        const double number = scm_to_double(value);
        if (!std::isfinite(number)) fail("range", "expected a finite number");
        if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
            fail("range", "number outside C++ type range");
        return static_cast<T>(number);
    }
}
template<class T> SCM to(const T& value) {
    if constexpr (std::is_same_v<T, bool>) return scm_from_bool(value);
    else if constexpr (std::is_same_v<T, std::string>) {
        if (!valid_utf8(value)) fail("conversion", "native string is not valid UTF-8");
        return scm_from_utf8_stringn(value.data(), value.size());
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return scm_from_int64(static_cast<std::int64_t>(value));
    else if constexpr (std::is_integral_v<T>) return scm_from_uint64(static_cast<std::uint64_t>(value));
    else {
        if (!std::isfinite(value)) fail("range", "non-finite C++ result");
        return scm_from_double(static_cast<double>(value));
    }
}
// Each object is a foreign object whose one slot owns the native value; it
// is null once closed. Finalization destroys a value that was never closed.
template<class T> void finalize(SCM object) {
    std::lock_guard<std::recursive_mutex> hold(mutex());
    auto* native = static_cast<T*>(scm_foreign_object_ref(object, 0));
    if (!native || registry().finished) return;
    scm_foreign_object_set_x(object, 0, nullptr);
    registry().owners.erase(native);
    delete native;
}
[[maybe_unused]] inline bool instance(SCM type, SCM object) { return SCM_STRUCTP(object) && scm_is_eq(scm_struct_vtable(object), type); }
template<class T> T* self(SCM type, SCM object, const char* name) {
    if (!instance(type, object)) fail("conversion", std::string("expected a ") + name);
    auto* native = static_cast<T*>(scm_foreign_object_ref(object, 0));
    if (!native) fail("conversion", "native object is closed");
    return native;
}
template<class T> SCM adopt(SCM type, std::unique_ptr<T> native) {
    registry().owners.emplace(native.get(), &destroy<T>);
    SCM object = scm_make_foreign_object_1(type, native.get());
    native.release();
    return object;
}
// Closing twice is harmless.
template<class T> SCM close(SCM type, SCM object, const char* name) {
    if (!instance(type, object)) fail("conversion", std::string("expected a ") + name);
    if (auto* native = static_cast<T*>(scm_foreign_object_ref(object, 0))) {
        scm_foreign_object_set_x(object, 0, nullptr);
        registry().owners.erase(native);
        delete native;
    }
    return SCM_UNSPECIFIED;
}
[[maybe_unused]] inline SCM type(const char* name, scm_t_struct_finalize finalizer) {
    SCM result = scm_make_foreign_object_type(scm_from_utf8_symbol(name), scm_list_1(scm_from_utf8_symbol("native")), finalizer);
    return scm_gc_protect_object(result);
}
} } // anonymous namespace, rosetta_guile
)cpp";

// The Scheme module beside the extension. @MODULE@ is the module name,
// @EXPORTS@ its exported names and @INIT@ the C initialization function.
inline constexpr const char* guile_interface_runtime = R"scm(
;; The extension sits beside this file; otherwise load-extension searches
;; GUILE_EXTENSIONS_PATH and the system library path.
(let* ((source (search-path %load-path "@MODULE@.scm"))
       (candidates (if source
                       (map (lambda (suffix) (string-append (dirname source) "/@MODULE@" suffix)) '(".so" ".dylib" ".dll"))
                       '()))
       (found (let loop ((files candidates))
                (cond ((null? files) "@MODULE@")
                      ((file-exists? (car files)) (car files))
                      (else (loop (cdr files)))))))
  (load-extension found "@INIT@"))
;; Native failures raise (rosetta-error SUBR MESSAGE () (KIND)); Guile
;; prints the "In procedure SUBR:" line itself.
(set-exception-printer! 'rosetta-error
  (lambda (port key args default-printer)
    (if (and (list? args) (= (length args) 4) (pair? (list-ref args 3)))
        (format port "~a (rosetta ~a)" (cadr args) (car (list-ref args 3)))
        (default-printer))))
)scm";
} // namespace rosetta::detail
