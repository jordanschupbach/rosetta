#define ROSETTA_DISABLE_INT128 1
#include "numeric_library.hpp"

template<class Operation> bool unavailable(Operation operation) {
    try { operation(); }
    catch (const std::runtime_error& error) {
        return std::string(error.what()).find("unavailable in this build") != std::string::npos;
    }
    return false;
}
int main() {
    namespace numeric = feature_library::numeric;
    static_assert(!numeric::has_int128);
    for (auto operation : {numeric::signed_identity, numeric::unsigned_identity})
        if (!unavailable([&] { return operation("42"); })) return 1;
    for (auto operation : {numeric::signed_add, numeric::unsigned_add, numeric::signed_multiply, numeric::unsigned_multiply})
        if (!unavailable([&] { return operation("21", "2"); })) return 1;
    if (numeric::u64_add("18446744073709551614", "1") != "18446744073709551615") return 1;
    if (numeric::parse_float(numeric::format(42.0L)) != 42.0L) return 1;
}
