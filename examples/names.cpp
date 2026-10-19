#include "demo.hpp"

namespace demo_names {
Count imported_count(Count value) { return value; }
Operation imported_operation(Operation value) { return value; }
values::vector<Operation> imported_values(values::vector<Operation> value) { return value; }
int imported_counter(const Counter& value) { return value.value(); }
values::string imported_text(values::string value) { return value; }
NestedOwner::Mode imported_nested(NestedOwner::Mode value) { return value; }
vector<Operation> using_vector(vector<Operation> value) { return value; }
optional<values::string> using_optional(optional<values::string> value) { return value; }
tuple<values::string, Count> using_tuple(tuple<values::string, Count> value) { return value; }
} // namespace demo_names
