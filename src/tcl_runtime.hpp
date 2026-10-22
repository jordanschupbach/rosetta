#pragma once

namespace rosetta::detail {
inline constexpr const char* tcl_runtime = R"cpp(
namespace { namespace rosetta_tcl {
// Tcl has no unwinding of its own: C++ exceptions stop at guard(), which sets
// the interpreter result and errorCode {ROSETTA kind}.
struct error { const char* kind; std::string message; };
// The interpreter result and errorCode are already set (Tcl_WrongNumArgs).
struct reported {};
[[noreturn]] inline void fail(const char* kind, std::string message) { throw error{kind, std::move(message)}; }
template<class F> int guard(Tcl_Interp* interp, F fn) {
    const char* kind = "native";
    std::string message;
    try {
        Tcl_Obj* result = fn();
        if (result) Tcl_SetObjResult(interp, result);
        else Tcl_ResetResult(interp);
        return TCL_OK;
    }
    catch (const reported&) { return TCL_ERROR; }
    catch (const error& e) { kind = e.kind; message = e.message; }
    catch (const std::bad_alloc&) { kind = "allocation"; message = "native allocation failed"; }
    catch (const std::overflow_error& e) { kind = "range"; message = e.what(); }
    catch (const std::out_of_range& e) { kind = "index"; message = e.what(); }
    catch (const std::invalid_argument& e) { kind = "argument"; message = e.what(); }
    catch (const std::exception& e) { message = e.what(); }
    catch (...) { message = "unknown C++ exception"; }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(message.data(), static_cast<Tcl_Size>(message.size())));
    Tcl_SetErrorCode(interp, "ROSETTA", kind, static_cast<char*>(nullptr));
    return TCL_ERROR;
}
[[maybe_unused]] inline int arity(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[], int skip, int minimum, int maximum, const char* usage) {
    const int count = objc - skip;
    if (count < minimum || count > maximum) { Tcl_WrongNumArgs(interp, skip, objv, usage); throw reported{}; }
    return count;
}
[[maybe_unused]] inline int choose(Tcl_Interp* interp, Tcl_Obj* object, const char* const* table, const char* what) {
    int index = 0;
    if (Tcl_GetIndexFromObj(interp, object, table, what, 0, &index) != TCL_OK) throw reported{};
    return index;
}
[[maybe_unused]] inline std::string message_of(Tcl_Interp* interp) {
    std::string text = Tcl_GetStringResult(interp);
    Tcl_ResetResult(interp);
    return text;
}
[[maybe_unused]] inline Tcl_Encoding utf8() {
    static const Tcl_Encoding encoding = Tcl_GetEncoding(nullptr, "utf-8");
    return encoding;
}
template<class T> T from(Tcl_Interp* interp, Tcl_Obj* object) {
    if constexpr (std::is_same_v<T, bool>) {
        // Tcl_GetBooleanFromObj is a GNU statement-expression macro in Tcl 9.
        char value = 0;
        if (Tcl_GetBoolFromObj(interp, object, 0, &value) != TCL_OK) fail("conversion", message_of(interp));
        return value != 0;
    } else if constexpr (std::is_same_v<T, std::string>) {
        // Tcl stores NUL as C0 80 internally; the UTF-8 encoding restores it.
        Tcl_Size size = 0;
        const char* text = Tcl_GetStringFromObj(object, &size);
        Tcl_DString external;
        Tcl_UtfToExternalDString(utf8(), text, size, &external);
        std::string result(Tcl_DStringValue(&external), static_cast<std::size_t>(Tcl_DStringLength(&external)));
        Tcl_DStringFree(&external);
        return result;
    } else if constexpr (std::is_integral_v<T>) {
        Tcl_WideInt wide = 0;
        if (Tcl_GetWideIntFromObj(nullptr, object, &wide) == TCL_OK) {
            if constexpr (std::is_signed_v<T>) {
                if (wide < std::numeric_limits<T>::lowest() || wide > std::numeric_limits<T>::max())
                    fail("range", "integer outside C++ type range");
            } else if (wide < 0 || static_cast<unsigned long long>(wide) > std::numeric_limits<T>::max())
                fail("range", "integer outside C++ type range");
            return static_cast<T>(wide);
        }
        if constexpr (std::is_unsigned_v<T>) {
            Tcl_WideUInt value = 0;
            if (Tcl_GetWideUIntFromObj(nullptr, object, &value) == TCL_OK) {
                if (value > std::numeric_limits<T>::max()) fail("range", "integer outside C++ type range");
                return static_cast<T>(value);
            }
        }
        // Either not an integer, or an integer beyond 64 bits.
        if (Tcl_GetWideIntFromObj(interp, object, &wide) != TCL_OK) {
            auto text = message_of(interp);
            if (text.find("too large") != std::string::npos) fail("range", "integer outside C++ type range");
            fail("conversion", text);
        }
        fail("range", "integer outside C++ type range");
    } else {
        double number = 0;
        if (Tcl_GetDoubleFromObj(interp, object, &number) != TCL_OK) fail("conversion", message_of(interp));
        if (!std::isfinite(number)) fail("range", "expected a finite number");
        if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max())
            fail("range", "number outside C++ type range");
        return static_cast<T>(number);
    }
}
// Strict UTF-8: no overlong forms, surrogates or values beyond U+10FFFF.
[[maybe_unused]] inline bool valid_utf8(const std::string& text) {
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
template<class T> Tcl_Obj* to(const T& value) {
    if constexpr (std::is_same_v<T, bool>) return Tcl_NewBooleanObj(value ? 1 : 0);
    else if constexpr (std::is_same_v<T, std::string>) {
        if (!valid_utf8(value)) fail("conversion", "native string is not valid UTF-8");
        if (value.size() > static_cast<std::size_t>(TCL_SIZE_MAX)) fail("range", "string too long for Tcl");
        Tcl_DString internal;
        Tcl_ExternalToUtfDString(utf8(), value.data(), static_cast<Tcl_Size>(value.size()), &internal);
        Tcl_Obj* result = Tcl_NewStringObj(Tcl_DStringValue(&internal), Tcl_DStringLength(&internal));
        Tcl_DStringFree(&internal);
        return result;
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(value));
    else if constexpr (std::is_integral_v<T>) return Tcl_NewWideUIntObj(static_cast<Tcl_WideUInt>(value));
    else {
        if (!std::isfinite(value)) fail("range", "non-finite C++ result");
        return Tcl_NewDoubleObj(static_cast<double>(value));
    }
}
// Each object is a command whose client data owns the native value; deleting
// the command (close, rename to "", interpreter deletion) destroys it.
template<class T> void destroy(void* data) noexcept { delete static_cast<T*>(data); }
template<class T> Tcl_Obj* adopt(Tcl_Interp* interp, const char* prefix, std::unique_ptr<T> native, Tcl_ObjCmdProc* proc) {
    static std::atomic<unsigned long long> next{0};
    std::string name;
    Tcl_CmdInfo existing;
    do name = std::string(prefix) + "#" + std::to_string(++next);
    while (Tcl_GetCommandInfo(interp, name.c_str(), &existing));
    if (!Tcl_CreateObjCommand(interp, name.c_str(), proc, native.get(), &destroy<T>)) fail("native", "cannot create object command");
    native.release();
    return Tcl_NewStringObj(name.data(), static_cast<Tcl_Size>(name.size()));
}
[[maybe_unused]] inline void close(Tcl_Interp* interp, Tcl_Obj* command) {
    Tcl_Command token = Tcl_GetCommandFromObj(interp, command);
    if (!token || Tcl_DeleteCommandFromToken(interp, token) != 0) fail("native", "cannot delete object command");
}
} } // anonymous namespace, rosetta_tcl
)cpp";
} // namespace rosetta::detail
