#pragma once
// Names that collide with Fortran keywords, intrinsic types and procedures, the names generated
// code uses (size, present, status, rs_...) and Fortran's generic resolution rules (--fortran-api
// full): the generated module must still compile, and ambiguous overloads are dropped with a
// diagnostic (tests/test_fortran_names.f90, ctest fortran_full_names).
#include <cstdint>
#include <string>
#include <vector>

namespace names {

/// Named like an intrinsic type.
struct Integer {
    int value = 4;
    int size() const { return value; }
};
/// Named like an intrinsic type and a keyword.
struct Type {
    std::string kind = "derived";
    std::string len() const { return kind; }
};
/// Named like an intrinsic procedure.
struct Size {
    long long count = 9;
    std::int64_t total(std::int64_t extra) const { return count + extra; }
};
/// Named like the generated runtime's internal prefix.
struct rs_thing {
    int id = 5;
};
/// Same names in different case: Fortran is case-insensitive.
struct Point { double x = 1; };
inline double point(const Point& p) { return p.x; }

/// An enum named like the optional error argument.
enum class Status { ok, failed };

/// Parameters named like generated dummies and intrinsics.
inline int present(int status, int self, int size, int rs_w) { return status + self + size + rs_w; }
inline std::string trim(const std::string& value) { return "[" + value + "]"; }

/// Integers spelled as 64-bit types keep 64 bits; vectors of them too.
inline std::int64_t wide(std::int64_t value) { return value * 2; }
inline std::vector<std::int64_t> wides(const std::vector<std::int64_t>& values) {
    std::vector<std::int64_t> result;
    for (auto value : values) result.push_back(value + 1);
    return result;
}
/// int results out of the default integer range are errors.
inline long long huge_value() { return 1LL << 40; }
inline long narrow_overflow() { return 1L << 40; }

/// Overloads with the same Fortran signature reach one generic specific: C++ chooses at run time.
inline std::string same(int) { return "int"; }
inline std::string same(short) { return "short"; }
/// Overloads Fortran cannot distinguish (class(Base) accepts Derived): the second is dropped
/// (C++ resolves the call among all overloads at run time).
struct Base { virtual ~Base() = default; };
struct Derived : Base {};
inline std::string which(const Base&) { return "base"; }
inline std::string which(const Derived&) { return "derived"; }
/// Mixed function and subroutine overloads under one name: the subroutine is dropped.
inline int mixed(int value) { return value; }
inline void mixed(double) {}

} // namespace names
