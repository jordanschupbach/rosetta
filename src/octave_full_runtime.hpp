#pragma once

// Octave side of the full Octave backend (--octave-api full): emitted (as text) after the shared
// C ABI library (cabi_full.hpp, lisp_full_runtime.hpp) in the main generated source, which is
// built as one native oct-file. Its single DEFUN (__MODULE__) is what the generated .m stubs call:
//
//   __MODULE__(index, args...)            calls generated function number `index` (0: a returned
//                                         std::function), with the arguments as Octave values
//   __MODULE__(-1, holder, id)            releases an object (the classdef delete method)
//   __MODULE__(-2, obj, ctor, sub, args)  constructs the C++ object of a classdef object; `sub`
//                                         marks an Octave subclass instance (a director)
//
// Values are converted straight between octave_value and the runtime's Nodes (no byte encoding);
// callbacks and overrides of virtual methods come back through the runtime's two hooks, which do
// use the byte encoding. Classdef objects deriving from PACKAGE.NativeObject hold the address of
// their Holder (rosetta_h__), an id (rosetta_id__) and the object a borrowed result lives in
// (rosetta_keep__). The id table refers to the Octave objects weakly (their cdef_object_rep), and
// strongly while C++ keeps them (retain): a director owned by C++, a callback held by C++.
//
// Before this text the generator defines, in ROSETTA_LISP_MODULE::rosetta_oct: package, classes[]
// (Octave class of each class index), generics[] (method names of overridable methods),
// out_masks[] and void_results[] (per function index) and init() (rosetta_MODULE_init).
namespace rosetta::detail {
inline constexpr const char* octave_full_runtime = R"cpp(
namespace ROSETTA_LISP_MODULE { namespace rosetta_oct {
namespace rl = ROSETTA_LISP_MODULE::rosetta_lisp;
using rl::Node;

// ---------------------------------------------------------------- the id table
struct Strong { octave_value value; long count = 0; };
struct Registry {
    unsigned long long next = 1;
    std::unordered_map<unsigned long long, octave::cdef_object_rep*> weak; // live classdef objects
    std::unordered_map<unsigned long long, Strong> strong;                 // values C++ keeps alive
    std::vector<unsigned long long> temporaries;                           // function handles of this call
};
inline Registry& registry() { static Registry value; return value; }
inline octave::interpreter*& interpreter() { static octave::interpreter* value = nullptr; return value; }

inline octave::cdef_object_rep* rep_of(const octave::cdef_object& object) {
    return const_cast<octave::cdef_object_rep*>(object.get_rep());
}
inline octave_value strengthen(octave::cdef_object_rep* rep) {
    // A handle object's clone() is the object itself with one more reference.
    return octave::to_ov(octave::cdef_object(rep->clone()));
}
inline octave_value lookup(unsigned long long id) {
    auto& r = registry();
    const auto strong = r.strong.find(id);
    if (strong != r.strong.end()) return strong->second.value;
    const auto weak = r.weak.find(id);
    if (weak != r.weak.end()) return strengthen(weak->second);
    rl::fail("the Octave object no longer exists");
}
inline unsigned long long object_id(octave::cdef_object& object) {
    const octave_value current = object.get("rosetta_id__");
    unsigned long long id = current.isempty() ? 0 : static_cast<unsigned long long>(current.double_value());
    if (id == 0) {
        id = registry().next++;
        registry().weak[id] = rep_of(object);
        object.put("rosetta_id__", octave_value(static_cast<double>(id)));
    }
    return id;
}
// retain(id, delta): C++ references to an Octave value (a callback, or an object C++ owns).
inline void retain(unsigned long long id, int delta) {
    auto& r = registry();
    auto found = r.strong.find(id);
    if (found == r.strong.end()) {
        if (delta <= 0) return;
        const auto weak = r.weak.find(id);
        if (weak == r.weak.end()) return;
        found = r.strong.emplace(id, Strong{strengthen(weak->second), 0}).first;
    }
    found->second.count += delta;
    if (found->second.count <= 0) {
        // Dropping the last reference may run the object's delete method, which re-enters.
        octave_value last = std::move(found->second.value);
        r.strong.erase(found);
        last = octave_value();
    }
}
inline unsigned long long register_function(const octave_value& value) {
    auto& r = registry();
    const auto id = r.next++;
    r.strong.emplace(id, Strong{value, 0});
    r.temporaries.push_back(id);
    return id;
}
// Function handles converted for a call that C++ did not keep.
inline void drop_temporaries(std::size_t from) {
    auto& r = registry();
    while (r.temporaries.size() > from) {
        const auto id = r.temporaries.back();
        r.temporaries.pop_back();
        const auto found = r.strong.find(id);
        if (found != r.strong.end() && found->second.count <= 0) {
            octave_value last = std::move(found->second.value);
            r.strong.erase(found);
        }
    }
}

// ---------------------------------------------------------------- Octave -> Node
inline std::string native_class() { return std::string(package) + ".NativeObject"; }
inline Node integer_node(long long value) { Node n(rl::tag_int); n.integer = value; return n; }
inline Node real_node(double value) { Node n(rl::tag_real); n.real = value; return n; }
inline Node string_node(std::string text) { return rl::text_node(rl::tag_string, std::move(text)); }

inline Node to_node(const octave_value& v, unsigned depth = 0);
inline Node object_node(const octave_value& v) {
    octave_classdef* object = v.classdef_object_value();
    octave::cdef_object cdef = object->get_object();
    Node n(rl::tag_object);
    const octave_value h = cdef.get("rosetta_h__");
    const auto address = h.isempty() ? 0ULL : static_cast<unsigned long long>(h.uint64_scalar_value().value());
    n.holder = reinterpret_cast<rl::Holder*>(static_cast<std::uintptr_t>(address));
    n.id = object_id(cdef);
    return n;
}
inline Node map_node(const octave_value& v, unsigned depth) {
    auto& interp = *interpreter();
    const Cell keys = interp.feval("keys", octave_value_list(v), 1)(0).cell_value();
    const Cell values = interp.feval("values", octave_value_list(v), 1)(0).cell_value();
    Node n(rl::tag_map);
    for (octave_idx_type i = 0; i < keys.numel(); ++i) {
        n.items.push_back(to_node(keys(i), depth + 1));
        n.items.push_back(to_node(values(i), depth + 1));
    }
    return n;
}
inline Node to_node(const octave_value& v, unsigned depth) {
    if (depth > 1000) rl::fail("value nesting is too deep");
    if (v.is_classdef_object()) {
        octave_classdef* object = v.classdef_object_value();
        if (v.numel() != 1) rl::fail("arrays of objects are not supported; pass a cell array");
        if (object->is_instance_of(native_class())) return object_node(v);
        if (object->is_instance_of("containers.Map")) return map_node(v, depth);
        rl::fail("unsupported Octave object of class " + v.class_name());
    }
    if (v.is_function_handle()) {
        Node n(rl::tag_function);
        n.id = register_function(v);
        return n;
    }
    if (v.iscell()) {
        const Cell cell = v.cell_value();
        Node n(rl::tag_list);
        n.items.reserve(static_cast<std::size_t>(cell.numel()));
        for (octave_idx_type i = 0; i < cell.numel(); ++i) n.items.push_back(to_node(cell(i), depth + 1));
        return n;
    }
    if (v.is_string()) {
        const charMatrix chars = v.char_matrix_value();
        if (chars.rows() <= 1) return string_node(v.string_value());
        Node n(rl::tag_list);
        for (octave_idx_type i = 0; i < chars.rows(); ++i) n.items.push_back(string_node(chars.row_as_string(i)));
        return n;
    }
    if (v.isstruct()) {
        const octave_map map = v.map_value();
        const string_vector keys = map.fieldnames();
        const auto entry = [&](octave_idx_type index) {
            Node n(rl::tag_map);
            for (octave_idx_type k = 0; k < keys.numel(); ++k) {
                n.items.push_back(string_node(keys(k)));
                n.items.push_back(to_node(map.contents(keys(k))(index), depth + 1));
            }
            return n;
        };
        if (map.numel() == 1) return entry(0);
        Node n(rl::tag_list);
        for (octave_idx_type i = 0; i < map.numel(); ++i) n.items.push_back(entry(i));
        return n;
    }
    if (v.isempty() && (v.isnumeric() || v.islogical())) return Node();
    if (v.ndims() > 2) rl::fail("arrays with more than two dimensions are not supported");
    if (v.islogical()) {
        const boolNDArray values = v.bool_array_value();
        if (values.numel() == 1) return rl::boolean(values(0));
        Node n(rl::tag_list);
        for (octave_idx_type i = 0; i < values.numel(); ++i) n.items.push_back(rl::boolean(values(i)));
        return n;
    }
    if (v.iscomplex()) {
        const ComplexNDArray values = v.complex_array_value();
        const auto item = [&](octave_idx_type i) { Node n(rl::tag_complex); n.real = values(i).real(); n.imag = values(i).imag(); return n; };
        if (values.numel() == 1) return item(0);
        Node n(rl::tag_list);
        for (octave_idx_type i = 0; i < values.numel(); ++i) n.items.push_back(item(i));
        return n;
    }
    if (v.isinteger()) {
        if (v.is_uint64_type()) {
            const uint64NDArray values = v.uint64_array_value();
            const auto item = [&](octave_idx_type i) {
                const unsigned long long value = values(i).value();
                if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
                    return rl::text_node(rl::tag_bigint, std::to_string(value));
                return integer_node(static_cast<long long>(value));
            };
            if (values.numel() == 1) return item(0);
            Node n(rl::tag_list);
            for (octave_idx_type i = 0; i < values.numel(); ++i) n.items.push_back(item(i));
            return n;
        }
        if (v.is_uint8_type() && v.numel() != 1) {
            const uint8NDArray values = v.uint8_array_value();
            std::string bytes(static_cast<std::size_t>(values.numel()), '\0');
            for (octave_idx_type i = 0; i < values.numel(); ++i) bytes[static_cast<std::size_t>(i)] = static_cast<char>(values(i).value());
            return rl::text_node(rl::tag_bytes, std::move(bytes));
        }
        const int64NDArray values = v.int64_array_value();
        if (values.numel() == 1) return integer_node(values(0).value());
        Node n(rl::tag_list);
        for (octave_idx_type i = 0; i < values.numel(); ++i) n.items.push_back(integer_node(values(i).value()));
        return n;
    }
    if (v.is_double_type() || v.is_single_type()) {
        const NDArray values = v.array_value();
        if (values.numel() == 1) return real_node(values(0));
        const auto rows = values.rows(), columns = values.columns();
        if (rows == 1 || columns == 1) {
            Node n(rl::tag_reals);
            n.reals.assign(values.data(), values.data() + values.numel());
            return n;
        }
        // A matrix: a sequence of its rows.
        Node n(rl::tag_list);
        for (octave_idx_type i = 0; i < rows; ++i) {
            Node row(rl::tag_reals);
            for (octave_idx_type j = 0; j < columns; ++j) row.reals.push_back(values(i, j));
            n.items.push_back(std::move(row));
        }
        return n;
    }
    rl::fail("unsupported Octave value of class " + v.class_name());
}

// ---------------------------------------------------------------- Node -> Octave
inline octave_value skip_token() {
    octave_scalar_map token;
    token.assign("rosetta_skip__", octave_value(true));
    return octave_value(token);
}
inline octave_value make_object(rl::Holder* holder, unsigned klass, const octave_value& keep) {
    if (klass >= class_count) {
        rl::release(holder, 1);
        rl::fail("C++ result type is not exported to Octave");
    }
    auto& interp = *interpreter();
    octave_value object;
    try {
        object = interp.feval(classes[klass], octave_value_list(skip_token()), 1)(0);
    } catch (...) {
        rl::release(holder, 1);
        throw;
    }
    octave::cdef_object cdef = object.classdef_object_value()->get_object();
    cdef.put("rosetta_h__", octave_value(octave_uint64(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(holder)))));
    object_id(cdef);
    if (keep.is_defined()) cdef.put("rosetta_keep__", keep);
    return object;
}
inline octave_value from_node(const Node& n, const std::vector<octave_value>* arguments, unsigned depth = 0);
inline bool numeric_kind(rl::Tag kind) { return kind == rl::tag_int || kind == rl::tag_real; }
inline octave_value list_value(const Node& n, const std::vector<octave_value>* arguments, unsigned depth) {
    const auto& items = n.items;
    const auto count = static_cast<octave_idx_type>(items.size());
    if (items.empty()) return octave_value(Matrix(1, 0));
    const auto all = [&](auto&& test) { return std::all_of(items.begin(), items.end(), test); };
    if (all([](const Node& item) { return numeric_kind(item.kind); })) {
        RowVector values(count);
        for (octave_idx_type i = 0; i < count; ++i) values(i) = rl::real_of(items[static_cast<std::size_t>(i)]);
        return octave_value(values);
    }
    if (all([](const Node& item) { return item.kind == rl::tag_t || item.kind == rl::tag_false; })) {
        boolNDArray values(dim_vector(1, count));
        for (octave_idx_type i = 0; i < count; ++i) values(i) = items[static_cast<std::size_t>(i)].kind == rl::tag_t;
        return octave_value(values);
    }
    if (all([](const Node& item) { return item.kind == rl::tag_complex || numeric_kind(item.kind); })) {
        ComplexRowVector values(count);
        for (octave_idx_type i = 0; i < count; ++i) {
            const auto& item = items[static_cast<std::size_t>(i)];
            values(i) = item.kind == rl::tag_complex ? Complex(item.real, item.imag) : Complex(rl::real_of(item), 0);
        }
        return octave_value(values);
    }
    // Rows of equal length: a matrix.
    const auto row_length = [](const Node& item) -> long long {
        if (item.kind == rl::tag_reals) return static_cast<long long>(item.reals.size());
        if (item.kind == rl::tag_list && !item.items.empty() &&
            std::all_of(item.items.begin(), item.items.end(), [](const Node& x) { return numeric_kind(x.kind); }))
            return static_cast<long long>(item.items.size());
        return -1;
    };
    const long long columns = row_length(items.front());
    if (columns > 0 && all([&](const Node& item) { return row_length(item) == columns; })) {
        Matrix values(count, static_cast<octave_idx_type>(columns));
        for (octave_idx_type i = 0; i < count; ++i) {
            const auto& row = items[static_cast<std::size_t>(i)];
            for (octave_idx_type j = 0; j < columns; ++j)
                values(i, j) = row.kind == rl::tag_reals ? row.reals[static_cast<std::size_t>(j)] : rl::real_of(row.items[static_cast<std::size_t>(j)]);
        }
        return octave_value(values);
    }
    Cell cell(1, count);
    for (octave_idx_type i = 0; i < count; ++i) cell(i) = from_node(items[static_cast<std::size_t>(i)], arguments, depth + 1);
    return octave_value(cell);
}
inline octave_value from_node(const Node& n, const std::vector<octave_value>* arguments, unsigned depth) {
    if (depth > 1000) rl::fail("value nesting is too deep");
    switch (n.kind) {
    case rl::tag_nil: return octave_value(Matrix());
    case rl::tag_t: return octave_value(true);
    case rl::tag_false: return octave_value(false);
    case rl::tag_int:
        if (n.integer > (1LL << 53) || n.integer < -(1LL << 53)) return octave_value(octave_int64(n.integer));
        return octave_value(static_cast<double>(n.integer));
    case rl::tag_bigint: return octave_value(octave_uint64(std::strtoull(n.text.c_str(), nullptr, 10)));
    case rl::tag_real: return octave_value(n.real);
    case rl::tag_complex: return octave_value(Complex(n.real, n.imag));
    case rl::tag_string: case rl::tag_symbol: return octave_value(n.text);
    case rl::tag_bytes: {
        uint8NDArray values(dim_vector(1, static_cast<octave_idx_type>(n.text.size())));
        for (std::size_t i = 0; i < n.text.size(); ++i) values(static_cast<octave_idx_type>(i)) = static_cast<unsigned char>(n.text[i]);
        return octave_value(values);
    }
    case rl::tag_reals: {
        RowVector values(static_cast<octave_idx_type>(n.reals.size()));
        for (std::size_t i = 0; i < n.reals.size(); ++i) values(static_cast<octave_idx_type>(i)) = n.reals[i];
        return octave_value(values);
    }
    case rl::tag_list: return list_value(n, arguments, depth);
    case rl::tag_tuple: case rl::tag_cons: {
        Cell cell(1, static_cast<octave_idx_type>(n.items.size()));
        for (std::size_t i = 0; i < n.items.size(); ++i) cell(static_cast<octave_idx_type>(i)) = from_node(n.items[i], arguments, depth + 1);
        return octave_value(cell);
    }
    case rl::tag_map: {
        auto& interp = *interpreter();
        const auto count = static_cast<octave_idx_type>(n.items.size() / 2);
        if (count == 0) return interp.feval("containers.Map", octave_value_list(), 1)(0);
        Cell keys(1, count), values(1, count);
        for (octave_idx_type i = 0; i < count; ++i) {
            keys(i) = from_node(n.items[static_cast<std::size_t>(2 * i)], arguments, depth + 1);
            values(i) = from_node(n.items[static_cast<std::size_t>(2 * i + 1)], arguments, depth + 1);
        }
        octave_value_list map_arguments;
        map_arguments(0) = octave_value(keys);
        map_arguments(1) = octave_value(values);
        map_arguments(2) = octave_value("UniformValues");
        map_arguments(3) = octave_value(false);
        return interp.feval("containers.Map", map_arguments, 1)(0);
    }
    case rl::tag_object: {
        octave_value keep;
        if (n.integer >= 0 && arguments && static_cast<std::size_t>(n.integer) < arguments->size())
            keep = (*arguments)[static_cast<std::size_t>(n.integer)];
        return make_object(n.holder, n.klass, keep);
    }
    case rl::tag_lisp_object: return lookup(n.id);
    case rl::tag_argument:
        if (!arguments || static_cast<std::size_t>(n.integer) >= arguments->size()) rl::fail("malformed native result");
        return (*arguments)[static_cast<std::size_t>(n.integer)];
    case rl::tag_function: {
        // A C++ std::function: an anonymous function calling it through a NativeFunction object.
        auto& interp = *interpreter();
        const auto function_class = std::string(package) + ".NativeFunction";
        octave_value object;
        try {
            object = interp.feval(function_class, octave_value_list(skip_token()), 1)(0);
        } catch (...) {
            rl::release(n.holder, 1);
            throw;
        }
        octave::cdef_object cdef = object.classdef_object_value()->get_object();
        cdef.put("rosetta_h__", octave_value(octave_uint64(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(n.holder)))));
        object_id(cdef);
        return interp.feval(function_class + ".handle__", octave_value_list(object), 1)(0);
    }
    default: rl::fail("malformed native result");
    }
}

// ---------------------------------------------------------------- calls from C++ (the hooks)
// The C++ -> host byte encoding (rosetta_lisp::Writer) back to Nodes.
struct Decoder {
    const unsigned char* at;
    const unsigned char* end;
    void need(std::size_t n) const { if (static_cast<std::size_t>(end - at) < n) rl::fail("malformed value encoding"); }
    template<class T> T raw() { need(sizeof(T)); T value; std::memcpy(&value, at, sizeof(T)); at += sizeof(T); return value; }
    std::string bytes(std::size_t n) { need(n); std::string value(reinterpret_cast<const char*>(at), n); at += n; return value; }
    Node node(unsigned depth = 0) {
        if (depth > 10000) rl::fail("value nesting is too deep");
        Node n(static_cast<rl::Tag>(raw<unsigned char>()));
        switch (n.kind) {
        case rl::tag_nil: case rl::tag_t: case rl::tag_false: break;
        case rl::tag_int: n.integer = raw<long long>(); break;
        case rl::tag_real: n.real = raw<double>(); break;
        case rl::tag_complex: n.real = raw<double>(); n.imag = raw<double>(); break;
        case rl::tag_bigint: case rl::tag_string: case rl::tag_symbol: case rl::tag_bytes:
            n.text = bytes(static_cast<std::size_t>(raw<unsigned long long>())); break;
        case rl::tag_list: case rl::tag_tuple: case rl::tag_map: {
            auto count = raw<unsigned long long>();
            if (n.kind == rl::tag_map) count *= 2;
            if (count > static_cast<std::size_t>(end - at)) rl::fail("malformed value encoding");
            for (unsigned long long i = 0; i < count; ++i) n.items.push_back(node(depth + 1));
            break;
        }
        case rl::tag_cons: n.items.push_back(node(depth + 1)); n.items.push_back(node(depth + 1)); break;
        case rl::tag_reals: {
            const auto count = static_cast<std::size_t>(raw<unsigned long long>());
            need(count * sizeof(double));
            n.reals.resize(count);
            if (count) std::memcpy(n.reals.data(), at, count * sizeof(double));
            at += count * sizeof(double);
            break;
        }
        case rl::tag_object:
            n.holder = reinterpret_cast<rl::Holder*>(static_cast<std::uintptr_t>(raw<unsigned long long>()));
            n.klass = raw<unsigned>();
            n.integer = raw<long long>();
            break;
        case rl::tag_lisp_object: n.id = raw<unsigned long long>(); break;
        case rl::tag_function: n.holder = reinterpret_cast<rl::Holder*>(static_cast<std::uintptr_t>(raw<unsigned long long>())); break;
        case rl::tag_argument: n.integer = raw<unsigned>(); break;
        default: rl::fail("malformed value encoding");
        }
        return n;
    }
};
// Nodes to the host -> C++ byte encoding (rosetta_lisp::Reader).
struct Encoder {
    std::string out;
    void u8(unsigned char v) { out.push_back(static_cast<char>(v)); }
    template<class T> void raw(T value) { out.append(reinterpret_cast<const char*>(&value), sizeof(T)); }
    void text(const std::string& value) { raw<unsigned long long>(value.size()); out += value; }
    void node(const Node& n) {
        u8(n.kind);
        switch (n.kind) {
        case rl::tag_nil: case rl::tag_t: case rl::tag_false: break;
        case rl::tag_int: raw<long long>(n.integer); break;
        case rl::tag_real: raw<double>(n.real); break;
        case rl::tag_complex: raw<double>(n.real); raw<double>(n.imag); break;
        case rl::tag_bigint: case rl::tag_string: case rl::tag_symbol: case rl::tag_bytes: text(n.text); break;
        case rl::tag_list: case rl::tag_tuple: raw<unsigned long long>(n.items.size()); for (const auto& item : n.items) node(item); break;
        case rl::tag_map: raw<unsigned long long>(n.items.size() / 2); for (const auto& item : n.items) node(item); break;
        case rl::tag_cons: node(n.items.at(0)); node(n.items.at(1)); break;
        case rl::tag_reals:
            raw<unsigned long long>(n.reals.size());
            out.append(reinterpret_cast<const char*>(n.reals.data()), n.reals.size() * sizeof(double));
            break;
        case rl::tag_object: raw<unsigned long long>(reinterpret_cast<std::uintptr_t>(n.holder)); raw<unsigned long long>(n.id); break;
        case rl::tag_function: raw<unsigned long long>(n.id); break;
        case rl::tag_ref: raw<unsigned>(static_cast<unsigned>(n.integer)); node(n.items.at(0)); break;
        default: rl::fail("cannot encode value");
        }
    }
};
// The Octave error a callback raised, raised again (with its identifier) once control is back in Octave.
struct CallbackError { bool set = false; std::string identifier, message; };
inline CallbackError& callback_error() { static CallbackError value; return value; }

inline int finish(Encoder& encoder, unsigned char** out, std::size_t* size) {
    *size = encoder.out.size();
    *out = static_cast<unsigned char*>(std::malloc(encoder.out.empty() ? 1 : encoder.out.size()));
    if (!*out) { *size = 0; return 3; }
    std::memcpy(*out, encoder.out.data(), encoder.out.size());
    return 0;
}
inline int invoke(int kind, unsigned long long id, unsigned generic, const unsigned char* in, std::size_t length,
                  unsigned char** out, std::size_t* size) {
    const bool no_result = (kind & 0x100) != 0;
    kind &= 0xff;
    Encoder encoder;
    int status = 0;
    auto& interp = *interpreter();
    try {
        Decoder decoder{in, in + length};
        const auto count = decoder.raw<unsigned>();
        octave_value_list arguments;
        for (unsigned i = 0; i < count; ++i) arguments(static_cast<octave_idx_type>(i)) = from_node(decoder.node(), nullptr);
        octave_value_list results;
        if (kind == 0) results = interp.feval(lookup(id), arguments, no_result ? 0 : 1);
        else {
            if (generic >= generic_count) rl::fail("unknown overridable method");
            results = interp.feval(generics[generic], arguments, no_result ? 0 : 1);
        }
        std::cout.flush();
        if (no_result || results.length() == 0 || !results(0).is_defined()) encoder.node(Node());
        else encoder.node(to_node(results(0)));
    } catch (const octave::execution_exception& error) {
        interp.recover_from_exception();
        auto& saved = callback_error();
        saved.set = true;
        saved.identifier = error.identifier();
        saved.message = error.message();
        encoder.out.clear();
        encoder.node(string_node(error.message()));
        status = 2;
    } catch (const std::exception& error) {
        encoder.out.clear();
        encoder.node(string_node(error.what()));
        status = 1;
    } catch (...) {
        encoder.out.clear();
        encoder.node(string_node("unknown error in Octave callback"));
        status = 1;
    }
    const int written = finish(encoder, out, size);
    return written ? written : status;
}
inline void retain_hook(unsigned long long id, int delta) { retain(id, delta); }

// ---------------------------------------------------------------- the entry point
inline std::string error_identifier(const std::exception& error) {
    std::string kind = rl::error_kind(error);
    for (auto& c : kind) if (c == '-') c = '_';
    return std::string(package) + ":" + kind;
}
// One native call's bookkeeping: depth (garbage released between calls), the argument label of
// conversion errors and the function handles converted for it.
struct Scope {
    const char* label;
    std::size_t temporaries;
    Scope() : label(rl::converting()), temporaries(registry().temporaries.size()) {
        rl::converting() = nullptr;
        if (rl::depth()++ == 0) rl::drain();
    }
    ~Scope() {
        --rl::depth();
        rl::converting() = label;
        drop_temporaries(temporaries);
    }
};
inline bool wrap_reference(const octave_value& v) { return !v.is_classdef_object(); }
inline octave_value_list entry(octave::interpreter& interp, const octave_value_list& args, int nargout) {
    (void)nargout;
    if (!interpreter()) {
        interpreter() = &interp;
        if (init(1, &invoke, &retain_hook) != 0) error("%s: incompatible native module", package);
    }
    if (args.length() < 1 || !args(0).is_real_scalar()) error("%s: invalid native call", package);
    const long long op = static_cast<long long>(args(0).double_value());
    if (op == -1) {
        // delete(obj): the holder (owned objects are destroyed) and the object's id.
        const auto address = args.length() > 1 && !args(1).isempty() ? args(1).uint64_scalar_value().value() : 0ULL;
        const auto id = args.length() > 2 && !args(2).isempty() ? static_cast<unsigned long long>(args(2).double_value()) : 0ULL;
        if (id) registry().weak.erase(id);
        if (address) rl::release(reinterpret_cast<rl::Holder*>(static_cast<std::uintptr_t>(address)), rl::depth() == 0 ? 1 : 0);
        return octave_value_list();
    }
    bool failed = false;
    std::string identifier, message;
    octave_value_list output;
    {
        Scope scope;
        try {
            rl::Call c;
            std::vector<octave_value> originals;
            std::size_t index = 0, first = 1;
            octave::cdef_object self;
            unsigned long long self_id = 0;
            if (op == -2) {
                if (args.length() < 4 || !args(1).is_classdef_object()) rl::fail("invalid construction");
                self = args(1).classdef_object_value()->get_object();
                index = static_cast<std::size_t>(args(2).double_value());
                self_id = object_id(self);
                const bool subclass = args(3).is_true();
                Node marker = subclass ? integer_node(static_cast<long long>(self_id)) : Node();
                c.args.push_back(marker);
                originals.push_back(octave_value());
                first = 4;
            } else {
                if (op < 0 || static_cast<std::size_t>(op) >= function_count) rl::fail("unknown native function");
                index = static_cast<std::size_t>(op);
            }
            const unsigned long long mask = op == -2 ? 0 : out_masks[index];
            for (octave_idx_type i = static_cast<octave_idx_type>(first); i < args.length(); ++i) {
                const auto position = c.args.size();
                rl::converting() = nullptr;
                Node value = to_node(args(i));
                if (position < 64 && (mask >> position) & 1 && wrap_reference(args(i))) {
                    Node cell(rl::tag_ref);
                    cell.integer = static_cast<long long>(position);
                    cell.items.push_back(std::move(value));
                    value = std::move(cell);
                }
                c.args.push_back(std::move(value));
                originals.push_back(args(i));
            }
            if (index >= function_count || !table[index]) rl::fail("unknown native function");
            table[index](c);
            std::cout.flush();
            rl::converting() = nullptr;
            if (op == -2) {
                if (c.result.kind != rl::tag_object || !c.result.holder) rl::fail("construction failed");
                self.put("rosetta_h__", octave_value(octave_uint64(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(c.result.holder)))));
            } else {
                if (!void_results[index]) output(0) = from_node(c.result, &originals);
                std::sort(c.writebacks.begin(), c.writebacks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (const auto& [slot, value] : c.writebacks) output(output.length()) = from_node(value, &originals);
            }
        } catch (const octave::execution_exception&) {
            throw;
        } catch (const rl::LispFailure& error) {
            failed = true;
            auto& saved = callback_error();
            if (saved.set) { identifier = saved.identifier; message = saved.message; saved = CallbackError(); }
            else { identifier = std::string(package) + ":callback"; message = error.what(); }
        } catch (const std::exception& error) {
            failed = true;
            identifier = error_identifier(error);
            message = rl::converting() ? std::string(rl::converting()) + ": " + error.what() : std::string(error.what());
        } catch (...) {
            failed = true;
            identifier = std::string(package) + ":unknown";
            message = "unknown C++ exception";
        }
    }
    if (failed) {
        if (identifier.empty()) error("%s", message.c_str());
        error_with_id(identifier.c_str(), "%s", message.c_str());
    }
    return output;
}
} } // ROSETTA_LISP_MODULE::rosetta_oct
)cpp";
} // namespace rosetta::detail
