#pragma once

namespace rosetta::detail {
inline constexpr const char* r_runtime = R"cpp(
namespace { namespace rosetta_r {
struct unwind {};
inline void cleanup(void*, Rboolean jump) { if (jump) throw unwind{}; }
template<class F> SEXP allocate(SEXP token, F&& fn) {
    return R_UnwindProtect([](void* p) -> SEXP { return (*static_cast<F*>(p))(); },
        &fn, cleanup, nullptr, token);
}
template<class F> SEXP boundary(F fn) {
    SEXP token = PROTECT(R_MakeUnwindCont());
    char error[512] = {};
    bool jumping = false;
    bool failed = false;
    SEXP result = R_NilValue;
    try {
        // Protect the whole call: even reading an ALTREP argument can allocate.
        result = allocate(token, [&]() -> SEXP {
            // Ordinary C++ errors must return through R_UnwindProtect so it can
            // restore R's context before the boundary raises an R error.
            try { return fn(token); }
            catch (const std::exception& e) { failed = true; std::snprintf(error, sizeof(error), "%s", e.what()); }
            catch (...) { failed = true; std::snprintf(error, sizeof(error), "unknown C++ exception"); }
            return R_NilValue;
        });
    } catch (const unwind&) { jumping = true; }
    // Resume R errors only after native temporaries and exception objects are gone.
    if (jumping) R_ContinueUnwind(token);
    UNPROTECT(1);
    if (failed) Rf_error("%s", error);
    return result;
}
template<class T> T from(SEXP value) {
    const int kind = TYPEOF(value);
    if (kind != LGLSXP && kind != STRSXP && kind != INTSXP && kind != REALSXP)
        throw std::runtime_error("expected a scalar logical, string, integer or numeric value");
    if (XLENGTH(value) != 1) throw std::runtime_error("expected a scalar");
    if constexpr (std::is_same_v<T, bool>) {
        if (TYPEOF(value) != LGLSXP || LOGICAL(value)[0] == NA_LOGICAL)
            throw std::runtime_error("expected TRUE or FALSE, without NA");
        return LOGICAL(value)[0] != 0;
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (TYPEOF(value) != STRSXP || STRING_ELT(value, 0) == NA_STRING)
            throw std::runtime_error("expected a string, without NA");
        SEXP text = STRING_ELT(value, 0);
        // Normalize R's declared encoding before publishing UTF-8 back to R.
        return std::string(Rf_translateCharUTF8(text));
    } else {
        double n;
        if (TYPEOF(value) == INTSXP && INTEGER(value)[0] != NA_INTEGER) n = INTEGER(value)[0];
        else if (TYPEOF(value) == REALSXP) n = REAL(value)[0];
        else throw std::runtime_error("expected an integer or numeric scalar, without NA");
        if (!std::isfinite(n)) throw std::runtime_error("expected a finite number");
        const long double wide = n;
        if (wide < std::numeric_limits<T>::lowest() || wide > std::numeric_limits<T>::max())
            throw std::runtime_error("number outside C++ type range");
        if constexpr (std::is_integral_v<T>) {
            if (std::trunc(n) != n || std::abs(n) > 9007199254740992.0)
                throw std::runtime_error("expected an exactly representable integer (up to 2^53)");
        }
        return static_cast<T>(n);
    }
}
template<class T> SEXP to(const T& value, SEXP token) {
    (void)token;
    if constexpr (std::is_same_v<T, std::string>) {
        if (value.size() > static_cast<std::size_t>(INT_MAX)) throw std::runtime_error("string too long for R");
        SEXP result = PROTECT(Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(result, 0, Rf_mkCharLenCE(value.data(), static_cast<int>(value.size()), CE_UTF8));
        UNPROTECT(1);
        return result;
    } else if constexpr (std::is_same_v<T, bool>) {
        return Rf_ScalarLogical(value);
    } else {
        const double n = static_cast<double>(value);
        if (!std::isfinite(n)) throw std::runtime_error("non-finite C++ result");
        if constexpr (std::is_integral_v<T>) {
            if (static_cast<long double>(value) < -9007199254740992.0L ||
                static_cast<long double>(value) > 9007199254740992.0L)
                throw std::runtime_error("C++ integer result exceeds R's exact numeric range");
        }
        return Rf_ScalarReal(n);
    }
}
template<class T> T* object(SEXP ptr, SEXP tag) {
    if (TYPEOF(ptr) != EXTPTRSXP || R_ExternalPtrTag(ptr) != tag)
        throw std::runtime_error("wrong native object type");
    auto* result = static_cast<T*>(R_ExternalPtrAddr(ptr));
    if (!result) throw std::runtime_error("native object is closed or unserialized");
    return result;
}
template<class T> void finalize(SEXP ptr) {
    auto* value = static_cast<T*>(R_ExternalPtrAddr(ptr));
    R_ClearExternalPtr(ptr);
    delete value;
}
} } // namespace rosetta_r, anonymous namespace
)cpp";
} // namespace rosetta::detail
