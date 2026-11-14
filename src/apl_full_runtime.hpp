#pragma once

// GNU APL side of the full APL backend (--language apl --apl-api full): emitted (as text) after the
// shared C ABI library (cabi_full.hpp, lisp_full_runtime.hpp) in the main generated source, which
// is built as one GNU APL native function library (libMODULE_gnu_apl.so) against GNU APL's own
// configured headers. The generated script fixes it as PREFIX⍙native (Z←'lib' ⎕FX 'PREFIX⍙native')
// and every generated function calls it dyadically:
//
//   (index mode [self]) PREFIX⍙native B   calls generated function `index` (0: a returned
//                                         std::function, self is its handle)
//   (¯1) PREFIX⍙native H                  deletes object handle(s) H
//   (¯2 mode ctor [impl]) PREFIX⍙native B constructs; impl names an APL implementation (a
//                                         subclass overriding virtual methods: a director)
//   (¯3) PREFIX⍙native ⍬                  the kind and message of the latest error
//   (¯4 class) PREFIX⍙native V            1 if V is a handle of class (or a derived class)
//   (¯5) PREFIX⍙native ⍬                  the number of live handles
//
// mode says how B holds the arguments: 0 B is the one argument; 1 B is one argument when it is a
// scalar, a character vector, a handle or a function reference, no argument when it is ⍬, and
// otherwise a vector of arguments; 2 no arguments. The result is always (message event value):
// the wrapper signals message with ⎕ES event (⍬: no error) and returns value.
//
// Values are converted straight between GNU APL Values and the runtime's Nodes. Objects are
// handles, 2-item vectors (class id): the id indexes a registry here that holds the native Holder.
// Handles are released explicitly (PREFIX∆Delete); borrowed objects keep the object they live in
// alive until they are deleted too. Callbacks (a function name, ('⍙fn' 'NAME')) and overrides of
// virtual methods run APL re-entrantly through Command::do_APL_expression under ⎕EC, which the
// GNU APL interpreter supports from within native functions.
//
// Before this text the generator defines, in ROSETTA_LISP_MODULE::rosetta_gnu: prefix, classes[]
// (APL class labels by class index), generics[] (overridable method names), out_masks[] and
// void_results[] (per function index), director_methods[] (class * generic_count + generic: the
// function index running the C++ implementation, 0 for pure virtual methods) and init().
namespace rosetta::detail {
inline constexpr const char* apl_full_runtime = R"cpp(
class NativeFunction;
#include "Native_interface.hh"
#include "Command.hh"
#include "Symbol.hh"
namespace ROSETTA_LISP_MODULE { namespace rosetta_gnu {
namespace rl = ROSETTA_LISP_MODULE::rosetta_lisp;
using rl::Node;

// ---------------------------------------------------------------- the handle registry
struct Entry {
    rl::Holder* holder = nullptr;   // native object (null once released)
    unsigned klass = 0;             // index into classes[] (class_count: a native std::function)
    std::string implementation;     // APL prefix of a director's overrides
    std::string function;           // an APL callback: the function name
    unsigned long long parent = 0;  // the object a borrowed object lives in
    long children = 0;              // borrowed objects living in this one
    long retained = 0;              // C++ references (callbacks, directors C++ owns)
    bool deleted = false;           // the APL handle was deleted
    bool temporary = false;         // a callback converted for one call
};
struct Registry {
    unsigned long long next = 1;
    std::unordered_map<unsigned long long, Entry> entries;
    std::vector<unsigned long long> temporaries;
};
inline Registry& registry() { static Registry* value = new Registry; return *value; }
inline bool& initialized() { static bool value = false; return value; }

inline std::string label_of(unsigned klass) { return klass < class_count ? classes[klass] : std::string("NativeFunction"); }
inline void forget(unsigned long long id);
// Releases an entry's native object once the handle is deleted and nothing borrows from it.
inline void settle(unsigned long long id) {
    auto& r = registry();
    auto found = r.entries.find(id);
    if (found == r.entries.end()) return;
    Entry& e = found->second;
    if (!e.deleted || e.children > 0) return;
    if (e.holder) {
        rl::Holder* holder = e.holder;
        e.holder = nullptr;
        rl::release(holder, rl::depth() == 0 ? 1 : 0);
    }
    const auto parent = e.parent;
    e.parent = 0;
    if (e.retained <= 0) r.entries.erase(found);
    if (parent) {
        auto p = r.entries.find(parent);
        if (p != r.entries.end()) { --p->second.children; settle(parent); }
    }
}
inline void forget(unsigned long long id) {
    auto& r = registry();
    auto found = r.entries.find(id);
    if (found == r.entries.end()) return;
    found->second.deleted = true;
    settle(id);
}
inline unsigned long long add_entry(Entry entry) {
    auto& r = registry();
    const auto id = r.next++;
    if (entry.parent) {
        auto p = r.entries.find(entry.parent);
        if (p == r.entries.end()) entry.parent = 0;
        else ++p->second.children;
    }
    r.entries.emplace(id, std::move(entry));
    return id;
}
inline void retain(unsigned long long id, int delta) {
    auto& r = registry();
    auto found = r.entries.find(id);
    if (found == r.entries.end()) return;
    found->second.retained += delta;
    if (found->second.retained <= 0 && (found->second.deleted || found->second.temporary)) {
        if (found->second.holder || found->second.children > 0) { found->second.deleted = true; settle(id); }
        else r.entries.erase(found);
    }
}
// Callbacks converted for one call that C++ did not keep.
inline void drop_temporaries(std::size_t from) {
    auto& r = registry();
    while (r.temporaries.size() > from) {
        const auto id = r.temporaries.back();
        r.temporaries.pop_back();
        auto found = r.entries.find(id);
        if (found != r.entries.end() && found->second.retained <= 0) r.entries.erase(found);
    }
}
inline void destroy_all() {
    auto& r = registry();
    for (auto& [id, e] : r.entries) {
        (void)id;
        if (e.holder) { rl::Holder* holder = e.holder; e.holder = nullptr; rl::release(holder, 1); }
    }
    r.entries.clear();
    r.temporaries.clear();
}
inline std::size_t live_handles() {
    std::size_t count = 0;
    for (const auto& [id, e] : registry().entries) { (void)id; if (!e.deleted && !e.temporary && e.function.empty()) ++count; }
    return count;
}

// ---------------------------------------------------------------- text
inline void encode(std::string& out, Unicode point) {
    auto u = static_cast<std::uint32_t>(point);
    if (u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) rl::fail("invalid character");
    if (u < 0x80) out += static_cast<char>(u);
    else if (u < 0x800) { out += static_cast<char>(0xC0 | (u >> 6)); out += static_cast<char>(0x80 | (u & 0x3F)); }
    else if (u < 0x10000) {
        out += static_cast<char>(0xE0 | (u >> 12)); out += static_cast<char>(0x80 | ((u >> 6) & 0x3F)); out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (u >> 18)); out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F)); out += static_cast<char>(0x80 | (u & 0x3F));
    }
}
inline std::vector<std::uint32_t> decode(const std::string& text) {
    std::vector<std::uint32_t> points;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t size = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 0;
        if (size == 0 || i + size > text.size()) { points.push_back(0xFFFD); ++i; continue; }
        std::uint32_t point = size == 1 ? lead : size == 2 ? lead & 0x1Fu : size == 3 ? lead & 0x0Fu : lead & 0x07u;
        bool ok = true;
        for (std::size_t k = 1; k < size; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) { ok = false; break; }
            point = (point << 6) | (next & 0x3Fu);
        }
        if (!ok) { points.push_back(0xFFFD); ++i; continue; }
        points.push_back(point);
        i += size;
    }
    return points;
}
inline Value_P text_value(const std::string& text) {
    const auto points = decode(text);
    if (points.empty()) return Str0(LOC);
    Value_P Z(static_cast<ShapeItem>(points.size()), LOC);
    for (const auto point : points) Z->next_ravel_Char(static_cast<Unicode>(point));
    Z->check_value(LOC);
    return Z;
}
inline bool is_text(const Value& v) {
    if (v.get_rank() > 1) return false;
    if (v.element_count() == 0) return v.get_rank() == 1 && v.get_cfirst().is_character_cell();
    for (ShapeItem i = 0; i < v.element_count(); ++i) if (!v.get_cravel(i).is_character_cell()) return false;
    return true;
}
inline std::string text_of(const Value& v) {
    std::string out;
    for (ShapeItem i = 0; i < v.element_count(); ++i) encode(out, v.get_cravel(i).get_char_value());
    return out;
}
inline bool is_empty_numeric(const Value& v) { return v.get_rank() == 1 && v.element_count() == 0 && !v.get_cfirst().is_character_cell(); }
// A nested item: the value of a pointer cell, or the cell as a scalar.
inline Value_P item_value(const Cell& cell) {
    if (cell.is_pointer_cell()) return cell.get_pointer_value();
    Value_P Z(cell, LOC);
    return Z;
}
inline Value_P none() { return Idx0(LOC); }
// Builds a vector (or matrix) from item values: simple scalars stay simple.
inline Value_P items_value(const std::vector<Value_P>& items, const ::Shape& shape) {
    if (items.empty()) return none();
    Value_P Z(shape, LOC);
    for (Value_P item : items) {
        if (item->is_simple_scalar()) Z->next_ravel_Cell(item->get_cfirst());
        else Z->next_ravel_Pointer(item.get());
    }
    Z->check_value(LOC);
    return Z;
}
inline Value_P vector_value(const std::vector<Value_P>& items) { return items_value(items, ::Shape(static_cast<ShapeItem>(items.size()))); }

// ---------------------------------------------------------------- APL -> Node
inline Node integer_node(long long value) { Node n(rl::tag_int); n.integer = value; return n; }
inline Node real_node(double value) { Node n(rl::tag_real); n.real = value; return n; }
inline Node string_node(std::string text) { return rl::text_node(rl::tag_string, std::move(text)); }
// A handle: (class id) with a known id of that class.
inline Entry* handle_entry(const Value& v, unsigned long long* id_out = nullptr) {
    if (v.get_rank() != 1 || v.element_count() != 2) return nullptr;
    const Cell& first = v.get_cravel(0);
    const Cell& second = v.get_cravel(1);
    if (!second.is_integer_cell()) return nullptr;
    std::string label;
    if (first.is_pointer_cell()) {
        Value_P name = first.get_pointer_value();
        if (!is_text(*name)) return nullptr;
        label = text_of(*name);
    } else if (first.is_character_cell()) encode(label, first.get_char_value());
    else return nullptr;
    const auto id = static_cast<unsigned long long>(second.get_int_value());
    auto& r = registry();
    auto found = r.entries.find(id);
    if (found == r.entries.end() || label_of(found->second.klass) != label || !found->second.function.empty()) {
        // A handle of a known class whose object is gone.
        for (std::size_t k = 0; k <= class_count; ++k)
            if (label_of(static_cast<unsigned>(k)) == label && second.get_int_value() > 0 && id < r.next) {
                static Entry closed;
                closed = Entry();
                closed.deleted = true;
                closed.klass = static_cast<unsigned>(k);
                if (id_out) *id_out = 0;
                return &closed;
            }
        return nullptr;
    }
    if (id_out) *id_out = id;
    return &found->second;
}
// A function reference: ('⍙fn' 'NAME').
inline bool function_reference(const Value& v, std::string* name) {
    if (v.get_rank() != 1 || v.element_count() != 2) return false;
    const Value_P tag = item_value(v.get_cravel(0));
    const Value_P target = item_value(v.get_cravel(1));
    if (!is_text(*tag) || text_of(*tag) != "⍙fn" || !is_text(*target)) return false;
    *name = text_of(*target);
    return true;
}
inline bool apl_name(const std::string& name) {
    const auto points = decode(name);
    if (points.empty()) return false;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto c = points[i];
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == 0x2206 || c == 0x2359;
        const bool digit = (c >= '0' && c <= '9') || c == 0xAF;
        if (!(letter || (i > 0 && digit))) return false;
    }
    return true;
}
inline Node value_node(const Value& v, unsigned depth = 0);
inline Node cell_node(const Cell& cell, unsigned depth) {
    if (cell.is_pointer_cell()) return value_node(*cell.get_pointer_value(), depth + 1);
    if (cell.is_character_cell()) { std::string text; encode(text, cell.get_char_value()); return string_node(text); }
    if (cell.is_integer_cell()) return integer_node(cell.get_int_value());
    if (cell.is_float_cell()) return real_node(cell.get_real_value());
    if (cell.is_complex_cell()) { Node n(rl::tag_complex); n.real = cell.get_real_value(); n.imag = cell.get_imag_value(); return n; }
    rl::fail("unsupported APL value");
}
inline Node value_node(const Value& v, unsigned depth) {
    if (depth > 1000) rl::fail("value nesting is too deep");
    if (v.get_rank() == 0) return cell_node(v.get_cfirst(), depth);
    if (is_text(v)) return string_node(text_of(v));
    if (v.get_rank() == 1) {
        if (v.element_count() == 0) return Node();
        unsigned long long id = 0;
        if (Entry* entry = handle_entry(v, &id)) {
            // Native std::function handles too: PREFIX∆call passes them as the callee.
            Node n(rl::tag_object);
            n.holder = entry->deleted ? nullptr : entry->holder;
            n.id = id;
            return n;
        }
        std::string name;
        if (function_reference(v, &name)) {
            if (!apl_name(name)) rl::fail("invalid APL function name in function reference: " + name);
            Entry e;
            e.function = name;
            e.temporary = true;
            e.klass = class_count + 1;
            const auto id = add_entry(std::move(e));
            registry().temporaries.push_back(id);
            Node n(rl::tag_function);
            n.id = id;
            return n;
        }
        bool reals = true, integers = true;
        for (ShapeItem i = 0; i < v.element_count(); ++i) {
            const Cell& c = v.get_cravel(i);
            if (!c.is_integer_cell()) integers = false;
            if (!c.is_integer_cell() && !c.is_float_cell()) reals = false;
        }
        if (reals && !integers) {
            Node n(rl::tag_reals);
            n.reals.reserve(static_cast<std::size_t>(v.element_count()));
            for (ShapeItem i = 0; i < v.element_count(); ++i) n.reals.push_back(v.get_cravel(i).get_real_value());
            return n;
        }
        Node n(rl::tag_list);
        n.items.reserve(static_cast<std::size_t>(v.element_count()));
        for (ShapeItem i = 0; i < v.element_count(); ++i) n.items.push_back(cell_node(v.get_cravel(i), depth));
        return n;
    }
    if (v.get_rank() == 2) {
        // A matrix: a sequence of its rows (a character matrix: its rows as strings).
        const ShapeItem rows = v.get_shape_item(0), columns = v.get_shape_item(1);
        Node n(rl::tag_list);
        bool chars = true;
        for (ShapeItem i = 0; i < v.element_count(); ++i) if (!v.get_cravel(i).is_character_cell()) chars = false;
        for (ShapeItem r = 0; r < rows; ++r) {
            if (chars && columns > 0) {
                std::string text;
                for (ShapeItem c = 0; c < columns; ++c) encode(text, v.get_cravel(r * columns + c).get_char_value());
                n.items.push_back(string_node(text));
                continue;
            }
            Node row(rl::tag_list);
            for (ShapeItem c = 0; c < columns; ++c) row.items.push_back(cell_node(v.get_cravel(r * columns + c), depth));
            n.items.push_back(std::move(row));
        }
        return n;
    }
    rl::fail("arrays of rank 3 or more are not supported");
}

// ---------------------------------------------------------------- Node -> APL
inline Value_P handle_value(unsigned long long id, unsigned klass) {
    Value_P Z(2, LOC);
    Value_P label = text_value(label_of(klass));
    Z->next_ravel_Pointer(label.get());
    Z->next_ravel_Int(static_cast<APL_Integer>(id));
    Z->check_value(LOC);
    return Z;
}
inline Value_P lookup(unsigned long long id) {
    auto found = registry().entries.find(id);
    if (found == registry().entries.end()) rl::fail("the APL object no longer exists");
    return handle_value(id, found->second.klass);
}
inline bool numeric_kind(rl::Tag kind) {
    return kind == rl::tag_int || kind == rl::tag_real || kind == rl::tag_bigint || kind == rl::tag_t || kind == rl::tag_false;
}
inline Value_P from_node(const Node& n, const std::vector<Value_P>* arguments, const std::vector<unsigned long long>* ids, unsigned depth = 0);
inline Value_P number_value(const Node& n) {
    switch (n.kind) {
    case rl::tag_t: return IntScalar(1, LOC);
    case rl::tag_false: return IntScalar(0, LOC);
    case rl::tag_int: return IntScalar(n.integer, LOC);
    case rl::tag_bigint: return FloatScalar(std::strtod(n.text.c_str(), nullptr), LOC);
    default: return FloatScalar(n.real, LOC);
    }
}
inline void next_number(Value& Z, const Node& n) {
    switch (n.kind) {
    case rl::tag_t: Z.next_ravel_Int(1); break;
    case rl::tag_false: Z.next_ravel_Int(0); break;
    case rl::tag_int: Z.next_ravel_Int(n.integer); break;
    case rl::tag_bigint: Z.next_ravel_Float(std::strtod(n.text.c_str(), nullptr)); break;
    case rl::tag_complex: Z.next_ravel_Complex(n.real, n.imag); break;
    default: Z.next_ravel_Float(n.real);
    }
}
inline Value_P list_value(const Node& n, const std::vector<Value_P>* arguments, const std::vector<unsigned long long>* ids, unsigned depth, bool matrices) {
    const auto& items = n.items;
    if (items.empty()) return none();
    const auto count = static_cast<ShapeItem>(items.size());
    const auto all = [&](auto&& test) { return std::all_of(items.begin(), items.end(), test); };
    if (all([](const Node& item) { return numeric_kind(item.kind) || item.kind == rl::tag_complex; })) {
        Value_P Z(count, LOC);
        for (const auto& item : items) next_number(*Z, item);
        Z->check_value(LOC);
        return Z;
    }
    if (matrices) {
        // Rows of numbers of one length: a matrix.
        const auto row_length = [](const Node& item) -> long long {
            if (item.kind == rl::tag_reals) return static_cast<long long>(item.reals.size());
            if (item.kind == rl::tag_list && !item.items.empty() &&
                std::all_of(item.items.begin(), item.items.end(), [](const Node& x) { return numeric_kind(x.kind) || x.kind == rl::tag_complex; }))
                return static_cast<long long>(item.items.size());
            return -1;
        };
        const long long columns = row_length(items.front());
        if (columns > 0 && all([&](const Node& item) { return row_length(item) == columns; })) {
            Value_P Z(::Shape(count, static_cast<ShapeItem>(columns)), LOC);
            for (const auto& row : items) {
                if (row.kind == rl::tag_reals) for (const double x : row.reals) Z->next_ravel_Float(x);
                else for (const auto& x : row.items) next_number(*Z, x);
            }
            Z->check_value(LOC);
            return Z;
        }
    }
    std::vector<Value_P> values;
    values.reserve(items.size());
    for (const auto& item : items) values.push_back(from_node(item, arguments, ids, depth + 1));
    return vector_value(values);
}
inline Value_P from_node(const Node& n, const std::vector<Value_P>* arguments, const std::vector<unsigned long long>* ids, unsigned depth) {
    if (depth > 1000) rl::fail("value nesting is too deep");
    switch (n.kind) {
    case rl::tag_nil: return none();
    case rl::tag_t: case rl::tag_false: case rl::tag_int: case rl::tag_bigint: case rl::tag_real: return number_value(n);
    case rl::tag_complex: {
        Value_P Z(LOC);
        Z->next_ravel_Complex(n.real, n.imag);
        Z->check_value(LOC);
        return Z;
    }
    case rl::tag_string: case rl::tag_symbol: return text_value(n.text);
    case rl::tag_bytes: {
        if (n.text.empty()) return none();
        Value_P Z(static_cast<ShapeItem>(n.text.size()), LOC);
        for (const char c : n.text) Z->next_ravel_Int(static_cast<unsigned char>(c));
        Z->check_value(LOC);
        return Z;
    }
    case rl::tag_reals: {
        if (n.reals.empty()) return none();
        Value_P Z(static_cast<ShapeItem>(n.reals.size()), LOC);
        for (const double x : n.reals) Z->next_ravel_Float(x);
        Z->check_value(LOC);
        return Z;
    }
    case rl::tag_list: return list_value(n, arguments, ids, depth, true);
    case rl::tag_tuple: case rl::tag_cons: return list_value(n, arguments, ids, depth, false);
    case rl::tag_map: {
        // A two-column matrix: keys, values.
        const auto count = static_cast<ShapeItem>(n.items.size() / 2);
        if (count == 0) {
            Value_P Z(::Shape(ShapeItem(0), ShapeItem(2)), LOC);
            Z->set_proto_Int();
            Z->check_value(LOC);
            return Z;
        }
        std::vector<Value_P> values;
        for (const auto& item : n.items) values.push_back(from_node(item, arguments, ids, depth + 1));
        return items_value(values, ::Shape(count, ShapeItem(2)));
    }
    case rl::tag_object: {
        if (!n.holder) return none();
        Entry e;
        e.holder = n.holder;
        e.klass = n.klass;
        if (n.integer >= 0 && ids && static_cast<std::size_t>(n.integer) < ids->size()) e.parent = (*ids)[static_cast<std::size_t>(n.integer)];
        if (e.klass >= class_count) { rl::release(n.holder, 1); rl::fail("C++ result type is not exported to APL"); }
        return handle_value(add_entry(std::move(e)), n.klass);
    }
    case rl::tag_function: {
        Entry e;
        e.holder = n.holder;
        e.klass = class_count;
        return handle_value(add_entry(std::move(e)), class_count);
    }
    case rl::tag_lisp_object: return lookup(n.id);
    case rl::tag_argument:
        if (!arguments || static_cast<std::size_t>(n.integer) >= arguments->size()) rl::fail("malformed native result");
        return (*arguments)[static_cast<std::size_t>(n.integer)];
    default: rl::fail("malformed native result");
    }
}

// ---------------------------------------------------------------- the byte encodings (hooks)
// The C++ -> host encoding (rosetta_lisp::Writer) back to Nodes.
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
// Nodes to the host -> C++ encoding (rosetta_lisp::Reader).
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

// ---------------------------------------------------------------- calling APL (the hooks)
// The APL error a callback raised, signalled again once control is back in APL.
struct CallbackError { bool set = false; std::string message; long long event[2] = {5, 4}; };
inline CallbackError& callback_error() { static CallbackError value; return value; }
inline UCS_string ucs(const std::string& text) { UTF8_string utf8(text.c_str()); return UCS_string(utf8); }
inline std::string variable(const char* suffix) { return std::string(prefix) + "⍙" + suffix; }
inline void assign(const std::string& name, Value_P value) {
    Symbol* symbol = Workspace::lookup_symbol(ucs(name));
    if (!symbol) rl::fail("cannot create APL variable " + name);
    symbol->assign(value, true, LOC);
}
inline bool defined_function(const std::string& name) {
    Symbol* symbol = Workspace::lookup_existing_symbol(ucs(name));
    return symbol && symbol->get_NC() == NC_FUNCTION;
}
// Runs `expression` under ⎕EC: the value it returns (or null without one); APL errors are
// saved and thrown as rosetta_lisp::LispFailure.
inline Value_P run_apl(const std::string& expression, const std::string& what) {
    const auto result_name = variable("cbR");
    std::string quoted;
    for (const char c : expression) { quoted += c; if (c == '\'') quoted += '\''; }
    UCS_string line = ucs(result_name + "←⎕EC '" + quoted + "'");
    const StateIndicator* before = Workspace::SI_top();
    rl::flush_output();
    Command::do_APL_expression(line);
    if (Workspace::SI_top() != before) rl::fail("APL callback " + what + " did not return");
    Symbol* symbol = Workspace::lookup_existing_symbol(ucs(result_name));
    Value_P r = symbol ? symbol->get_apl_value() : Value_P();
    assign(result_name, none());
    if (!r || r->element_count() < 2) rl::fail("APL callback " + what + " failed");
    const auto rc = r->get_cravel(0).get_int_value();
    if (rc == 0) {
        auto& saved = callback_error();
        saved.set = true;
        const Value_P event = item_value(r->get_cravel(1));
        if (event->element_count() >= 2) { saved.event[0] = event->get_cravel(0).get_int_value(); saved.event[1] = event->get_cravel(1).get_int_value(); }
        std::string message;
        if (r->element_count() >= 3) {
            const Value_P text = item_value(r->get_cravel(2));
            const ShapeItem columns = text->get_rank() == 2 ? text->get_shape_item(1) : text->element_count();
            for (ShapeItem i = 0; i < columns && i < text->element_count(); ++i)
                if (text->get_cravel(i).is_character_cell()) {
                    // GNU APL pads its error lines with private-use fill characters.
                    const auto c = static_cast<std::uint32_t>(text->get_cravel(i).get_char_value());
                    encode(message, c >= 0xE000 && c <= 0xF8FF ? Unicode(' ') : text->get_cravel(i).get_char_value());
                }
            while (!message.empty() && message.back() == ' ') message.pop_back();
        }
        saved.message = message.empty() ? std::string("APL error") : message;
        throw rl::LispFailure(saved.message + " in " + what);
    }
    if (rc == 1 || rc == 2) return r->element_count() >= 3 ? item_value(r->get_cravel(2)) : Value_P();
    return Value_P();
}
inline Value_P arguments_value(const std::vector<Value_P>& values) {
    if (values.size() == 1) return values[0];
    return vector_value(values);
}
inline int finish(Encoder& encoder, unsigned char** out, std::size_t* size) {
    *size = encoder.out.size();
    *out = static_cast<unsigned char*>(std::malloc(encoder.out.empty() ? 1 : encoder.out.size()));
    if (!*out) { *size = 0; return 3; }
    std::memcpy(*out, encoder.out.data(), encoder.out.size());
    return 0;
}
// kind 0: call APL function `id` (a callback); kind 1: generic `generic` on the director object `id`.
inline int invoke(int kind, unsigned long long id, unsigned generic, const unsigned char* in, std::size_t length,
                  unsigned char** out, std::size_t* size) {
    kind &= 0xff;
    Encoder encoder;
    int status = 0;
    try {
        Decoder decoder{in, in + length};
        const auto count = decoder.raw<unsigned>();
        std::vector<Node> nodes;
        for (unsigned i = 0; i < count; ++i) nodes.push_back(decoder.node());
        auto& r = registry();
        auto found = r.entries.find(id);
        if (found == r.entries.end()) rl::fail("the APL callback no longer exists");
        Value_P result;
        if (kind == 0) {
            const std::string name = found->second.function;
            std::vector<Value_P> values;
            for (const auto& node : nodes) values.push_back(from_node(node, nullptr, nullptr));
            assign(variable("cbB"), values.empty() ? none() : arguments_value(values));
            result = run_apl(name + " " + variable("cbB"), name);
        } else {
            if (generic >= generic_count) rl::fail("unknown overridable method");
            const Entry entry = found->second;
            const std::string method = generics[generic];
            const std::string name = entry.implementation + "∆" + method;
            if (!entry.implementation.empty() && defined_function(name)) {
                std::vector<Value_P> values;
                for (std::size_t i = 1; i < nodes.size(); ++i) values.push_back(from_node(nodes[i], nullptr, nullptr));
                assign(variable("cbS"), handle_value(id, entry.klass));
                if (values.empty()) result = run_apl(name + " " + variable("cbS"), name);
                else {
                    assign(variable("cbB"), arguments_value(values));
                    result = run_apl(variable("cbS") + " " + name + " " + variable("cbB"), name);
                }
            } else {
                // Not overridden in APL: the C++ implementation of the class.
                const std::size_t index = entry.klass < class_count ? director_methods[entry.klass * generic_count + generic] : 0;
                if (index == 0) rl::fail("pure virtual method " + method + " is not implemented by the APL implementation " + entry.implementation);
                rl::Call c;
                Node self(rl::tag_object);
                self.holder = entry.holder;
                self.id = id;
                c.args.push_back(self);
                for (std::size_t i = 1; i < nodes.size(); ++i) {
                    Node& node = nodes[i];
                    if (node.kind == rl::tag_object) {
                        // A C++ object passed to the override: hand it back borrowed.
                        Node object(rl::tag_object);
                        object.holder = node.holder;
                        c.args.push_back(object);
                    } else c.args.push_back(node);
                }
                table[index](c);
                encoder.node(c.result.kind == rl::tag_object ? Node() : c.result);
                const int written = finish(encoder, out, size);
                return written;
            }
        }
        rl::flush_output();
        if (!result) encoder.node(Node());
        else encoder.node(value_node(*result));
    } catch (const rl::LispFailure& error) {
        encoder.out.clear();
        encoder.node(string_node(error.what()));
        status = 2;
    } catch (const std::exception& error) {
        encoder.out.clear();
        encoder.node(string_node(error.what()));
        status = 1;
    } catch (const Error&) {
        throw;
    } catch (...) {
        encoder.out.clear();
        encoder.node(string_node("unknown error in APL callback"));
        status = 1;
    }
    const int written = finish(encoder, out, size);
    return written ? written : status;
}
inline void retain_hook(unsigned long long id, int delta) { retain(id, delta); }

// ---------------------------------------------------------------- the native function
struct Last { std::string kind, message; };
inline Last& last() { static Last* value = new Last; return *value; }
inline Token reply(const std::string& message, long long major, long long minor, Value_P value) {
    Value_P Z(3, LOC);
    Value_P text = message.empty() ? Str0(LOC) : text_value(message);
    Z->next_ravel_Pointer(text.get());
    if (major) {
        Value_P event(2, LOC);
        event->next_ravel_Int(major);
        event->next_ravel_Int(minor);
        event->check_value(LOC);
        Z->next_ravel_Pointer(event.get());
    } else {
        Value_P empty = none();
        Z->next_ravel_Pointer(empty.get());
    }
    if (!value) value = none();
    if (value->is_simple_scalar()) Z->next_ravel_Cell(value->get_cfirst());
    else Z->next_ravel_Pointer(value.get());
    Z->check_value(LOC);
    return Token(TOK_APL_VALUE1, Z);
}
inline std::string kind_of(const std::exception& error) {
    std::string kind = rl::error_kind(error);
    for (auto& c : kind) if (c == '-') c = '_';
    return kind;
}
// One native call's bookkeeping: depth (garbage released between calls), the argument label of
// conversion errors and the callbacks converted for it.
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
inline long long integer_item(const Value& A, ShapeItem i, const char* what) {
    if (A.element_count() <= i || !A.get_cravel(i).is_integer_cell()) rl::fail(std::string("invalid native call: ") + what);
    return A.get_cravel(i).get_int_value();
}
// The arguments B holds in a mode (see the header).
inline void split(const Value_P& B, long long mode, std::vector<Value_P>& values) {
    if (mode == 2) return;
    if (mode == 0) { values.push_back(B); return; }
    unsigned long long id = 0;
    std::string name;
    if (B->get_rank() == 0 || is_text(*B) || B->get_rank() > 1 || handle_entry(*B, &id) || function_reference(*B, &name)) {
        values.push_back(B);
        return;
    }
    if (is_empty_numeric(*B)) return;
    for (ShapeItem i = 0; i < B->element_count(); ++i) values.push_back(item_value(B->get_cravel(i)));
}
inline Token entry(Value_P A, Value_P B) {
    if (!initialized()) {
        if (init(1, &invoke, &retain_hook) != 0) return reply(std::string(prefix) + ": binding: incompatible native library", 5, 4, Value_P());
        initialized() = true;
    }
    std::string kind, message;
    long long major = 0, minor = 0;
    Value_P output;
    {
        Scope scope;
        try {
            const long long op = integer_item(*A, 0, "operation");
            if (op == -1) {
                // Delete one handle, or a vector of them.
                std::vector<Value_P> handles;
                unsigned long long id = 0;
                if (handle_entry(*B, &id) || is_empty_numeric(*B)) { if (id) handles.push_back(B); }
                else for (ShapeItem i = 0; i < B->element_count(); ++i) handles.push_back(item_value(B->get_cravel(i)));
                for (const auto& handle : handles) {
                    unsigned long long hid = 0;
                    if (!handle_entry(*handle, &hid)) rl::fail("expected an object handle");
                    if (hid) forget(hid);
                }
                output = none();
            } else if (op == -3) {
                output = vector_value({text_value(last().kind), text_value(last().message)});
            } else if (op == -4) {
                const long long klass = integer_item(*A, 1, "class");
                unsigned long long id = 0;
                Entry* e = handle_entry(*B, &id);
                bool result = false;
                if (e && e->holder && e->holder->info && klass >= 0 && static_cast<std::size_t>(klass) < class_count) {
                    const rl::TypeInfo* target = nullptr;
                    for (const auto& [type, info] : rl::registry()) { (void)type; if (info->index == static_cast<unsigned>(klass) && info->name) target = info; }
                    result = target && rl::derives(e->holder->info, target);
                }
                output = IntScalar(result ? 1 : 0, LOC);
            } else if (op == -5) {
                output = IntScalar(static_cast<APL_Integer>(live_handles()), LOC);
            } else {
                const bool construct = op == -2;
                const long long mode = integer_item(*A, 1, "mode");
                const long long index = construct ? integer_item(*A, 2, "constructor") : op;
                if (index < 0 || static_cast<std::size_t>(index) >= function_count || !table[index]) rl::fail("unknown native function");
                rl::Call c;
                std::vector<Value_P> originals;
                std::vector<unsigned long long> ids;
                unsigned long long director = 0;
                if (construct) {
                    // The subclass marker: the registry id of the APL implementation, or none.
                    if (A->element_count() > 3) {
                        const Value_P implementation = item_value(A->get_cravel(3));
                        if (!is_text(*implementation) || !apl_name(text_of(*implementation)))
                            rl::fail("the implementation must be an APL name prefix");
                        Entry e;
                        e.implementation = text_of(*implementation);
                        e.deleted = false;
                        director = add_entry(std::move(e));
                        c.args.push_back(integer_node(static_cast<long long>(director)));
                    } else c.args.push_back(Node());
                    originals.push_back(none());
                    ids.push_back(0);
                } else if (A->element_count() > 2) {
                    // The object (or first operand) on the left.
                    const Value_P self = item_value(A->get_cravel(2));
                    unsigned long long id = 0;
                    handle_entry(*self, &id);
                    rl::converting() = "left argument";
                    c.args.push_back(value_node(*self));
                    rl::converting() = nullptr;
                    originals.push_back(self);
                    ids.push_back(id);
                }
                std::vector<Value_P> values;
                split(B, mode, values);
                const unsigned long long mask = construct ? 0 : out_masks[index];
                for (const auto& value : values) {
                    const auto position = c.args.size();
                    unsigned long long id = 0;
                    handle_entry(*value, &id);
                    Node node = value_node(*value);
                    if (position < 64 && (mask >> position) & 1 && node.kind != rl::tag_object) {
                        Node cell(rl::tag_ref);
                        cell.integer = static_cast<long long>(position);
                        cell.items.push_back(std::move(node));
                        node = std::move(cell);
                    }
                    c.args.push_back(std::move(node));
                    originals.push_back(value);
                    ids.push_back(id);
                }
                try {
                    table[index](c);
                } catch (...) {
                    if (director) registry().entries.erase(director);
                    throw;
                }
                rl::flush_output();
                rl::converting() = nullptr;
                if (construct) {
                    if (c.result.kind != rl::tag_object || !c.result.holder) rl::fail("construction failed");
                    if (director) {
                        Entry& e = registry().entries.at(director);
                        e.holder = c.result.holder;
                        e.klass = c.result.klass;
                        output = handle_value(director, e.klass);
                    } else output = from_node(c.result, &originals, &ids);
                } else {
                    std::vector<Value_P> outputs;
                    if (!void_results[index]) outputs.push_back(from_node(c.result, &originals, &ids));
                    std::sort(c.writebacks.begin(), c.writebacks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                    for (const auto& [slot, value] : c.writebacks) { (void)slot; outputs.push_back(from_node(value, &originals, &ids)); }
                    if (mask == 0) output = outputs.empty() ? none() : outputs.front();
                    else output = outputs.size() == 1 ? outputs.front() : outputs.empty() ? none() : vector_value(outputs);
                }
            }
        } catch (const Error&) {
            throw;
        } catch (const rl::LispFailure& error) {
            auto& saved = callback_error();
            kind = "callback";
            if (saved.set) { message = error.what(); major = saved.event[0]; minor = saved.event[1]; saved = CallbackError(); }
            else { message = error.what(); major = 5; minor = 4; }
        } catch (const std::exception& error) {
            kind = kind_of(error);
            message = rl::converting() ? std::string(rl::converting()) + ": " + error.what() : std::string(error.what());
            major = 5; minor = 4;
            if (kind == "out_of_range") minor = 5;
            else if (kind == "length_error") minor = 3;
            else if (kind == "allocation") { major = 1; minor = 1; }
        } catch (...) {
            kind = "native";
            message = "unknown C++ exception";
            major = 5; minor = 4;
        }
    }
    if (!kind.empty()) {
        const std::string text = std::string(prefix) + ": " + kind + ": " + message;
        last() = Last{kind, text};
        return reply(text, major, minor, Value_P());
    }
    return reply(std::string(), 0, 0, output);
}
} } // ROSETTA_LISP_MODULE::rosetta_gnu

static Fun_signature rosetta_signature() { return SIG_Z_A_F2_B; }
static Token rosetta_eval_AB(Value_P A, Value_P B, const NativeFunction*) { return ROSETTA_LISP_MODULE::rosetta_gnu::entry(A, B); }
static Token rosetta_eval_B(Value_P B, const NativeFunction*) {
    return ROSETTA_LISP_MODULE::rosetta_gnu::entry(IntScalar(-3, LOC), B);
}
// GNU APL removes the native function when it clears the workspace, which drops every handle.
static bool rosetta_close(Cause, const NativeFunction*) { ROSETTA_LISP_MODULE::rosetta_gnu::destroy_all(); return false; }
extern "C" void* get_function_mux(const char* function) {
    if (!std::strcmp(function, "get_signature")) return reinterpret_cast<void*>(&rosetta_signature);
    if (!std::strcmp(function, "eval_AB")) return reinterpret_cast<void*>(&rosetta_eval_AB);
    if (!std::strcmp(function, "eval_B")) return reinterpret_cast<void*>(&rosetta_eval_B);
    if (!std::strcmp(function, "close_fun")) return reinterpret_cast<void*>(&rosetta_close);
    return nullptr;
}
)cpp";
} // namespace rosetta::detail
