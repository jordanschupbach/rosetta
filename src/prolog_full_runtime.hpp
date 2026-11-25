#pragma once

#include <cstddef>

// Runtime of the Prolog backend (--language prolog; src/prolog_full.cpp), which speaks the value
// encoding of lisp_full_runtime.hpp (the C++ library every full C ABI backend shares): the
// SWI-Prolog foreign glue appended to the library's first source, and the Prolog runtime of the
// generated module. @C@ is the prefix of the native entry points, @M@ the Prolog module, @LIB@ the
// library name and @ENV@ the environment variable naming its path.
// (Split in parts: compilers limit string literal sizes.)
namespace rosetta::detail {
inline constexpr const char* prolog_full_glue_parts[] = {
R"cpp(
// ---------------------------------------------------------------- SWI-Prolog foreign glue
// The Prolog module loads this library with load_foreign_library/2 (entry @C@_prolog_install),
// which registers the foreign predicates below in module @M@. Compile this source with
// SWI-Prolog's headers (swipl --dump-runtime-variables: PLBASE/include); the library resolves
// the PL_ functions from the running Prolog.
#include <SWI-Prolog.h>
#include <SWI-Stream.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
namespace {
// The Prolog side of a C++ object or function: a blob holds a pointer to it.
struct RosettaHandle {
    void* holder = nullptr;        // C++ holder (null: closed)
    long klass = 0;                // class index (-1: a C++ function)
    atom_t atom = 0;               // its blob
    atom_t parent = 0;             // the object it points into (registered while this lives)
    unsigned long long id = 0;     // id C++ knows it by (0: none yet)
    record_t module = 0;           // extended objects: the module of the overrides
    record_t data = 0;             // and their data
    long strong = 0;               // references C++ holds (each registers the atom)
};
struct RosettaClosure {
    record_t goal = 0;
    bool has_result = true;
    long count = 0;                // references C++ holds, and 1 while a call passes it
};
std::unordered_map<unsigned long long, RosettaHandle*> rosetta_objects;
std::unordered_map<unsigned long long, RosettaClosure> rosetta_closures;
std::unordered_map<unsigned long long, record_t> rosetta_causes;
std::vector<std::string> rosetta_class_names;
unsigned long long rosetta_next = 0;
bool rosetta_halting = false;
const char* const rosetta_marker = " [prolog callback #";

int rosetta_release_blob(atom_t atom);
int rosetta_write_blob(IOSTREAM* stream, atom_t atom, int flags);
PL_blob_t rosetta_blob = [] {
    PL_blob_t type{};
    type.magic = PL_BLOB_MAGIC;
    type.flags = PL_BLOB_NOCOPY;
    type.name = const_cast<char*>("@M@_object");
    type.release = rosetta_release_blob;
    type.write = rosetta_write_blob;
    return type;
}();

RosettaHandle* rosetta_handle_of(atom_t atom) {
    PL_blob_t* type = nullptr;
    void* data = PL_blob_data(atom, nullptr, &type);
    return type == &rosetta_blob ? static_cast<RosettaHandle*>(data) : nullptr;
}
int rosetta_release_blob(atom_t atom) {
    auto* handle = rosetta_handle_of(atom);
    if (!handle) return TRUE;
    if (handle->holder) @C@_release(handle->holder, 0);
    if (handle->id) {
        const auto found = rosetta_objects.find(handle->id);
        if (found != rosetta_objects.end() && found->second == handle) rosetta_objects.erase(found);
    }
    if (handle->parent) PL_unregister_atom(handle->parent);
    if (handle->module) PL_erase(handle->module);
    if (handle->data) PL_erase(handle->data);
    delete handle;
    return TRUE;
}
int rosetta_write_blob(IOSTREAM* stream, atom_t atom, int) {
    const auto* handle = rosetta_handle_of(atom);
    const char* name = "object";
    if (handle && handle->klass < 0) name = "function";
    else if (handle && static_cast<std::size_t>(handle->klass) < rosetta_class_names.size()) name = rosetta_class_names[handle->klass].c_str();
    Sfprintf(stream, "<%s>(%p)", name, static_cast<const void*>(handle));
    return TRUE;
}
// A new blob for a new handle, in `out`.
bool rosetta_new_blob(RosettaHandle* handle, term_t out) {
    if (!PL_unify_blob(out, handle, sizeof(RosettaHandle), &rosetta_blob)) { delete handle; return false; }
    return PL_get_atom(out, &handle->atom);
}
RosettaHandle* rosetta_get_handle(term_t term) {
    void* data = nullptr;
    PL_blob_t* type = nullptr;
    if (!PL_get_blob(term, &data, nullptr, &type) || type != &rosetta_blob) return nullptr;
    return static_cast<RosettaHandle*>(data);
}
unsigned long long rosetta_id_of(RosettaHandle* handle) {
    if (!handle->id) {
        handle->id = ++rosetta_next;
        rosetta_objects[handle->id] = handle;
    }
    return handle->id;
}

// ------------------------------------------------------------ errors
// error(native_error(Kind, Message), _)
bool rosetta_raise(const char* kind, const std::string& message) {
    term_t formal = PL_new_term_ref(), error = PL_new_term_ref(), text = PL_new_term_ref();
    if (!PL_unify_chars(text, PL_STRING | REP_UTF8, message.size(), message.data()) ||
        !PL_unify_term(formal, PL_FUNCTOR_CHARS, "native_error", 2, PL_CHARS, kind, PL_TERM, text) ||
        !PL_unify_term(error, PL_FUNCTOR_CHARS, "error", 2, PL_TERM, formal, PL_VARIABLE))
        return false;
    return PL_raise_exception(error);
}
struct RosettaFailure {   // thrown while encoding: an exception is pending, or to raise
    std::string message;
    bool pending = false;
};
[[noreturn]] void rosetta_fail(const std::string& message) { throw RosettaFailure{message, false}; }
[[noreturn]] void rosetta_pending() { throw RosettaFailure{std::string(), true}; }
void rosetta_check(int ok) { if (!ok) rosetta_pending(); }
std::string rosetta_text(term_t term) {
    char* text = nullptr;
    std::size_t length = 0;
    if (PL_get_nchars(term, &length, &text, CVT_WRITEQ | BUF_STACK | REP_UTF8)) return std::string(text, length);
    return "?";
}

// ------------------------------------------------------------ encoding
struct RosettaWriter {
    std::string out;
    std::vector<unsigned long long>* closures;
    void tag(unsigned char t) { out += static_cast<char>(t); }
    template<class T> void raw(T value) { out.append(reinterpret_cast<const char*>(&value), sizeof value); }
    void text(unsigned char t, const char* data, std::size_t n) { tag(t); raw<unsigned long long>(n); out.append(data, n); }
    void value(term_t term, unsigned depth = 0);
    void closure(term_t goal, bool has_result);
};
atom_t rosetta_atom_true, rosetta_atom_false, rosetta_atom_null, rosetta_atom_minus, rosetta_atom_complex, rosetta_atom_slot,
    rosetta_atom_fn, rosetta_atom_function, rosetta_atom_colon;
void RosettaWriter::closure(term_t goal, bool has_result) {
    // A Prolog closure, or a native function (M:'$rosetta_function'(Blob)) C++ calls back through Prolog.
    const auto id = ++rosetta_next;
    auto& entry = rosetta_closures[id];
    entry.goal = PL_record(goal);
    entry.has_result = has_result;
    entry.count = 1;
    if (closures) closures->push_back(id);
    tag(12);
    raw<unsigned long long>(id);
}
void RosettaWriter::value(term_t term, unsigned depth) {
    if (depth > 10000) rosetta_fail("value nesting is too deep");
    switch (PL_term_type(term)) {
    case PL_VARIABLE:
        PL_instantiation_error(term);
        rosetta_pending();
    case PL_NIL:
        tag(6);
        raw<unsigned long long>(0);
        return;
    case PL_INTEGER: {
        int64_t integer;
        if (PL_get_int64(term, &integer)) { tag(2); raw<long long>(integer); return; }
        char* text = nullptr;
        std::size_t length = 0;
        if (!PL_get_nchars(term, &length, &text, CVT_INTEGER | BUF_STACK)) rosetta_pending();
        this->text(3, text, length);
        return;
    }
    case PL_FLOAT: {
        double real;
        rosetta_check(PL_get_float(term, &real));
        tag(4);
        raw<double>(real);
        return;
    }
    case PL_STRING: {
        char* text = nullptr;
        std::size_t length = 0;
        if (!PL_get_nchars(term, &length, &text, CVT_STRING | BUF_STACK | REP_UTF8)) rosetta_pending();
        this->text(5, text, length);
        return;
    }
    case PL_ATOM: {
        atom_t atom;
        rosetta_check(PL_get_atom(term, &atom));
        if (atom == rosetta_atom_true) { tag(1); return; }
        if (atom == rosetta_atom_false) { tag(19); return; }
        if (atom == rosetta_atom_null) { tag(0); return; }
        char* text = nullptr;
        std::size_t length = 0;
        if (!PL_get_nchars(term, &length, &text, CVT_ATOM | BUF_STACK | REP_UTF8)) rosetta_pending();
        this->text(14, text, length);
        return;
    }
    case PL_BLOB: {
        auto* handle = rosetta_get_handle(term);
        if (!handle) rosetta_fail("cannot pass " + rosetta_text(term) + " to C++");
        tag(10);
        raw<unsigned long long>(reinterpret_cast<std::uintptr_t>(handle->holder));
        raw<unsigned long long>(handle->holder && handle->klass >= 0 ? rosetta_id_of(handle) : 0);
        return;
    }
    case PL_LIST_PAIR: {
        tag(6);
        const auto at = out.size();
        raw<unsigned long long>(0);
        unsigned long long n = 0;
        term_t list = PL_copy_term_ref(term), head = PL_new_term_ref();
        while (PL_get_list(list, head, list)) { value(head, depth + 1); ++n; }
        if (!PL_get_nil(list)) {
            if (PL_is_variable(list)) { PL_instantiation_error(list); rosetta_pending(); }
            rosetta_fail("cannot pass a partial list to C++");
        }
        std::memcpy(&out[at], &n, sizeof n);
        return;
    }
    case PL_TERM: {
        atom_t name;
        std::size_t arity;
        rosetta_check(PL_get_name_arity(term, &name, &arity));
        term_t a = PL_new_term_ref(), b = PL_new_term_ref();
        if (arity == 2) {
            rosetta_check(PL_get_arg(1, term, a));
            rosetta_check(PL_get_arg(2, term, b));
            if (name == rosetta_atom_minus) { tag(8); value(a, depth + 1); value(b, depth + 1); return; }
            if (name == rosetta_atom_complex) {
                double re, im;
                if (!PL_get_float(a, &re)) { int64_t i; if (!PL_get_int64(a, &i)) rosetta_fail("complex/2 needs numbers"); re = static_cast<double>(i); }
                if (!PL_get_float(b, &im)) { int64_t i; if (!PL_get_int64(b, &i)) rosetta_fail("complex/2 needs numbers"); im = static_cast<double>(i); }
                tag(18);
                raw<double>(re);
                raw<double>(im);
                return;
            }
            if (name == rosetta_atom_slot) {
                int slot;
                rosetta_check(PL_get_integer(a, &slot));
                tag(13);
                raw<unsigned>(static_cast<unsigned>(slot));
                value(b, depth + 1);
                return;
            }
            if (name == rosetta_atom_fn) {
                if (PL_is_variable(a)) { PL_instantiation_error(a); rosetta_pending(); }
                atom_t flag = 0;
                rosetta_check(PL_get_atom(b, &flag));
                closure(a, flag == rosetta_atom_true);
                return;
            }
        }
        rosetta_fail("cannot pass " + rosetta_text(term) + " to C++");
    }
    default:
        rosetta_fail("cannot pass " + rosetta_text(term) + " to C++");
    }
}

)cpp",
R"cpp(// ------------------------------------------------------------ decoding
struct RosettaReader {
    const unsigned char* at;
    const unsigned char* end;
    term_t arguments = 0;   // the call's arguments (argument references, parents)
    std::size_t count = 0;
    void need(std::size_t n) const { if (static_cast<std::size_t>(end - at) < n) rosetta_fail("malformed native value"); }
    template<class T> T raw() { need(sizeof(T)); T value; std::memcpy(&value, at, sizeof(T)); at += sizeof(T); return value; }
    std::string text() {
        const auto n = raw<unsigned long long>();
        need(static_cast<std::size_t>(n));
        std::string value(reinterpret_cast<const char*>(at), static_cast<std::size_t>(n));
        at += n;
        return value;
    }
    void value(term_t out, unsigned depth = 0);
    void list(term_t out, unsigned long long n, bool reals, unsigned depth);
    void unify(bool ok) { if (!ok) rosetta_pending(); }
};
void RosettaReader::list(term_t out, unsigned long long n, bool reals, unsigned depth) {
    term_t tail = PL_copy_term_ref(out), head = PL_new_term_ref();
    for (unsigned long long i = 0; i < n; ++i) {
        unify(PL_unify_list(tail, head, tail));
        if (reals) unify(PL_unify_float(head, raw<double>()));
        else value(head, depth + 1);
    }
    unify(PL_unify_nil(tail));
}
void RosettaReader::value(term_t out, unsigned depth) {
    if (depth > 10000) rosetta_fail("value nesting is too deep");
    need(1);
    const unsigned char tag = *at++;
    switch (tag) {
    case 0: unify(PL_unify_atom(out, rosetta_atom_null)); return;
    case 1: unify(PL_unify_atom(out, rosetta_atom_true)); return;
    case 19: unify(PL_unify_atom(out, rosetta_atom_false)); return;
    case 2: unify(PL_unify_int64(out, raw<long long>())); return;
    case 4: unify(PL_unify_float(out, raw<double>())); return;
    case 3: {
        const auto digits = text();
        term_t number = PL_new_term_ref();
        unify(PL_put_term_from_chars(number, REP_UTF8, digits.size(), digits.c_str()) && PL_unify(out, number));
        return;
    }
    case 5: { const auto s = text(); unify(PL_unify_chars(out, PL_STRING | REP_UTF8, s.size(), s.data())); return; }
    case 14: { const auto s = text(); unify(PL_unify_chars(out, PL_ATOM | REP_UTF8, s.size(), s.data())); return; }
    case 15: {
        const auto s = text();
        term_t tail = PL_copy_term_ref(out), head = PL_new_term_ref();
        for (const char c : s) {
            unify(PL_unify_list(tail, head, tail));
            unify(PL_unify_integer(head, static_cast<unsigned char>(c)));
        }
        unify(PL_unify_nil(tail));
        return;
    }
    case 18: {
        const double re = raw<double>(), im = raw<double>();
        unify(PL_unify_term(out, PL_FUNCTOR, PL_new_functor(rosetta_atom_complex, 2), PL_FLOAT, re, PL_FLOAT, im));
        return;
    }
    case 6: list(out, raw<unsigned long long>(), false, depth); return;
    case 7: list(out, raw<unsigned long long>(), true, depth); return;
    case 16: {
        const auto n = raw<unsigned long long>();
        if (n != 2) { list(out, n, false, depth); return; }
        [[fallthrough]];
    }
    case 8: {
        term_t a = PL_new_term_ref(), b = PL_new_term_ref();
        value(a, depth + 1);
        value(b, depth + 1);
        unify(PL_unify_term(out, PL_FUNCTOR, PL_new_functor(rosetta_atom_minus, 2), PL_TERM, a, PL_TERM, b));
        return;
    }
    case 9: {
        const auto n = raw<unsigned long long>();
        term_t tail = PL_copy_term_ref(out), head = PL_new_term_ref();
        for (unsigned long long i = 0; i < n; ++i) {
            term_t a = PL_new_term_ref(), b = PL_new_term_ref();
            value(a, depth + 1);
            value(b, depth + 1);
            unify(PL_unify_list(tail, head, tail));
            unify(PL_unify_term(head, PL_FUNCTOR, PL_new_functor(rosetta_atom_minus, 2), PL_TERM, a, PL_TERM, b));
        }
        unify(PL_unify_nil(tail));
        return;
    }
    case 10: {
        const auto holder = raw<unsigned long long>();
        const auto klass = raw<unsigned>();
        const auto parent = raw<long long>();
        auto* handle = new RosettaHandle;
        handle->holder = reinterpret_cast<void*>(static_cast<std::uintptr_t>(holder));
        handle->klass = static_cast<long>(klass);
        if (parent >= 0 && static_cast<std::size_t>(parent) < count) {
            if (auto* owner = rosetta_get_handle(arguments + static_cast<std::size_t>(parent))) {
                handle->parent = owner->atom;
                PL_register_atom(handle->parent);
            }
        }
        term_t blob = PL_new_term_ref();
        unify(rosetta_new_blob(handle, blob) && PL_unify(out, blob));
        return;
    }
    case 11: {
        const auto id = raw<unsigned long long>();
        const auto object = rosetta_objects.find(id);
        if (object != rosetta_objects.end()) { unify(PL_unify_atom(out, object->second->atom)); return; }
        const auto closure = rosetta_closures.find(id);
        if (closure != rosetta_closures.end()) {
            term_t goal = PL_new_term_ref();
            unify(PL_recorded(closure->second.goal, goal) && PL_unify(out, goal));
            return;
        }
        rosetta_fail("the Prolog value of a native reference is gone");
    }
    case 12: {
        auto* handle = new RosettaHandle;
        handle->holder = reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw<unsigned long long>()));
        handle->klass = -1;
        term_t blob = PL_new_term_ref();
        unify(rosetta_new_blob(handle, blob));
        unify(PL_unify_term(out, PL_FUNCTOR, PL_new_functor(rosetta_atom_colon, 2), PL_CHARS, "@M@",
                            PL_FUNCTOR, PL_new_functor(rosetta_atom_function, 1), PL_TERM, blob));
        return;
    }
    case 17: {
        const auto index = raw<unsigned>();
        if (index >= count) rosetta_fail("malformed native value (argument reference)");
        unify(PL_unify(out, arguments + index));
        return;
    }
    default: rosetta_fail("malformed native value (tag " + std::to_string(tag) + ")");
    }
}

)cpp",
R"cpp(// ------------------------------------------------------------ calls into C++
void rosetta_adjust(unsigned long long id, int delta);
std::string rosetta_kind(std::string kind) {
    for (auto& c : kind) if (c == '-') c = '_';
    return kind.empty() || kind == "unknown" ? "native" : kind;
}
// Calls native function `index` with the arguments in the list `args`, unifies `result` (and
// `writebacks` with Slot-Value pairs). `prefix`: an argument encoded first (constructors).
foreign_t rosetta_invoke_native(term_t index_term, term_t args, term_t result, term_t writebacks, const std::string* prefix,
                                std::size_t prefix_count, RosettaHandle* director, term_t director_out) {
    unsigned index = 0;
    {
        int64_t i;
        if (!PL_get_int64(index_term, &i)) return PL_type_error("integer", index_term);
        index = static_cast<unsigned>(i);
    }
    std::vector<unsigned long long> closures;
    std::size_t count = 0;
    {
        term_t list = PL_copy_term_ref(args), head = PL_new_term_ref();
        while (PL_get_list(list, head, list)) ++count;
    }
    term_t arguments = PL_new_term_refs(count ? count : 1);
    RosettaWriter writer;
    writer.closures = &closures;
    unsigned char* out = nullptr;
    std::size_t out_size = 0;
    int status;
    try {
        writer.raw<unsigned>(static_cast<unsigned>(count + prefix_count));
        if (prefix) writer.out += *prefix;
        term_t list = PL_copy_term_ref(args);
        for (std::size_t i = 0; i < count; ++i) {
            rosetta_check(PL_get_list(list, arguments + i, list));
            writer.value(arguments + i);
        }
        status = @C@_call(index, reinterpret_cast<const unsigned char*>(writer.out.data()), writer.out.size(), &out, &out_size);
    } catch (const RosettaFailure& failure) {
        for (const auto id : closures) rosetta_adjust(id, -1);
        return failure.pending ? FALSE : rosetta_raise("binding", failure.message);
    }
    // Callbacks passed in are dropped unless C++ retained them.
    for (const auto id : closures) rosetta_adjust(id, -1);
    struct Free { unsigned char* p; ~Free() { if (p) @C@_free(p); } } guard{out};
    RosettaReader reader{out, out + out_size, arguments, count};
    try {
        if (status == 0) {
            if (director) {
                // The C++ object of an extended object: its holder joins the handle.
                if (reader.raw<unsigned char>() != 10) rosetta_fail("C++ constructor returned no object");
                director->holder = reinterpret_cast<void*>(static_cast<std::uintptr_t>(reader.raw<unsigned long long>()));
                director->klass = static_cast<long>(reader.raw<unsigned>());
                reader.raw<long long>();
                return PL_unify_atom(director_out, director->atom);
            }
            term_t value = PL_new_term_ref();
            reader.value(value);
            if (!PL_unify(result, value)) return FALSE;
            if (!writebacks) return TRUE;
            term_t tail = PL_copy_term_ref(writebacks), head = PL_new_term_ref();
            if (reader.at + 4 <= reader.end) {
                const auto n = reader.raw<unsigned>();
                for (unsigned i = 0; i < n; ++i) {
                    const auto slot = reader.raw<unsigned>();
                    term_t item = PL_new_term_ref();
                    reader.value(item);
                    if (!PL_unify_list(tail, head, tail) ||
                        !PL_unify_term(head, PL_FUNCTOR, PL_new_functor(rosetta_atom_minus, 2), PL_INT, static_cast<int>(slot), PL_TERM, item))
                        return FALSE;
                }
            }
            return PL_unify_nil(tail);
        }
        if (status == 1 || status == 2) {
            std::string kind, message;
            // The kind (a symbol) and the message (a string).
            if (reader.at < reader.end && (*reader.at == 5 || *reader.at == 14)) { ++reader.at; kind = reader.text(); }
            if (reader.at < reader.end && (*reader.at == 5 || *reader.at == 14)) { ++reader.at; message = reader.text(); }
            if (status == 2) {
                const auto at = message.rfind(rosetta_marker);
                if (at != std::string::npos && !message.empty() && message.back() == ']') {
                    const auto start = at + std::strlen(rosetta_marker);
                    const auto id = std::strtoull(message.substr(start, message.size() - 1 - start).c_str(), nullptr, 10);
                    const auto found = rosetta_causes.find(id);
                    if (found != rosetta_causes.end()) {
                        term_t cause = PL_new_term_ref();
                        const bool ok = PL_recorded(found->second, cause);
                        PL_erase(found->second);
                        rosetta_causes.erase(found);
                        if (ok) return PL_raise_exception(cause);
                    }
                    message.erase(at);
                }
                return rosetta_raise("callback", message);
            }
            return rosetta_raise(rosetta_kind(kind).c_str(), message);
        }
        return rosetta_raise("allocation", "out of memory in a native call");
    } catch (const RosettaFailure& failure) {
        return failure.pending ? FALSE : rosetta_raise("binding", failure.message);
    }
}
foreign_t rosetta_pl_call3(term_t index, term_t args, term_t result) {
    return rosetta_invoke_native(index, args, result, 0, nullptr, 0, nullptr, 0);
}
foreign_t rosetta_pl_call4(term_t index, term_t args, term_t result, term_t writebacks) {
    return rosetta_invoke_native(index, args, result, writebacks, nullptr, 0, nullptr, 0);
}
// '$rosetta_construct'(+Index, +Args, +Module, +Data, -Object): Module [] for a plain C++ object,
// else an extended object whose virtual methods Module's override/1 implements.
foreign_t rosetta_pl_construct(term_t index, term_t args, term_t module, term_t data, term_t object) {
    std::string prefix;
    prefix += static_cast<char>(2);
    if (PL_get_nil(module)) {
        const long long zero = 0;
        prefix.append(reinterpret_cast<const char*>(&zero), sizeof zero);
        return rosetta_invoke_native(index, args, object, 0, &prefix, 1, nullptr, 0);
    }
    atom_t name;
    if (!PL_get_atom(module, &name)) return PL_type_error("atom", module);
    auto* handle = new RosettaHandle;
    term_t blob = PL_new_term_ref();
    if (!rosetta_new_blob(handle, blob)) return FALSE;
    handle->module = PL_record(module);
    handle->data = PL_record(data);
    const long long id = static_cast<long long>(rosetta_id_of(handle));
    prefix.append(reinterpret_cast<const char*>(&id), sizeof id);
    return rosetta_invoke_native(index, args, object, 0, &prefix, 1, handle, object);
}

// ------------------------------------------------------------ objects
RosettaHandle* rosetta_object(term_t term, bool* ok) {
    auto* handle = rosetta_get_handle(term);
    *ok = handle != nullptr;
    if (!handle) PL_type_error("@M@_object", term);
    return handle;
}
foreign_t rosetta_pl_close(term_t term) {
    bool ok;
    auto* handle = rosetta_object(term, &ok);
    if (!ok) return FALSE;
    if (void* holder = handle->holder) {
        handle->holder = nullptr;
        @C@_release(holder, 1);
    }
    return TRUE;
}
foreign_t rosetta_pl_closed(term_t term) {
    bool ok;
    auto* handle = rosetta_object(term, &ok);
    return ok && !handle->holder;
}
foreign_t rosetta_pl_class_of(term_t term, term_t index) {
    bool ok;
    auto* handle = rosetta_object(term, &ok);
    return ok && handle->klass >= 0 && PL_unify_integer(index, handle->klass);
}
// '$rosetta_director'(+Object, -Id, -Module): an extended object's id and module.
foreign_t rosetta_pl_director(term_t term, term_t id, term_t module) {
    auto* handle = rosetta_get_handle(term);
    if (!handle || !handle->module) return FALSE;
    term_t value = PL_new_term_ref();
    return PL_recorded(handle->module, value) && PL_unify(module, value) && PL_unify_uint64(id, handle->id);
}
foreign_t rosetta_pl_data(term_t term, term_t data) {
    bool ok;
    auto* handle = rosetta_object(term, &ok);
    if (!ok) return FALSE;
    if (!handle->data) return rosetta_raise("binding", "the object is not an extended object: it has no data");
    term_t value = PL_new_term_ref();
    return PL_recorded(handle->data, value) && PL_unify(data, value);
}
foreign_t rosetta_pl_set_data(term_t term, term_t data) {
    bool ok;
    auto* handle = rosetta_object(term, &ok);
    if (!ok) return FALSE;
    if (!handle->data) return rosetta_raise("binding", "the object is not an extended object: it has no data");
    PL_erase(handle->data);
    handle->data = PL_record(data);
    return TRUE;
}

)cpp",
R"cpp(// ------------------------------------------------------------ calls from C++
int rosetta_message(const std::string& text, unsigned char** result, std::size_t* result_size) {
    const unsigned long long n = text.size();
    auto* out = static_cast<unsigned char*>(@C@_alloc(1 + sizeof n + n));
    if (!out) return 3;
    out[0] = 5;
    std::memcpy(out + 1, &n, sizeof n);
    std::memcpy(out + 1 + sizeof n, text.data(), n);
    *result = out;
    *result_size = 1 + sizeof n + n;
    return 2;
}
// The message of a Prolog exception, with a marker C++ carries back (the call that reached C++
// raises the original exception).
std::string rosetta_save_cause(term_t exception) {
    std::string message;
    term_t formal = PL_new_term_ref(), text = PL_new_term_ref();
    atom_t name;
    std::size_t arity;
    if (PL_is_functor(exception, PL_new_functor(PL_new_atom("error"), 2)) && PL_get_arg(1, exception, formal) &&
        PL_get_name_arity(formal, &name, &arity) && arity == 2 && std::strcmp(PL_atom_chars(name), "native_error") == 0 &&
        PL_get_arg(2, formal, text) && PL_is_string(text)) {
        char* chars = nullptr;
        std::size_t length = 0;
        if (PL_get_nchars(text, &length, &chars, CVT_STRING | BUF_STACK | REP_UTF8)) message.assign(chars, length);
    } else {
        message = rosetta_text(exception);
    }
    const auto id = ++rosetta_next;
    rosetta_causes[id] = PL_record(exception);
    return message + rosetta_marker + std::to_string(id) + "]";
}
int rosetta_invoke(int kind, unsigned long long id, unsigned generic, const unsigned char* data, std::size_t size,
                   unsigned char** result, std::size_t* result_size) {
    *result = nullptr;
    *result_size = 0;
    if (PL_thread_self() < 0) return rosetta_message("C++ called back into Prolog from a thread without a Prolog engine", result, result_size);
    fid_t frame = PL_open_foreign_frame();
    int status = 0;
    std::string message;
    try {
        RosettaReader reader{data, data + size};
        const auto n = size >= 4 ? reader.raw<unsigned>() : 0u;
        term_t list = PL_new_term_ref(), tail = PL_copy_term_ref(list), head = PL_new_term_ref();
        for (unsigned i = 0; i < n; ++i) {
            term_t item = PL_new_term_ref();
            reader.value(item);
            if (!PL_unify_list(tail, head, tail) || !PL_unify(head, item)) rosetta_pending();
        }
        if (!PL_unify_nil(tail)) rosetta_pending();
        term_t call = PL_new_term_refs(4);
        predicate_t predicate;
        if (kind == 0) {
            const auto found = rosetta_closures.find(id);
            if (found == rosetta_closures.end()) rosetta_fail("the Prolog callback of a native reference is gone");
            if (!PL_recorded(found->second.goal, call) || !PL_unify(call + 1, list) ||
                !PL_unify_atom(call + 2, found->second.has_result ? rosetta_atom_true : rosetta_atom_false))
                rosetta_pending();
            predicate = PL_predicate("$rosetta_callback", 4, "@M@");
        } else {
            if (!PL_unify_integer(call, generic) || !PL_unify(call + 1, list)) rosetta_pending();
            predicate = PL_predicate("$rosetta_virtual_call", 3, "@M@");
        }
        term_t value = kind == 0 ? call + 3 : call + 2;
        qid_t query = PL_open_query(nullptr, PL_Q_CATCH_EXCEPTION | PL_Q_NODEBUG, predicate, call);
        const int found = PL_next_solution(query);
        if (!found) {
            if (term_t exception = PL_exception(query)) message = rosetta_save_cause(exception);
            else message = kind == 0 ? "the Prolog callback failed" : "the Prolog override failed";
            PL_cut_query(query);
            status = 2;
        } else {
            PL_cut_query(query);
            RosettaWriter writer;
            writer.closures = nullptr;   // closures in a result stay until C++ retains or drops them
            writer.value(value);
            auto* out = static_cast<unsigned char*>(@C@_alloc(writer.out.size() ? writer.out.size() : 1));
            if (!out) status = 3;
            else {
                std::memcpy(out, writer.out.data(), writer.out.size());
                *result = out;
                *result_size = writer.out.size();
            }
        }
    } catch (const RosettaFailure& failure) {
        status = 2;
        if (failure.pending) {
            term_t exception = PL_exception(0);
            message = exception ? rosetta_save_cause(exception) : "the Prolog callback failed";
            PL_clear_exception();
        } else {
            message = failure.message;
        }
    }
    PL_discard_foreign_frame(frame);
    if (status == 2) return rosetta_message(message, result, result_size);
    return status;
}
void rosetta_adjust(unsigned long long id, int delta) {
    const auto closure = rosetta_closures.find(id);
    if (closure != rosetta_closures.end()) {
        if ((closure->second.count += delta) <= 0) {
            PL_erase(closure->second.goal);
            rosetta_closures.erase(closure);
        }
        return;
    }
    const auto object = rosetta_objects.find(id);
    if (object == rosetta_objects.end()) return;
    auto* handle = object->second;
    for (; delta > 0; --delta, ++handle->strong) PL_register_atom(handle->atom);
    for (; delta < 0 && handle->strong > 0; ++delta, --handle->strong) PL_unregister_atom(handle->atom);
}
void rosetta_retain(unsigned long long id, int delta) {
    if (rosetta_halting) return;
    rosetta_adjust(id, delta);
}
int rosetta_on_halt(int, void*) {
    rosetta_halting = true;
    return 0;
}
// '$rosetta_init'(+ClassNames)
foreign_t rosetta_pl_init(term_t names) {
    rosetta_class_names.clear();
    term_t list = PL_copy_term_ref(names), head = PL_new_term_ref();
    while (PL_get_list(list, head, list)) {
        char* text = nullptr;
        if (!PL_get_chars(head, &text, CVT_ATOM | BUF_STACK)) return PL_type_error("atom", head);
        rosetta_class_names.emplace_back(text);
    }
    if (@C@_init(1, rosetta_invoke, rosetta_retain) != 0)
        return rosetta_raise("binding", "the native library was generated by a different Rosetta version");
    return TRUE;
}
} // namespace

ROSETTA_CABI_EXPORT install_t @C@_prolog_install(void) {
    rosetta_atom_true = PL_new_atom("true");
    rosetta_atom_false = PL_new_atom("false");
    rosetta_atom_null = PL_new_atom("null");
    rosetta_atom_minus = PL_new_atom("-");
    rosetta_atom_complex = PL_new_atom("complex");
    rosetta_atom_slot = PL_new_atom("$rosetta_slot");
    rosetta_atom_fn = PL_new_atom("$rosetta_fn");
    rosetta_atom_function = PL_new_atom("$rosetta_function");
    rosetta_atom_colon = PL_new_atom(":");
    const char* module = "@M@";
    PL_register_foreign_in_module(module, "$rosetta_call", 3, reinterpret_cast<pl_function_t>(rosetta_pl_call3), 0);
    PL_register_foreign_in_module(module, "$rosetta_call", 4, reinterpret_cast<pl_function_t>(rosetta_pl_call4), 0);
    PL_register_foreign_in_module(module, "$rosetta_construct", 5, reinterpret_cast<pl_function_t>(rosetta_pl_construct), 0);
    PL_register_foreign_in_module(module, "$rosetta_close", 1, reinterpret_cast<pl_function_t>(rosetta_pl_close), 0);
    PL_register_foreign_in_module(module, "$rosetta_closed", 1, reinterpret_cast<pl_function_t>(rosetta_pl_closed), 0);
    PL_register_foreign_in_module(module, "$rosetta_class_of", 2, reinterpret_cast<pl_function_t>(rosetta_pl_class_of), 0);
    PL_register_foreign_in_module(module, "$rosetta_director", 3, reinterpret_cast<pl_function_t>(rosetta_pl_director), 0);
    PL_register_foreign_in_module(module, "$rosetta_data", 2, reinterpret_cast<pl_function_t>(rosetta_pl_data), 0);
    PL_register_foreign_in_module(module, "$rosetta_set_data", 2, reinterpret_cast<pl_function_t>(rosetta_pl_set_data), 0);
    PL_register_foreign_in_module(module, "$rosetta_init", 1, reinterpret_cast<pl_function_t>(rosetta_pl_init), 0);
    PL_on_halt(rosetta_on_halt, nullptr);
}


)cpp"
};
inline constexpr std::size_t prolog_full_glue_count = 4;
inline constexpr const char* prolog_full_runtime_parts[] = {
R"pl(
% ---------------------------------------------------------------- runtime
% Loads the native library (whose foreign glue registers the '$rosetta_' predicates), and runs
% the Prolog callbacks and overrides C++ calls back.

% member/2 without library(lists); defined before the directive below that loads the library uses it.
'$rosetta_member'(Item, [Item|_]).
'$rosetta_member'(Item, [_|Tail]) :-
    '$rosetta_member'(Item, Tail).

% The library: @ENV@ (a path), else beside this file, in lib/ or ../lib, else on the foreign
% search path.
'$rosetta_library'(Path) :-
    getenv('@ENV@', Path),
    Path \== '',
    !.
'$rosetta_library'(Path) :-
    prolog_load_context(directory, Directory),
    current_prolog_flag(shared_object_extension, Extension),
    file_name_extension('lib@LIB@', Extension, File),
    '$rosetta_member'(Sub, ['', 'lib/', '../lib/']),
    atomic_list_concat([Directory, '/', Sub, File], Path),
    exists_file(Path),
    !.
'$rosetta_library'(foreign('lib@LIB@')).

:- '$rosetta_library'(Path), load_foreign_library(Path, '@C@_prolog_install').

:- multifile prolog:error_message//1.
prolog:error_message(native_error(Kind, Message)) -->
    [ '~w (~w)'-[Message, Kind] ].

%!  close_object(+Object) is det.
%
%   Releases the C++ object of Object now (again does nothing); later calls raise
%   error(native_error(binding, _), _). Objects are otherwise released when atom garbage
%   collection reclaims them.
close_object(Object) :-
    '$rosetta_close'(Object).

%!  object_closed(+Object) is semidet.
%
%   True when the C++ object of Object is released.
object_closed(Object) :-
    '$rosetta_closed'(Object).

%!  object_class(+Object, -Class) is det.
%
%   Class is the class of Object (most derived: a C++ function declared to return a base class
%   may return a derived object), as the prefix of its predicates (circle).
object_class(Object, Class) :-
    '$rosetta_class_of'(Object, Index),
    '$rosetta_class'(Index, Class, _).

%!  object_is_a(+Object, +Class) is semidet.
%
%   True when Object is a Class or a class derived from it.
object_is_a(Object, Class) :-
    '$rosetta_class_of'(Object, Index),
    '$rosetta_class'(To, Class, _),
    '$rosetta_derives'(Index, To),
    !.

%!  object_data(+Object, -Data) is det.
%
%   The data of an extended object (created by a CLASS_extend predicate).
object_data(Object, Data) :-
    '$rosetta_data'(Object, Data).

%!  object_set_data(+Object, +Data) is det.
%
%   Replaces the data of an extended object (not undone on backtracking).
object_set_data(Object, Data) :-
    '$rosetta_set_data'(Object, Data).

'$rosetta_derives'(Class, Class) :-
    !.
'$rosetta_derives'(From, To) :-
    '$rosetta_class'(From, _, Bases),
    '$rosetta_member'(Base, Bases),
    '$rosetta_derives'(Base, To),
    !.

% The overrides running now (Id-Method): calling the method's predicate again inside its own
% override runs the C++ implementation.
'$rosetta_active'(Active) :-
    (   nb_current('$rosetta_active', Active0)
    ->  Active = Active0
    ;   Active = []
    ).

'$rosetta_override'(Self, Method, Arguments, Id-Method, Module:override(Head)) :-
    '$rosetta_director'(Self, Id, Module),
    '$rosetta_active'(Active),
    \+ memberchk(Id-Method, Active),
    Head =.. [Method|Arguments],
    '$rosetta_overrides'(Module, Head).

'$rosetta_overrides'(Module, Head) :-
    functor(Head, Name, Arity),
    functor(Template, Name, Arity),
    current_predicate(Module:override/1),
    catch(clause(Module:override(Template), _), _, fail),
    !.

'$rosetta_run'(Key, Goal) :-
    '$rosetta_active'(Old),
    b_setval('$rosetta_active', [Key|Old]),
    once(Goal),
    b_setval('$rosetta_active', Old).

% A virtual method's predicate: the override of an extended object, else the C++ method.
'$rosetta_dispatch'(Self, Method, Arguments, Native) :-
    (   '$rosetta_override'(Self, Method, Arguments, Key, Goal)
    ->  '$rosetta_run'(Key, Goal)
    ;   call(Native)
    ).

% C++ calls a virtual method of an extended object.
'$rosetta_virtual_call'(Generic, [Self|Arguments], Result) :-
    '$rosetta_virtual'(Generic, Method, HasResult),
    (   HasResult == true
    ->  '$rosetta_append'([Self|Arguments], [Result], All)
    ;   All = [Self|Arguments],
        Result = null
    ),
    (   '$rosetta_override'(Self, Method, All, Key, Goal)
    ->  '$rosetta_run'(Key, Goal)
    ;   '$rosetta_class_of'(Self, Class),
        '$rosetta_native'(Class, Generic, Predicate)
    ->  Native =.. [Predicate|All],
        once(Native)
    ;   throw(error(native_error(binding, "no Prolog predicate for this C++ virtual method"), _))
    ).

% C++ calls a Prolog callback.
'$rosetta_callback'(Goal, Arguments, HasResult, Result) :-
    (   HasResult == true
    ->  '$rosetta_append'(Arguments, [Result], All)
    ;   All = Arguments,
        Result = null
    ),
    Call =.. [call, Goal|All],
    once(Call).

% A C++ function: call(F, Arguments..., Result).
'$rosetta_function'(F, R) :- '$rosetta_call'(0, [F], R).
'$rosetta_function'(F, A, R) :- '$rosetta_call'(0, [F, A], R).
'$rosetta_function'(F, A, B, R) :- '$rosetta_call'(0, [F, A, B], R).
'$rosetta_function'(F, A, B, C, R) :- '$rosetta_call'(0, [F, A, B, C], R).
'$rosetta_function'(F, A, B, C, D, R) :- '$rosetta_call'(0, [F, A, B, C, D], R).
'$rosetta_function'(F, A, B, C, D, E, R) :- '$rosetta_call'(0, [F, A, B, C, D, E], R).
'$rosetta_function'(F, A, B, C, D, E, G, R) :- '$rosetta_call'(0, [F, A, B, C, D, E, G], R).
'$rosetta_function'(F, A, B, C, D, E, G, H, R) :- '$rosetta_call'(0, [F, A, B, C, D, E, G, H], R).

'$rosetta_append'([], List, List).
'$rosetta_append'([Head|Tail], List, [Head|Rest]) :-
    '$rosetta_append'(Tail, List, Rest).


% What C++ wrote back to the reference parameter in Slot, else the value passed.
'$rosetta_out'(Writebacks, Slot, In, Out) :-
    (   memberchk(Slot-Value, Writebacks)
    ->  Out = Value
    ;   Out = In
    ).


)pl"
};
inline constexpr std::size_t prolog_full_runtime_count = 1;
// SWI-Prolog 9 system predicates (name/arity, one per line): a generated predicate may not redefine them.
inline constexpr const char* prolog_system_predicates = R"pl(abolish/1 abolish/2 abort/0 absolute_file_name/3 access_file/2 acyclic_term/1 add_import_module/3 answer_count_restraint/0 append/1 arg/3 assert/1 assert/2 asserta/1 asserta/2 assertz/1 assertz/2 at_end_of_stream/0 at_end_of_stream/1 at_halt/1 atom/1 atom_chars/2 atom_codes/2 atom_concat/3 atom_length/2 atom_number/2 atom_string/2 atom_to_term/3 atomic/1 atomic_concat/3 atomic_list_concat/2 atomic_list_concat/3 atomics_to_string/2 atomics_to_string/3 attvar/1 b_getval/2 b_set_dict/3 b_setval/2 between/3 blob/2 bounded_number/3 break/0 byte_count/2 call/1 call/2 call/3 call/4 call/5 call/6 call/7 call/8 call_cleanup/2 call_continuation/1 call_shared_object_function/2 callable/1 cancel_halt/1 catch/3 catch_with_backtrace/3 char_code/2 char_conversion/2 char_type/2 character_count/2 clause/2 clause/3 close/1 close/2 close_shared_object/1 code_type/2 collation_key/2 compare/3 compile_aux_clauses/1 compile_predicates/1 compiling/0 compound/1 compound_name_arguments/3 compound_name_arity/3 consult/1 context_module/1 copy_predicate_clauses/2 copy_stream_data/2 copy_stream_data/3 copy_term/2 copy_term/4 copy_term_nat/2 copy_term_nat/4 create_prolog_flag/3 current_arithmetic_function/1 current_atom/1 current_blob/2 current_char_conversion/2 current_flag/1 current_format_predicate/2 current_functor/2 current_input/1 current_key/1 current_locale/1 current_op/3 current_output/1 current_predicate/1 current_prolog_flag/2 current_transaction/1 cyclic_term/1 date_time_stamp/2 default_module/2 del_attr/2 del_attrs/1 del_dict/4 delete_directory/1 delete_file/1 delete_import_module/2 det/1 deterministic/1 dict_create/3 dict_pairs/3 directory_files/2 discontiguous/1 divmod/4 downcase_atom/2 duplicate_term/2 dwim_match/3 dynamic/1 engine_destroy/1 engine_fetch/1 engine_next/2 engine_post/2 engine_post/3 ensure_loaded/1 erase/1 exists_directory/1 exists_file/1 expand_file_name/2 expand_file_search_path/2 export/1 fail/0 false/0 fast_read/2 fast_term_serialized/2 fast_write/2 file_base_name/2 file_directory_name/2 file_name_extension/3 fill_buffer/1 float/1 float_class/2 float_parts/4 flush_output/0 flush_output/1 format/2 format/3 format_predicate/2 format_time/3 format_time/4 functor/3 functor/4 garbage_collect_atoms/0 garbage_collect_clauses/0 gc_file_search_cache/1 get/1 get/2 get0/1 get0/2 get_attr/3 get_attrs/2 get_byte/1 get_byte/2 get_char/1 get_char/2 get_code/1 get_code/2 get_dict/3 get_dict/5 get_flag/2 get_single_char/1 get_string_code/3 get_time/1 getenv/2 goal_expansion/2 goal_expansion/4 ground/1 halt/0 halt/1 ignore/1 import/1 import_module/2 initialization/2 instance/2 integer/1 is/2 is_absolute_file_name/1 is_dict/1 is_dict/2 is_engine/1 is_list/1 is_message_queue/1 is_most_general_term/1 is_stream/1 is_thread/1 is_trie/1 keysort/2 length/2 line_count/2 line_position/2 load_files/1 load_files/2 locale_create/3 locale_destroy/1 locale_property/2 make_directory/1 malloc_property/1 memberchk/2 message_queue_create/1 message_queue_create/2 message_queue_destroy/1 message_queue_property/2 message_queue_set/2 meta_predicate/1 module_transparent/1 msort/2 multifile/1 mutex_create/1 mutex_create/2 mutex_destroy/1 mutex_lock/1 mutex_property/2 mutex_statistics/0 mutex_trylock/1 mutex_unlock/1 mutex_unlock_all/0 name/2 nb_current/2 nb_delete/1 nb_getval/2 nb_link_dict/3 nb_linkarg/3 nb_linkval/2 nb_set_dict/3 nb_setarg/3 nl/0 nl/1 non_terminal/1 nonground/2 nonvar/1 noprofile/1 noprotocol/0 normalize_space/2 not/1 notrace/0 notrace/1 nth_clause/3 nth_integer_root_and_remainder/4 number/1 number_chars/2 number_codes/2 number_string/2 numbervars/4 once/1 op/3 open/3 open/4 open_null_stream/1 open_shared_object/2 open_shared_object/3 open_string/2 peek_byte/1 peek_byte/2 peek_char/1 peek_char/2 peek_code/1 peek_code/2 peek_string/3 plus/3 predicate_option_mode/2 predicate_option_type/2 print/1 print/2 profiler/2 prolog_alert_signal/2 prolog_choice_attribute/3 prolog_current_choice/1 prolog_current_frame/1 prolog_cut_to/1 prolog_debug/1 prolog_frame_attribute/3 prolog_interrupt/0 prolog_listen/2 prolog_listen/3 prolog_nodebug/1 prolog_skip_frame/1 prolog_skip_level/2 prolog_to_os_filename/2 prolog_unlisten/2 prompt/2 prompt1/1 protocol/1 protocola/1 protocolling/1 public/1 put/1 put/2 put_attr/3 put_attrs/2 put_byte/1 put_byte/2 put_char/1 put_char/2 put_code/1 put_code/2 put_dict/3 put_dict/4 radial_restraint/0 random_property/1 rational/1 rational/3 read/1 read/2 read_clause/3 read_link/3 read_pending_chars/3 read_pending_codes/3 read_string/3 read_string/5 read_term/2 read_term/3 read_term_from_atom/3 recorda/2 recorda/3 recorded/2 recorded/3 recordz/2 recordz/3 redefine_system_predicate/1 reexport/1 reexport/2 rename_file/2 repeat/0 reset/3 reset_profiler/0 retract/1 retractall/1 same_file/2 same_term/2 see/1 seeing/1 seek/4 seen/0 select_dict/3 set_end_of_stream/1 set_flag/2 set_input/1 set_locale/1 set_malloc/1 set_module/1 set_output/1 set_prolog_IO/3 set_prolog_flag/2 set_random/1 set_stream/2 set_stream_position/2 set_system_IO/3 setarg/3 setenv/2 setlocale/3 setup_call_catcher_cleanup/4 setup_call_cleanup/3 shell/2 shift/1 shift_for_copy/1 sig_atomic/1 sig_pending/1 sig_remove/2 size_abstract_term/3 size_file/2 skip/1 skip/2 sleep/1 sort/2 sort/4 source_location/2 split_string/4 stamp_date_time/3 statistics/2 stream_pair/3 stream_property/2 string/1 string_bytes/3 string_chars/2 string_code/3 string_codes/2 string_concat/3 string_length/2 string_lower/2 string_upper/2 strip_module/3 sub_atom/5 sub_atom_icasechk/3 sub_string/5 subsumes_term/2 succ/2 tab/1 tab/2 tabled_call/1 tell/1 telling/1 term_attvars/2 term_expansion/2 term_expansion/4 term_hash/2 term_hash/4 term_singletons/2 term_string/2 term_to_atom/2 term_variables/2 term_variables/3 text_to_string/2 thread_affinity/3 thread_alias/1 thread_create/3 thread_detach/1 thread_exit/1 thread_get_message/1 thread_get_message/2 thread_get_message/3 thread_idle/2 thread_join/2 thread_local/1 thread_peek_message/1 thread_peek_message/2 thread_property/2 thread_self/1 thread_send_message/2 thread_send_message/3 thread_setconcurrency/2 thread_signal/2 thread_statistics/3 thread_update/2 thread_wait/2 throw/1 time_file/2 tmp_file/2 told/0 trace/0 tracing/0 transaction_updates/1 trie_delete/3 trie_destroy/1 trie_gen/2 trie_gen/3 trie_gen_compiled/2 trie_gen_compiled/3 trie_insert/2 trie_insert/3 trie_insert/4 trie_lookup/3 trie_new/1 trie_term/2 trie_update/3 trim_heap/0 trim_stacks/0 true/0 tty_get_capability/3 tty_goto/2 tty_put/2 tty_size/2 ttyflush/0 undefined/0 unifiable/3 unify_with_occurs_check/2 unsetenv/1 unwrap_predicate/2 upcase_atom/2 use_module/1 use_module/2 var/1 var_number/2 variant_hash/2 variant_sha1/2 volatile/1 wait_for_input/3 wildcard_match/2 wildcard_match/3 with_mutex/2 with_output_to/2 with_tty_raw/1 write/1 write/2 write_canonical/1 write_canonical/2 write_length/3 write_term/2 write_term/3 writeln/1 writeln/2 writeq/1 writeq/2 zip_clone/2 zip_close_/2 zip_file_info_/3 zip_lock/1 zip_open_stream/3 zip_unlock/1 zipper_goto/2 zipper_open_current/3 zipper_open_new_file_in_zip/4)pl";
} // namespace rosetta::detail
