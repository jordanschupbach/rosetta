#pragma once

namespace rosetta::detail {
// The C ABI both APL front ends call: Dyalog through ⎕NA, GNU APL through the
// bridge below. Every stub returns 0 or an error kind; a failure's message,
// or a string result, waits in a per-thread buffer until it is taken.
[[maybe_unused]] inline constexpr const char* apl_runtime = R"cpp(
namespace { namespace rosetta_apl {
enum kind : std::int32_t { allocation = 1, range, index, argument, native, closed };
struct error { kind code; std::string message; };
[[noreturn]] inline void fail(kind code, std::string message) { throw error{code, std::move(message)}; }
// Calls, closes and the exit cleanup are serialized per library.
[[maybe_unused]] inline std::mutex& mutex() { static auto* value = new std::mutex; return *value; }
[[maybe_unused]] inline std::string& pending() { static thread_local std::string value; return value; }
// Strict UTF-8: no overlong forms, surrogates or values above U+10FFFF.
// Returns the length of the sequence at TEXT[I], or 0 if it is invalid.
[[maybe_unused]] inline std::size_t sequence(const std::string& text, std::size_t i) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char lead = byte(i);
    if (lead < 0x80) return 1;
    std::size_t size = 0;
    std::uint32_t point = 0;
    if (lead >= 0xC2 && lead <= 0xDF) { size = 2; point = lead & 0x1Fu; }
    else if (lead >= 0xE0 && lead <= 0xEF) { size = 3; point = lead & 0x0Fu; }
    else if (lead >= 0xF0 && lead <= 0xF4) { size = 4; point = lead & 0x07u; }
    else return 0;
    if (i + size > text.size()) return 0;
    for (std::size_t k = 1; k < size; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) return 0;
        point = (point << 6) | (byte(i + k) & 0x3Fu);
    }
    if ((size == 3 && point < 0x800) || (size == 4 && (point < 0x10000 || point > 0x10FFFF)) || (point >= 0xD800 && point <= 0xDFFF))
        return 0;
    return size;
}
[[maybe_unused]] inline bool utf8(const std::string& text) {
    for (std::size_t i = 0, size = 0; i < text.size(); i += size)
        if (!(size = sequence(text, i))) return false;
    return true;
}
// Messages reach APL as text, so invalid bytes become U+FFFD.
[[maybe_unused]] inline std::string sanitize(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
        const auto size = sequence(text, i);
        if (size) { result.append(text, i, size); i += size; }
        else { result += "\xEF\xBF\xBD"; ++i; }
    }
    return result;
}
[[maybe_unused]] inline std::int32_t record(kind code, const char* message) noexcept {
    try { pending() = sanitize(message); } catch (...) { pending().clear(); }
    return code;
}
template<class F> std::int32_t guard(F fn) noexcept {
    try {
        std::lock_guard<std::mutex> hold(mutex());
        pending().clear();
        fn();
        return 0;
    }
    catch (const error& e) { return record(e.code, e.message.c_str()); }
    catch (const std::bad_alloc&) { return record(allocation, "native allocation failed"); }
    catch (const std::overflow_error& e) { return record(range, e.what()); }
    catch (const std::out_of_range& e) { return record(index, e.what()); }
    catch (const std::invalid_argument& e) { return record(argument, e.what()); }
    catch (const std::exception& e) { return record(native, e.what()); }
    catch (...) { return record(native, "unknown C++ exception"); }
}

// APL objects are numeric handles into this registry. Handles are never
// reused, so a closed or forged handle fails instead of reaching another object.
struct Object { const void* type; void* native; void (*destroy)(void*); };
struct Registry { std::map<std::uint64_t, Object> objects; std::uint64_t next = 1; };
[[maybe_unused]] inline Registry& registry() { static auto* value = new Registry; return *value; }
template<class T> const void* tag() { static const char value = 0; return &value; }
template<class T> void destroy(void* native) { delete static_cast<T*>(native); }
// Objects still open when the program exits, or when GNU APL clears the
// workspace, are destroyed in creation order.
[[maybe_unused]] inline void destroy_all() noexcept {
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
// Arguments arrive as int64_t, uint64_t or double and are checked against the C++ type.
template<class To, class From> To integer(From number) {
    if (!fits<To>(number)) fail(range, "integer outside C++ type range");
    return static_cast<To>(number);
}
[[maybe_unused]] inline bool boolean(std::int64_t number) {
    if (number != 0 && number != 1) fail(argument, "expected a boolean (0 or 1)");
    return number == 1;
}
[[maybe_unused]] inline char character(std::int64_t point) {
    if (point < 0 || point > 255) fail(range, "character outside the C++ char range");
    return static_cast<char>(static_cast<unsigned char>(point));
}
[[maybe_unused]] inline std::string text(const std::uint8_t* data, std::uint64_t size) {
    return size ? std::string(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size)) : std::string();
}
// Results APL cannot represent are errors rather than silently changed values.
[[maybe_unused]] inline double real(double number) {
    if (!std::isfinite(number)) fail(range, "non-finite floating-point result");
    return number;
}
[[maybe_unused]] inline std::uint64_t result_text(std::string value) {
    if (!utf8(value)) fail(range, "string result is not valid UTF-8");
    pending() = std::move(value);
    return pending().size();
}
} } // anonymous namespace, rosetta_apl
)cpp";

// The GNU APL native function. It reads APL values, calls the C ABI above and
// returns (message event value): an empty event when the call succeeded, or
// the ⎕ES event for the wrapper to signal. Only compiled with ROSETTA_GNU_APL,
// against a configured GNU APL source tree. @MODULE@ is the library name.
[[maybe_unused]] inline constexpr const char* gnu_apl_runtime = R"cpp(
class NativeFunction;
#include "Native_interface.hh"
namespace { namespace rosetta_gnu {
using rosetta_apl::fail;
// One argument: a ravel cell of the right argument, or the whole argument.
struct Argument { const Cell* cell = nullptr; const Value* value = nullptr; };
[[maybe_unused]] inline bool character_prototype(const Value& value) { return value.get_cfirst().is_character_cell(); }
[[maybe_unused]] inline bool characters(const Value& value) {
    if (value.get_rank() > 1) return false;
    if (value.element_count() == 0) return character_prototype(value);
    for (ShapeItem i = 0; i < value.element_count(); ++i)
        if (!value.get_cravel(i).is_character_cell()) return false;
    return true;
}
// A call's arguments from its right argument B: none for niladic functions;
// B itself for one parameter (or ⍬ to omit an optional one); otherwise the
// items of a vector, a scalar for one, or a whole string when the first
// parameter is a string.
[[maybe_unused]] inline std::vector<Argument> arguments(const Value& B, std::size_t min, std::size_t max, bool text_first) {
    std::vector<Argument> result;
    if (max == 0) return result;
    auto whole = [&] {
        Argument argument;
        if (B.is_scalar()) argument.cell = &B.get_cfirst(); else argument.value = &B;
        return argument;
    };
    const bool omitted = B.element_count() == 0 && B.get_rank() == 1 && !character_prototype(B);
    if (max == 1) {
        if (!(min == 0 && omitted)) result.push_back(whole());
    } else if (B.get_rank() > 1) fail(rosetta_apl::argument, "arguments must be a scalar or vector");
    else if (B.is_scalar() || (text_first && characters(B))) result.push_back(whole());
    else if (!omitted)
        for (ShapeItem i = 0; i < B.element_count(); ++i) {
            Argument argument;
            argument.cell = &B.get_cravel(i);
            result.push_back(argument);
        }
    if (result.size() < min || result.size() > max)
        fail(rosetta_apl::argument, min == max ? "expected " + std::to_string(min) + " argument" + (min == 1 ? "" : "s")
            : "expected " + std::to_string(min) + " to " + std::to_string(max) + " arguments");
    return result;
}
[[maybe_unused]] inline const Value* nested(const Argument& argument) {
    if (argument.cell && argument.cell->is_pointer_cell()) return argument.cell->get_pointer_value().get();
    return argument.value;
}
[[maybe_unused]] inline const Cell& single(const Argument& argument, const char* expected) {
    const Cell* cell = argument.cell;
    if (const Value* value = nested(argument)) cell = value->element_count() == 1 ? &value->get_cfirst() : nullptr;
    if (!cell || cell->is_pointer_cell()) fail(rosetta_apl::argument, expected);
    return *cell;
}
[[maybe_unused]] inline std::int64_t to_signed(const Argument& argument, const char* expected = "expected an integer") {
    const Cell& cell = single(argument, expected);
    if (cell.is_integer_cell()) return cell.get_int_value();
    if (!cell.is_float_cell()) fail(rosetta_apl::argument, expected);
    const double number = cell.get_real_value();
    if (std::trunc(number) != number) fail(rosetta_apl::argument, expected);
    if (number < -9223372036854775808.0 || number >= 9223372036854775808.0) fail(rosetta_apl::range, "integer outside C++ type range");
    return static_cast<std::int64_t>(number);
}
[[maybe_unused]] inline std::uint64_t to_unsigned(const Argument& argument) {
    const Cell& cell = single(argument, "expected an integer");
    if (cell.is_integer_cell()) {
        const auto number = cell.get_int_value();
        if (number < 0) fail(rosetta_apl::range, "integer outside C++ type range");
        return static_cast<std::uint64_t>(number);
    }
    if (!cell.is_float_cell()) fail(rosetta_apl::argument, "expected an integer");
    const double number = cell.get_real_value();
    if (std::trunc(number) != number) fail(rosetta_apl::argument, "expected an integer");
    if (number < 0 || number >= 18446744073709551616.0) fail(rosetta_apl::range, "integer outside C++ type range");
    return static_cast<std::uint64_t>(number);
}
[[maybe_unused]] inline std::int64_t to_boolean(const Argument& argument) { return to_signed(argument, "expected a boolean (0 or 1)"); }
[[maybe_unused]] inline double to_real(const Argument& argument) {
    const Cell& cell = single(argument, "expected a real number");
    if (cell.is_integer_cell()) return static_cast<double>(cell.get_int_value());
    if (!cell.is_float_cell()) fail(rosetta_apl::argument, "expected a real number");
    return cell.get_real_value();
}
[[maybe_unused]] inline std::int64_t to_character(const Argument& argument) {
    const Cell& cell = single(argument, "expected a character");
    if (!cell.is_character_cell()) fail(rosetta_apl::argument, "expected a character");
    return static_cast<std::int64_t>(cell.get_char_value());
}
[[maybe_unused]] inline void encode(std::string& out, std::int64_t point) {
    if (point < 0 || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) fail(rosetta_apl::argument, "invalid character");
    const auto u = static_cast<std::uint32_t>(point);
    if (u < 0x80) out += static_cast<char>(u);
    else if (u < 0x800) { out += static_cast<char>(0xC0 | (u >> 6)); out += static_cast<char>(0x80 | (u & 0x3F)); }
    else if (u < 0x10000) {
        out += static_cast<char>(0xE0 | (u >> 12)); out += static_cast<char>(0x80 | ((u >> 6) & 0x3F)); out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (u >> 18)); out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F)); out += static_cast<char>(0x80 | (u & 0x3F));
    }
}
// A character vector (or one character) as UTF-8.
[[maybe_unused]] inline std::string to_text(const Argument& argument) {
    std::string result;
    const Value* value = nested(argument);
    if (!value) {
        if (!argument.cell->is_character_cell()) fail(rosetta_apl::argument, "expected a character vector");
        encode(result, argument.cell->get_char_value());
        return result;
    }
    if (!characters(*value)) fail(rosetta_apl::argument, "expected a character vector");
    for (ShapeItem i = 0; i < value->element_count(); ++i) encode(result, value->get_cravel(i).get_char_value());
    return result;
}
[[maybe_unused]] inline std::uint64_t to_handle(const Value& A) {
    if (A.element_count() != 2 || !A.get_cravel(1).is_integer_cell() || A.get_cravel(1).get_int_value() < 1)
        fail(rosetta_apl::argument, "expected one object");
    return static_cast<std::uint64_t>(A.get_cravel(1).get_int_value());
}
[[maybe_unused]] inline Value_P integer(std::int64_t number) {
    Value_P Z(LOC);
    Z->next_ravel_Int(number);
    Z->check_value(LOC);
    return Z;
}
[[maybe_unused]] inline Value_P natural(std::uint64_t number) {
    if (number <= 9223372036854775807ULL) return integer(static_cast<std::int64_t>(number));
    // Larger unsigned values are floats when that is exact.
    const auto real = static_cast<double>(number);
    if (real >= 18446744073709551616.0 || static_cast<std::uint64_t>(real) != number)
        fail(rosetta_apl::range, "unsigned result is not exactly representable in APL");
    Value_P Z(LOC);
    Z->next_ravel_Float(real);
    Z->check_value(LOC);
    return Z;
}
[[maybe_unused]] inline Value_P real(double number) {
    Value_P Z(LOC);
    Z->next_ravel_Float(number);
    Z->check_value(LOC);
    return Z;
}
[[maybe_unused]] inline Value_P character(std::int64_t point) {
    Value_P Z(LOC);
    Z->next_ravel_Char(static_cast<Unicode>(point));
    Z->check_value(LOC);
    return Z;
}
// Decodes text the stubs have already validated as UTF-8.
[[maybe_unused]] inline Value_P text(const std::string& value) {
    std::vector<std::uint32_t> points;
    for (std::size_t i = 0; i < value.size();) {
        const auto size = rosetta_apl::sequence(value, i);
        auto lead = static_cast<unsigned char>(value[i]);
        std::uint32_t point = size == 1 ? lead : size == 2 ? lead & 0x1Fu : size == 3 ? lead & 0x0Fu : lead & 0x07u;
        for (std::size_t k = 1; k < size; ++k) point = (point << 6) | (static_cast<unsigned char>(value[i + k]) & 0x3Fu);
        points.push_back(point);
        i += size ? size : 1;
    }
    if (points.empty()) return Str0(LOC);
    Value_P Z(static_cast<ShapeItem>(points.size()), LOC);
    for (auto point : points) Z->next_ravel_Char(static_cast<Unicode>(point));
    Z->check_value(LOC);
    return Z;
}
[[maybe_unused]] inline std::string take() {
    std::string value = std::move(rosetta_apl::pending());
    rosetta_apl::pending().clear();
    return value;
}
[[maybe_unused]] inline const char* kind_name(std::int32_t code) {
    static const char* const names[] = {"native", "allocation", "range", "index", "argument", "native", "closed"};
    return code >= 1 && code <= 6 ? names[code] : names[0];
}
// The kind and message of the latest failure, for MODULE∆last_error.
struct Last { std::string kind, message; };
[[maybe_unused]] inline Last& last() { static auto* value = new Last; return *value; }
[[maybe_unused]] inline Token reply(const std::string& message, std::int32_t code, Value_P value) {
    Value_P Z(3, LOC);
    Value_P text_message = text(message);
    Z->next_ravel_Pointer(text_message.get());
    if (code) {
        Value_P event(2, LOC);
        event->next_ravel_Int(500 + code);
        event->next_ravel_Int(0);
        event->check_value(LOC);
        Z->next_ravel_Pointer(event.get());
    } else {
        Value_P none = Idx0(LOC);
        Z->next_ravel_Pointer(none.get());
    }
    if (!value) value = Idx0(LOC);
    Z->next_ravel_Value(value.get());
    Z->check_value(LOC);
    return Token(TOK_APL_VALUE1, Z);
}
// Runs one entry. CALL decodes the arguments, calls its stub and stores the
// result; it returns the stub's status.
template<class F> Token call(const char* label, F call) {
    std::int32_t code = 0;
    std::string message;
    Value_P result;
    try {
        code = call(result);
        if (code) message = take();
    }
    catch (const rosetta_apl::error& e) { code = e.code; message = e.message; }
    catch (const std::bad_alloc&) { code = rosetta_apl::allocation; message = "native allocation failed"; }
    if (!code) return reply(std::string(), 0, result);
    message = std::string("@MODULE@: ") + label + ": " + rosetta_apl::sanitize(message) + " (" + kind_name(code) + ")";
    last() = Last{kind_name(code), message};
    return reply(message, code, Value_P());
}
[[maybe_unused]] inline Token last_error() {
    Value_P Z(2, LOC);
    Value_P kind = text(last().kind), message = text(last().message);
    Z->next_ravel_Pointer(kind.get());
    Z->next_ravel_Pointer(message.get());
    Z->check_value(LOC);
    return reply(std::string(), 0, Z);
}
} } // anonymous namespace, rosetta_gnu
)cpp";
} // namespace rosetta::detail
