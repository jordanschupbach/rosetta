#pragma once

// Support code emitted into every generated Tcl extension (--tcl-api full). It is split into
// several literals only to stay below compiler limits for one string literal. Everything is
// inline: a split extension repeats it in each source, and the per-module namespace
// ROSETTA_TCL_MODULE keeps two extensions in one process apart.
//
// Objects are TclOO objects of TclOO classes; the C++ object hangs off the TclOO object as
// metadata (an Instance), and is destroyed with it (`$obj destroy`, `rename $obj {}`), unless
// C++ took ownership. Every native command and method runs inside guard(), which turns C++
// exceptions into Tcl errors with an -errorcode of {MODULE kind cpp_type message}. A Tcl error
// raised by Tcl code that C++ called (a callback or an override) travels through C++ as
// tcl_error, which carries the saved interpreter state, and is restored unchanged at the
// boundary.
namespace rosetta::detail {
inline constexpr const char* tcl_full_runtime_parts[] = {
R"cpp(
#ifndef TCL_OO_METHOD_PUBLIC
#define TCL_OO_METHOD_PUBLIC 1 /* Tcl 8.6 */
#endif
namespace ROSETTA_TCL_MODULE { namespace rosetta_tcl {
#ifdef TCL_SIZE_MAX
using Size = Tcl_Size;
#else
using Size = int;
#endif
struct TypeInfo;
struct Instance;
// ---------------------------------------------------------------- interpreter state
struct State {
    Tcl_Interp* interp = nullptr;
    std::string code;                 // first -errorcode element, such as FEATURES
    std::string ns;                   // ::features
    std::vector<Tcl_Class> classes;   // by TypeInfo::id
    std::map<std::pair<void*, const TypeInfo*>, Instance*> live;
    unsigned long functions = 0;
    bool deleted = false;
};
inline State*& current() { static thread_local State* value = nullptr; return value; }
inline State& state() {
    State* value = current();
    if (!value) throw std::logic_error("native code called a Tcl conversion outside a Tcl call");
    return *value;
}
inline Tcl_Interp* interp() { return state().interp; }

// ---------------------------------------------------------------- errors
// An error raised at the Tcl boundary: kind "binding" for argument and lifetime errors.
struct native_error { std::string kind, cpp, message; };
[[noreturn]] inline void fail(const std::string& message) { throw native_error{"binding", "", message}; }
[[noreturn]] inline void fail_as(const std::string& kind, const std::string& cpp, const std::string& message) {
    throw native_error{kind, cpp, message};
}
// A Tcl error (or another non-OK code) raised by Tcl code that C++ called.
struct SavedState {
    Tcl_InterpState saved;
    explicit SavedState(Tcl_InterpState value) : saved(value) {}
    SavedState(const SavedState&) = delete;
    SavedState& operator=(const SavedState&) = delete;
    ~SavedState() { if (saved) Tcl_DiscardInterpState(saved); }
    int restore(Tcl_Interp* interp) { Tcl_InterpState value = saved; saved = nullptr; return Tcl_RestoreInterpState(interp, value); }
};
struct tcl_error { std::shared_ptr<SavedState> saved; };
[[noreturn]] inline void raise_pending(Tcl_Interp* interp, int code = TCL_ERROR) {
    throw tcl_error{std::make_shared<SavedState>(Tcl_SaveInterpState(interp, code))};
}

inline std::string load_string(Tcl_Obj* obj) {
    Size length = 0;
    const char* data = Tcl_GetStringFromObj(obj, &length);
    std::string out(data, static_cast<std::size_t>(length));
    // Tcl spells U+0000 as the overlong sequence C0 80.
    for (std::size_t i = 0; (i = out.find("\xC0\x80", i)) != std::string::npos; ++i) out.replace(i, 2, 1, '\0');
    return out;
}
inline Tcl_Obj* text(const std::string& value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<Size>::max() / 2)) fail("string too long for Tcl");
    if (value.find('\0') == std::string::npos) return Tcl_NewStringObj(value.data(), static_cast<Size>(value.size()));
    std::string spelled;
    for (const char c : value) { if (c) spelled += c; else spelled += "\xC0\x80"; }
    return Tcl_NewStringObj(spelled.data(), static_cast<Size>(spelled.size()));
}
inline Tcl_Obj* empty() { return Tcl_NewObj(); }
inline bool is_empty(Tcl_Obj* obj) { Size length = 0; Tcl_GetStringFromObj(obj, &length); return length == 0; }
inline std::string shown(Tcl_Obj* obj) {
    auto value = load_string(obj);
    if (value.size() > 40) value = value.substr(0, 37) + "...";
    return "\"" + value + "\"";
}

inline std::string demangle(const char* name) {
    int status = 0;
    char* spelled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = status == 0 && spelled ? spelled : name;
    std::free(spelled);
    for (const char* inline_namespace : {"__cxx11::", "__1::"})
        for (std::size_t i = 0; (i = result.find(inline_namespace, i)) != std::string::npos;) result.erase(i, std::string(inline_namespace).size());
    return result;
}
// Sets the interpreter result and -errorcode {CODE kind ?cpp_type? message}.
inline int set_error(Tcl_Interp* interp, const std::string& kind, const std::string& cpp, const std::string& message) {
    Tcl_SetObjResult(interp, text(message));
    Tcl_Obj* code = Tcl_NewListObj(0, nullptr);
    const std::string prefix = current() ? current()->code : std::string("ROSETTA");
    Tcl_ListObjAppendElement(nullptr, code, text(prefix));
    Tcl_ListObjAppendElement(nullptr, code, text(kind));
    if (kind != "binding") Tcl_ListObjAppendElement(nullptr, code, text(cpp));
    Tcl_ListObjAppendElement(nullptr, code, text(message));
    Tcl_SetObjErrorCode(interp, code);
    return TCL_ERROR;
}
// Translates the in-flight C++ exception (inside a catch block).
inline int translate_current(Tcl_Interp* interp) noexcept {
    try {
        try { throw; }
        catch (const std::bad_alloc& e) { return set_error(interp, "bad_alloc", demangle(typeid(e).name()), "native allocation failed"); }
        catch (const std::invalid_argument& e) { return set_error(interp, "invalid_argument", demangle(typeid(e).name()), e.what()); }
        catch (const std::domain_error& e) { return set_error(interp, "domain_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::length_error& e) { return set_error(interp, "length_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::out_of_range& e) { return set_error(interp, "out_of_range", demangle(typeid(e).name()), e.what()); }
        catch (const std::future_error& e) { return set_error(interp, "logic_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::logic_error& e) { return set_error(interp, "logic_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::overflow_error& e) { return set_error(interp, "overflow_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::underflow_error& e) { return set_error(interp, "underflow_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::range_error& e) { return set_error(interp, "range_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::filesystem::filesystem_error& e) { return set_error(interp, "filesystem_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::ios_base::failure& e) { return set_error(interp, "io_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::runtime_error& e) { return set_error(interp, "runtime_error", demangle(typeid(e).name()), e.what()); }
        catch (const std::bad_optional_access& e) { return set_error(interp, "bad_optional_access", demangle(typeid(e).name()), e.what()); }
        catch (const std::bad_variant_access& e) { return set_error(interp, "bad_variant_access", demangle(typeid(e).name()), e.what()); }
        catch (const std::bad_cast& e) { return set_error(interp, "bad_cast", demangle(typeid(e).name()), e.what()); }
        catch (const std::bad_function_call& e) { return set_error(interp, "bad_function_call", demangle(typeid(e).name()), "empty native function"); }
        catch (const std::exception& e) { return set_error(interp, "exception", demangle(typeid(e).name()), e.what()); }
        catch (...) { return set_error(interp, "exception", "", "unknown C++ exception"); }
    } catch (...) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("native error while reporting a native error", -1));
        return TCL_ERROR;
    }
}
// Tcl and C++ buffer standard output separately (fully, when it is not a terminal): guard()
// flushes Tcl's stdout channel before C++ runs and C++'s stdout after, so `puts` and native
// output (print_summary and the like) stay in order when output is piped.
// (Tcl_Flush leaves the interpreter's result alone; a failed flush is not the call's error.)
inline void flush_tcl_stdout() {
    if (Tcl_Channel out = Tcl_GetStdChannel(TCL_STDOUT)) Tcl_Flush(out);
}
inline void flush_native_stdout() noexcept {
    try { std::cout.flush(); } catch (...) {}
    std::fflush(stdout);
}
// The outermost frame of every native command and method: the result (nullptr: empty) or a
// Tcl error once every C++ object of the call is destroyed.
template<class F> int guard(State* frame, F&& fn) {
    struct Restore { State* saved; ~Restore() { flush_native_stdout(); current() = saved; } } restore{current()};
    current() = frame;
    Tcl_Interp* interp = frame->interp;
    flush_tcl_stdout();
    try {
        Tcl_Obj* result = fn();
        if (result) Tcl_SetObjResult(interp, result);
        else Tcl_ResetResult(interp);
        return TCL_OK;
    }
    catch (tcl_error& e) { return e.saved->restore(interp); }
    catch (const native_error& e) { return set_error(interp, e.kind, e.cpp, e.message); }
    catch (...) { return translate_current(interp); }
}

// ---------------------------------------------------------------- Tcl value kinds
struct ObjTypes { const Tcl_ObjType *list, *dict, *integer, *wide, *real, *boolean, *bytearray, *bignum, *series; };
inline const ObjTypes& obj_types() {
    static const ObjTypes value = {Tcl_GetObjType("list"), Tcl_GetObjType("dict"), Tcl_GetObjType("int"), Tcl_GetObjType("wideInt"),
        Tcl_GetObjType("double"), Tcl_GetObjType("boolean"), Tcl_GetObjType("bytearray"), Tcl_GetObjType("bignum"),
        Tcl_GetObjType("arithseries")};
    return value;
}
inline bool has_type(Tcl_Obj* obj, const Tcl_ObjType* type) { return type && obj->typePtr == type; }
// Tcl 9 does not register its byte array type under a name Tcl_GetObjType finds.
inline bool byte_array(Tcl_Obj* obj) {
    return has_type(obj, obj_types().bytearray) || (obj->typePtr && obj->typePtr->name && std::strcmp(obj->typePtr->name, "bytearray") == 0);
}
inline bool numeric_rep(Tcl_Obj* obj) {
    const auto& t = obj_types();
    return obj->typePtr && (has_type(obj, t.integer) || has_type(obj, t.wide) || has_type(obj, t.real) || has_type(obj, t.bignum));
}
inline bool list_rep(Tcl_Obj* obj) { return has_type(obj, obj_types().list) || has_type(obj, obj_types().series) || has_type(obj, obj_types().dict); }
// Tcl 9 spells Tcl_GetBooleanFromObj as a macro around a GNU statement expression.
inline int get_boolean(Tcl_Obj* obj, int* out) {
#if TCL_MAJOR_VERSION > 8 || (TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 7)
    char value = 0;
    const int code = Tcl_GetBoolFromObj(nullptr, obj, 0, &value);
    *out = value;
    return code;
#else
    return Tcl_GetBooleanFromObj(nullptr, obj, out);
#endif
}
inline bool elements(Tcl_Obj* obj, Size& count, Tcl_Obj**& items) { return Tcl_ListObjGetElements(nullptr, obj, &count, &items) == TCL_OK; }
inline bool integer_like(Tcl_Obj* obj) { Tcl_WideInt n; return Tcl_GetWideIntFromObj(nullptr, obj, &n) == TCL_OK; }
inline bool real_like(Tcl_Obj* obj) { double n; return Tcl_GetDoubleFromObj(nullptr, obj, &n) == TCL_OK; }

// ---------------------------------------------------------------- classes and objects
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct FieldEntry {
    std::string name;
    Tcl_Obj* (*get)(Instance*);
    void (*set)(Instance*, Tcl_Obj*); // nullptr: read-only
};
struct TypeInfo {
    const char* name = nullptr;       // qualified Tcl class name
    std::size_t id = 0;
    bool registered = false;
    std::vector<BaseLink> bases;
    std::vector<FieldEntry> fields;
    void (*destroy)(void*) = nullptr;
    void* (*copy)(const void*) = nullptr;
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
// Specialized by generated code for every exported class.
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;
inline std::vector<TypeInfo*>& type_list() { static std::vector<TypeInfo*> value; return value; }
inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }

struct Director;
// Tcl's ownership of a C++ object; C++ shares it through aliasing shared_ptrs, and transfers
// take it over by releasing it.
struct Owner {
    void* ptr;
    void (*destroy)(void*);
    bool released = false;
    Owner(void* p, void (*d)(void*)) : ptr(p), destroy(d) {}
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner() { if (!released && ptr && destroy) destroy(ptr); }
};
// A TclOO object's native side.
struct Instance {
    void* ptr = nullptr;
    TypeInfo* info = nullptr;
    std::shared_ptr<void> life;   // the Owner, a C++ shared_ptr, or what keeps a borrowed object alive
    Owner* owner = nullptr;       // set while Tcl owns the object (life holds it)
    Director* director = nullptr; // a Tcl subclass object
    Tcl_Object object = nullptr;
    State* state = nullptr;
    bool shares = false;          // life is a C++ shared_ptr to the object itself
};
inline bool owned(const Instance* self) { return self && self->owner && !self->owner->released; }
inline void track(Instance* self) { if (self->ptr && self->state) self->state->live[{self->ptr, self->info}] = self; }
inline void untrack(Instance* self) {
    if (!self->ptr || !self->state) return;
    auto& live = self->state->live;
    const auto found = live.find({self->ptr, self->info});
    if (found != live.end() && found->second == self) live.erase(found);
}

// Base of generated director classes: C++ subclasses forwarding virtual calls to the methods of
// a Tcl subclass.
struct Director {
    State* rosetta_state = nullptr;
    Instance* rosetta_instance = nullptr; // null once the Tcl object is gone
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    // C++ deleting a Tcl subclass object (one it owned) deletes the Tcl object too.
    virtual ~Director() {
        if (!rosetta_instance) return;
        Instance* self = rosetta_instance;
        rosetta_instance = nullptr;
        untrack(self);
        self->ptr = nullptr;
        self->director = nullptr;
        State* s = self->state;
        if (s && !s->deleted && !Tcl_InterpDeleted(s->interp) && self->object && !Tcl_ObjectDeleted(self->object))
            Tcl_DeleteCommandFromToken(s->interp, Tcl_GetObjectCommand(self->object));
    }
};

inline void instance_delete(void* data) {
    auto* self = static_cast<Instance*>(data);
    untrack(self);
    if (self->director) self->director->rosetta_instance = nullptr;
    self->director = nullptr;
    self->owner = nullptr;
    delete self; // releases life: destroys the C++ object when Tcl was its last owner
}
inline int instance_clone(Tcl_Interp* interp, void* source, void** target);
inline const Tcl_ObjectMetadataType* instance_type() {
    static const Tcl_ObjectMetadataType value = {TCL_OO_METADATA_VERSION_CURRENT, "rosetta native object", instance_delete, instance_clone};
    return &value;
}
inline Instance* instance_of(Tcl_Object object) {
    return object ? static_cast<Instance*>(Tcl_ObjectGetMetadata(object, instance_type())) : nullptr;
}
// The native object a Tcl value names, or nullptr.
inline Instance* instance_of(Tcl_Obj* obj) {
    Size length = 0;
    const char* name = Tcl_GetStringFromObj(obj, &length);
    if (length == 0) return nullptr;
    for (Size i = 0; i < length; ++i) if (name[i] == ' ' || name[i] == '\t' || name[i] == '\n') return nullptr;
    Tcl_Interp* in = interp();
    if (!Tcl_GetCommandFromObj(in, obj)) return nullptr;
    Tcl_InterpState saved = Tcl_SaveInterpState(in, TCL_OK);
    Tcl_Object object = Tcl_GetObjectFromObj(in, obj);
    Tcl_RestoreInterpState(in, saved);
    return instance_of(object);
}
inline Tcl_Obj* object_name(Instance* self) {
    if (!self->object || Tcl_ObjectDeleted(self->object)) fail("native object has no Tcl object");
    return Tcl_GetObjectName(self->state->interp, self->object);
}
inline void* cast_path(void* ptr, const TypeInfo* from, const TypeInfo* to) {
    if (from == to) return ptr;
    for (const auto& link : from->bases)
        if (void* result = cast_path(link.upcast(ptr), link.base, to)) return result;
    return nullptr;
}
inline bool derives(const TypeInfo* from, const TypeInfo* to) {
    if (from == to) return true;
    for (const auto& link : from->bases) if (derives(link.base, to)) return true;
    return false;
}
inline std::string class_label(const TypeInfo* info) { return info && info->name ? info->name : "native object"; }
template<class T> bool is_instance(Instance* self) {
    return self && self->info && derives(self->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> bool is_instance(Tcl_Obj* obj) { return is_instance<T>(instance_of(obj)); }
template<class T> T* native(Instance* self) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    if (!self) fail("native " + class_label(info) + " object was never constructed (a Tcl subclass constructor must call next)");
    if (!self->ptr) fail("native " + class_label(self->info) + " object is closed (destroyed, moved to C++ or never constructed)");
    void* result = self->info ? cast_path(self->ptr, self->info, info) : nullptr;
    if (!result) fail("expected a " + class_label(info) + " object, got a " + class_label(self->info));
    return static_cast<U*>(result);
}
template<class T> T* native(Tcl_Obj* obj) {
    Instance* self = instance_of(obj);
    if (!self) fail("expected a " + class_label(&Info<std::remove_cv_t<T>>::value) + " object, got " + shown(obj));
    return native<T>(self);
}
inline bool is_director(const Instance* self) { return self && self->director; }

inline Tcl_Class class_of(State& s, const TypeInfo* info) { return info->id < s.classes.size() ? s.classes[info->id] : nullptr; }
// Wraps a C++ object in a new TclOO object (without running its constructor).
inline Instance* new_instance(State& s, void* ptr, TypeInfo* info) {
    Tcl_Class cls = class_of(s, info);
    if (!cls) fail("C++ result type " + class_label(info) + " is not exported to Tcl");
    Tcl_Object object = Tcl_NewObjectInstance(s.interp, cls, nullptr, nullptr, static_cast<Size>(-1), nullptr, 0);
    if (!object) raise_pending(s.interp);
    auto* self = new Instance;
    self->ptr = ptr;
    self->info = info;
    self->object = object;
    self->state = &s;
    Tcl_ObjectSetMetadata(object, instance_type(), self);
    return self;
}
inline void own(Instance* self) {
    auto owner = std::make_shared<Owner>(self->ptr, self->info->destroy);
    self->owner = owner.get();
    self->life = std::move(owner);
}
enum class Ownership { borrowed, owned };
template<class T> Tcl_Obj* wrap(T* value, Ownership ownership, std::shared_ptr<void> life = {}, bool shares = false) {
    using U = std::remove_cv_t<T>;
    if (!value) return empty();
    State& s = state();
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value); director && director->rosetta_instance) {
            Instance* self = director->rosetta_instance;
            if (ownership == Ownership::owned && self->ptr && !owned(self)) own(self); // C++ handed back a Tcl subclass object
            return object_name(self);
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info && class_of(s, found->second)) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    const auto destroy = [&] { if (ownership == Ownership::owned && info->destroy) info->destroy(ptr); };
    if (ownership == Ownership::borrowed) {
        const auto found = s.live.find({ptr, info});
        if (found != s.live.end() && found->second->ptr == ptr && found->second->object && !Tcl_ObjectDeleted(found->second->object))
            return object_name(found->second);
    }
    Instance* self = nullptr;
    try { self = new_instance(s, ptr, info); }
    catch (...) { destroy(); throw; }
    if (ownership == Ownership::owned) own(self);
    else { self->life = std::move(life); self->shares = shares; }
    track(self);
    return object_name(self);
}
template<class T> Tcl_Obj* wrap_shared(std::shared_ptr<T> value) {
    if (!value) return empty();
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()); director && director->rosetta_instance)
            return object_name(director->rosetta_instance);
    }
    U* raw = const_cast<U*>(value.get());
    return wrap(raw, Ownership::borrowed, std::shared_ptr<void>(std::const_pointer_cast<U>(value), raw), true);
}
// Shares ownership with Tcl: C++ keeps the object (and whatever keeps it alive) until it lets go.
template<class T> std::shared_ptr<T> shared(Tcl_Obj* obj) {
    if (is_empty(obj)) return {};
    T* ptr = native<T>(obj);
    Instance* self = instance_of(obj);
    if (self->life) return std::shared_ptr<T>(self->life, ptr);
    return std::shared_ptr<T>(ptr, [](T*) {}); // a borrowed object: the caller keeps it alive
}
template<class T> std::unique_ptr<T> take(Tcl_Obj* obj) {
    if (is_empty(obj)) return {};
    T* ptr = native<T>(obj);
    Instance* self = instance_of(obj);
    if (!owned(self) || self->life.use_count() != 1) fail("cannot transfer ownership of a shared or borrowed native object");
    if (self->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail("cannot transfer a derived object without a virtual destructor");
    self->owner->released = true;
    self->owner = nullptr;
    self->life.reset();
    if (!self->director) { untrack(self); self->ptr = nullptr; } // a Tcl subclass object stays usable until C++ deletes it
    return std::unique_ptr<T>(ptr);
}
inline int instance_clone(Tcl_Interp* interp, void* source, void** target) {
    auto* from = static_cast<Instance*>(source);
    *target = nullptr;
    if (!from || !from->ptr) return TCL_OK;
    if (from->director || !from->info->copy) {
        Tcl_SetObjResult(interp, text(class_label(from->info) + " objects cannot be copied"));
        return TCL_ERROR;
    }
    try {
        auto* copy = new Instance;
        copy->info = from->info;
        copy->state = from->state;
        copy->ptr = from->info->copy(from->ptr);
        own(copy);
        *target = copy;
        return TCL_OK;
    } catch (...) { return translate_current(interp); }
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- values
// Value<T>: check(obj, level) and load(obj) for arguments, cast(value) for results. Overloads are
// tried at level 0 (strict: the value's own kind decides), 1 (lenient: numbers widen, names
// convert) and 2 (anything that converts, so every value is also a string).
template<class T, class = void> struct Value;

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool word(Tcl_Obj* obj) {
        if (has_type(obj, obj_types().boolean)) return true;
        std::string value = load_string(obj);
        for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value == "true" || value == "false" || value == "yes" || value == "no" || value == "on" || value == "off";
    }
    static bool check(Tcl_Obj* obj, int level) {
        if (level == 0) return word(obj);
        int value = 0;
        return get_boolean(obj, &value) == TCL_OK;
    }
    static bool load(Tcl_Obj* obj) {
        int value = 0;
        if (get_boolean(obj, &value) != TCL_OK) fail("expected a boolean, got " + shown(obj));
        return value != 0;
    }
    static Tcl_Obj* cast(bool value) { return Tcl_NewBooleanObj(value ? 1 : 0); }
};
// char is a one-character string; signed and unsigned char stay integers.
template<> struct Value<char> {
    static bool check(Tcl_Obj* obj, int) { Size length = 0; Tcl_GetStringFromObj(obj, &length); return length == 1; }
    static char load(Tcl_Obj* obj) {
        Size length = 0;
        const char* data = Tcl_GetStringFromObj(obj, &length);
        if (length != 1) fail("expected a single ASCII character, got " + shown(obj));
        return data[0];
    }
    static Tcl_Obj* cast(char value) { return Tcl_NewStringObj(&value, 1); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(Tcl_Obj* obj, int) {
        if (has_type(obj, obj_types().real)) return false;
        if (integer_like(obj)) return true;
#if TCL_MAJOR_VERSION > 8
        if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(Tcl_WideUInt)) {
            Tcl_WideUInt n;
            return Tcl_GetWideUIntFromObj(nullptr, obj, &n) == TCL_OK;
        }
#endif
        return false;
    }
    [[noreturn]] static void range(Tcl_Obj* obj) {
        fail("integer " + load_string(obj) + " is outside the range of the C++ type (" +
             std::to_string(static_cast<long long>(std::numeric_limits<T>::lowest())) + ".." +
             std::to_string(static_cast<unsigned long long>(std::numeric_limits<T>::max())) + ")");
    }
    static T load(Tcl_Obj* obj) {
#if TCL_MAJOR_VERSION > 8
        void* number = nullptr;
        int kind = 0;
        if (Tcl_GetNumberFromObj(nullptr, obj, &number, &kind) != TCL_OK || (kind != TCL_NUMBER_INT && kind != TCL_NUMBER_BIG))
            fail("expected an integer, got " + shown(obj));
        if (kind == TCL_NUMBER_BIG) {
            if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(Tcl_WideUInt)) {
                Tcl_WideUInt n;
                if (Tcl_GetWideUIntFromObj(nullptr, obj, &n) == TCL_OK) return static_cast<T>(n);
            }
            range(obj);
        }
        const Tcl_WideInt n = *static_cast<Tcl_WideInt*>(number);
#else
        // Tcl 8.6 wraps decimal integers up to 2**64-1 into a Tcl_WideInt and has no API for
        // larger ones: decimal spellings are parsed here instead.
        Tcl_WideInt n;
        std::string spelled = load_string(obj);
        while (!spelled.empty() && std::isspace(static_cast<unsigned char>(spelled.back()))) spelled.pop_back();
        std::size_t at = spelled.find_first_not_of(" \t\n");
        const bool negative = at != std::string::npos && spelled[at] == '-';
        if (at != std::string::npos && (spelled[at] == '-' || spelled[at] == '+')) ++at;
        const bool decimal = at != std::string::npos && at < spelled.size() && spelled.find_first_not_of("0123456789", at) == std::string::npos &&
            (spelled[at] != '0' || at + 1 == spelled.size());
        if (decimal) {
            errno = 0;
            if (negative) {
                const long long v = std::strtoll(spelled.c_str(), nullptr, 10);
                if (errno != 0) range(obj);
                n = static_cast<Tcl_WideInt>(v);
            } else {
                const unsigned long long u = std::strtoull(spelled.c_str() + at, nullptr, 10);
                if (errno != 0) range(obj);
                if (u > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
                    if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(unsigned long long)) return static_cast<T>(u);
                    range(obj);
                }
                n = static_cast<Tcl_WideInt>(u);
            }
        } else if (Tcl_GetWideIntFromObj(nullptr, obj, &n) != TCL_OK) fail("expected an integer, got " + shown(obj));
#endif
        if constexpr (std::is_signed_v<T>) {
            if (n < static_cast<Tcl_WideInt>(std::numeric_limits<T>::lowest()) || n > static_cast<Tcl_WideInt>(std::numeric_limits<T>::max())) range(obj);
        } else {
            if (n < 0) range(obj);
            if (static_cast<unsigned long long>(n) > static_cast<unsigned long long>(std::numeric_limits<T>::max())) range(obj);
        }
        return static_cast<T>(n);
    }
    static Tcl_Obj* cast(T value) {
        if constexpr (std::is_signed_v<T>) return Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(value));
        else {
            if (static_cast<unsigned long long>(value) <= static_cast<unsigned long long>(std::numeric_limits<Tcl_WideInt>::max()))
                return Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(value));
#if TCL_MAJOR_VERSION > 8
            return Tcl_NewWideUIntObj(static_cast<Tcl_WideUInt>(value));
#else
            return text(std::to_string(static_cast<unsigned long long>(value)));
#endif
        }
    }
};
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(Tcl_Obj* obj, int level) {
        if (level == 0) return has_type(obj, obj_types().real) || (!numeric_rep(obj) && !integer_like(obj) && real_like(obj));
        return real_like(obj);
    }
    static T load(Tcl_Obj* obj) {
        double n;
        if (Tcl_GetDoubleFromObj(nullptr, obj, &n) != TCL_OK) fail("expected a number, got " + shown(obj));
        if constexpr (!std::is_same_v<T, double> && !std::is_same_v<T, long double>) {
            if (std::isfinite(n) && (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max()))
                fail("number " + load_string(obj) + " is outside the range of the C++ type");
        }
        return static_cast<T>(n);
    }
    static Tcl_Obj* cast(T value) { return Tcl_NewDoubleObj(static_cast<double>(value)); }
};
// Strings: at level 0 a value that is a number, a list of several elements or a native object
// is not taken for a string; at level 2 everything is.
inline bool string_check(Tcl_Obj* obj, int level) {
    if (instance_of(obj)) return false; // among overloads, an object never passes for a string
    if (level >= 2) return true;
    if (byte_array(obj)) return true;
    if (list_rep(obj)) { Size count = 0; Tcl_Obj** items = nullptr; if (!elements(obj, count, items) || count != 1) return false; }
    if (level == 0 && (numeric_rep(obj) || integer_like(obj) || real_like(obj))) return false;
    // A plain string of several numbers ("1 2 3") is more likely a list for another overload.
    if (!list_rep(obj)) {
        const std::string spelled = load_string(obj);
        if (spelled.find_first_of(" \t\n") != std::string::npos) {
            Size count = 0; Tcl_Obj** items = nullptr;
            if (elements(obj, count, items) && count > 1) {
                bool numbers = true;
                for (Size i = 0; i < count && numbers; ++i) numbers = integer_like(items[i]) || real_like(items[i]);
                if (numbers) return false;
            }
        }
    }
    return true;
}
template<> struct Value<std::string> {
    static bool check(Tcl_Obj* obj, int level) { return string_check(obj, level); }
    static std::string load(Tcl_Obj* obj) { return load_string(obj); }
    static Tcl_Obj* cast(const std::string& value) { return text(value); }
};
template<> struct Value<std::string_view> {
    static bool check(Tcl_Obj* obj, int level) { return string_check(obj, level); }
    static Tcl_Obj* cast(std::string_view value) { return text(std::string(value)); }
};
template<> struct Value<const char*> {
    static bool check(Tcl_Obj* obj, int level) { return string_check(obj, level); }
    static Tcl_Obj* cast(const char* value) { return value ? text(std::string(value)) : empty(); }
};
template<> struct Value<char*> {
    static Tcl_Obj* cast(const char* value) { return Value<const char*>::cast(value); }
};
template<> struct Value<std::filesystem::path> {
    static bool check(Tcl_Obj* obj, int level) { return string_check(obj, level); }
    static std::filesystem::path load(Tcl_Obj* obj) { return std::filesystem::path(load_string(obj)); }
    static Tcl_Obj* cast(const std::filesystem::path& value) { return text(value.string()); }
};
// "Nothing" is the empty string.
template<> struct Value<std::nullptr_t> {
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj); }
    static std::nullptr_t load(Tcl_Obj*) { return nullptr; }
    static Tcl_Obj* cast(std::nullptr_t) { return empty(); }
};
template<> struct Value<std::monostate> {
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj); }
    static std::monostate load(Tcl_Obj*) { return {}; }
    static Tcl_Obj* cast(std::monostate) { return empty(); }
};
// Complex numbers are {re im} lists; arguments also take a plain number.
template<class T> struct Value<std::complex<T>> {
    static bool check(Tcl_Obj* obj, int level) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (Value<T>::check(obj, level == 0 ? 1 : level)) return true;
        return elements(obj, count, items) && count == 2 && Value<T>::check(items[0], 1) && Value<T>::check(items[1], 1);
    }
    static std::complex<T> load(Tcl_Obj* obj) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (elements(obj, count, items) && count == 2) return {Value<T>::load(items[0]), Value<T>::load(items[1])};
        return {Value<T>::load(obj), T{}};
    }
    static Tcl_Obj* cast(const std::complex<T>& value) {
        Tcl_Obj* items[2] = {Value<T>::cast(value.real()), Value<T>::cast(value.imag())};
        return Tcl_NewListObj(2, items);
    }
};

// Enumerations are their enumerators' names; arguments also take the integer value. Flag
// enumerations (policy enum_flags) are integers; arguments also take a list of names.
struct EnumInfo {
    bool flags = false;
    std::vector<std::pair<std::string, long long>> members;
};
inline std::map<std::type_index, EnumInfo>& enum_infos() { static std::map<std::type_index, EnumInfo> value; return value; }
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    using N = std::conditional_t<std::is_same_v<U, bool>, int, U>;
    static const EnumInfo* info() {
        const auto found = enum_infos().find(std::type_index(typeid(T)));
        return found == enum_infos().end() ? nullptr : &found->second;
    }
    static bool member(const std::string& name, long long* out) {
        if (const auto* e = info())
            for (const auto& [item, value] : e->members) if (item == name) { if (out) *out = value; return true; }
        return false;
    }
    static std::string names() {
        std::string out;
        if (const auto* e = info()) for (const auto& item : e->members) out += (out.empty() ? "" : ", ") + item.first;
        return out;
    }
    static bool check(Tcl_Obj* obj, int level) {
        if (member(load_string(obj), nullptr)) return true;
        const auto* e = info();
        if (e && e->flags) {
            Size count = 0; Tcl_Obj** items = nullptr;
            if (elements(obj, count, items) && count > 1) {
                for (Size i = 0; i < count; ++i) if (!member(load_string(items[i]), nullptr)) return false;
                return true;
            }
        }
        if (level == 0 && !(e && e->flags)) return false;
        return Value<N>::check(obj, 1);
    }
    static T load(Tcl_Obj* obj) {
        long long number = 0;
        if (member(load_string(obj), &number)) return static_cast<T>(static_cast<U>(number));
        if (Value<N>::check(obj, 1)) return static_cast<T>(static_cast<U>(Value<N>::load(obj)));
        const auto* e = info();
        Size count = 0; Tcl_Obj** items = nullptr;
        if (e && e->flags && elements(obj, count, items) && count > 1) {
            unsigned long long bits = 0;
            for (Size i = 0; i < count; ++i) bits |= static_cast<unsigned long long>(static_cast<U>(load(items[i])));
            return static_cast<T>(static_cast<U>(bits));
        }
        fail("unknown enumerator " + shown(obj) + " (expected one of " + names() + " or an integer)");
    }
    static Tcl_Obj* cast(T value) {
        const auto number = static_cast<long long>(static_cast<U>(value));
        if (const auto* e = info(); e && !e->flags)
            for (const auto& [name, item] : e->members) if (item == number) return text(name);
        return Value<N>::cast(static_cast<N>(static_cast<U>(value)));
    }
};

// Exported classes by value: Tcl receives an owned copy (destroy it when done).
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(Tcl_Obj* obj, int) { return is_instance<T>(obj); }
    static T load(Tcl_Obj* obj) { return *native<T>(obj); }
    static Tcl_Obj* cast(const T& value) {
        if constexpr (copyable_v<T>) return wrap(new T(value), Ownership::owned);
        else fail("C++ result is not copyable");
    }
    static Tcl_Obj* cast(T&& value) {
        if constexpr (std::is_move_constructible_v<T> && !std::is_abstract_v<T>) return wrap(new T(std::move(value)), Ownership::owned);
        else return cast(static_cast<const T&>(value));
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj) || is_instance<T>(obj); }
    static T* load(Tcl_Obj* obj) { return is_empty(obj) ? nullptr : native<T>(obj); }
    static Tcl_Obj* cast(T* value) { return wrap(value, Ownership::borrowed); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj) || is_instance<T>(obj); }
    static std::shared_ptr<T> load(Tcl_Obj* obj) { return shared<T>(obj); }
    static Tcl_Obj* cast(const std::shared_ptr<T>& value) { return wrap_shared(value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj) || is_instance<T>(obj); }
    static std::unique_ptr<T, D> load(Tcl_Obj* obj) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(obj);
        else fail("custom deleters cannot receive Tcl objects");
    }
    static Tcl_Obj* cast(std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return wrap(value.release(), Ownership::owned);
        else fail("custom deleters cannot be returned to Tcl");
    }
};
// Optional values: the empty string is "no value".
template<class T> struct Value<std::optional<T>> {
    static bool check(Tcl_Obj* obj, int level) { return is_empty(obj) || Value<T>::check(obj, level); }
    static std::optional<T> load(Tcl_Obj* obj) {
        if (is_empty(obj)) return std::nullopt;
        return Value<T>::load(obj);
    }
    static Tcl_Obj* cast(const std::optional<T>& value) { return value ? Value<T>::cast(*value) : empty(); }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static Tcl_Obj* cast(std::reference_wrapper<T> value) {
        if constexpr (bound_v<T>) return wrap(&value.get(), Ownership::borrowed);
        else return Value<std::remove_cv_t<T>>::cast(value.get());
    }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- containers
inline Tcl_Obj* new_list() { return Tcl_NewListObj(0, nullptr); }
inline void push(Tcl_Obj* list, Tcl_Obj* item) { Tcl_ListObjAppendElement(nullptr, list, item); }
// Lists of elements (at level 0 a value that is not already a list must hold several elements,
// so a plain word is taken for a string first).
template<class C, class E> struct Sequence {
    static bool check(Tcl_Obj* obj, int level) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (instance_of(obj)) return false;
        if (!elements(obj, count, items)) return false;
        if (level == 0 && !list_rep(obj) && count == 1) return false;
        for (Size i = 0; i < count; ++i) if (!Value<E>::check(items[i], level == 0 ? 1 : level)) return false;
        return true;
    }
    static C load(Tcl_Obj* obj) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (!elements(obj, count, items)) fail("expected a list, got " + shown(obj));
        // Loading may shimmer obj; keep the elements alive.
        std::vector<Tcl_Obj*> held(items, items + count);
        for (auto* item : held) Tcl_IncrRefCount(item);
        struct Release { std::vector<Tcl_Obj*>& items; ~Release() { for (auto* item : items) Tcl_DecrRefCount(item); } } release{held};
        C result{};
        if constexpr (std::is_same_v<C, std::vector<E>>) result.reserve(held.size());
        for (auto* item : held) {
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(Value<E>::load(item));
            else result.push_back(Value<E>::load(item));
        }
        return result;
    }
    template<class Container> static Tcl_Obj* cast(Container&& value) {
        Tcl_Obj* result = new_list();
        for (auto&& item : value) {
            if constexpr (std::is_rvalue_reference_v<Container&&> && !std::is_same_v<E, bool>) push(result, Value<E>::cast(std::move(item)));
            else push(result, Value<E>::cast(item));
        }
        return result;
    }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
// Byte vectors also take a byte array (binary string).
template<class A> struct Value<std::vector<unsigned char, A>> : Sequence<std::vector<unsigned char, A>, unsigned char> {
    using Base = Sequence<std::vector<unsigned char, A>, unsigned char>;
    static bool check(Tcl_Obj* obj, int level) { return byte_array(obj) || Base::check(obj, level); }
    static std::vector<unsigned char, A> load(Tcl_Obj* obj) {
        if (byte_array(obj)) {
            Size length = 0;
#if TCL_MAJOR_VERSION > 8
            const unsigned char* data = Tcl_GetBytesFromObj(nullptr, obj, &length);
#else
            const unsigned char* data = Tcl_GetByteArrayFromObj(obj, &length);
#endif
            if (data) return std::vector<unsigned char, A>(data, data + length);
        }
        return Base::load(obj);
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
template<class E, class C, class A> struct Value<std::set<E, C, A>> : Sequence<std::set<E, C, A>, E> {};
template<class E, class H, class Q, class A> struct Value<std::unordered_set<E, H, Q, A>> : Sequence<std::unordered_set<E, H, Q, A>, E> {};
template<class E> struct Value<std::valarray<E>> {
    static bool check(Tcl_Obj* obj, int level) { return Value<std::vector<E>>::check(obj, level); }
    static std::valarray<E> load(Tcl_Obj* obj) { auto v = Value<std::vector<E>>::load(obj); return std::valarray<E>(v.data(), v.size()); }
    static Tcl_Obj* cast(const std::valarray<E>& value) { return Value<std::vector<E>>::cast(std::vector<E>(std::begin(value), std::end(value))); }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(Tcl_Obj* obj, int level) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (!elements(obj, count, items) || static_cast<std::size_t>(count) != N) return false;
        for (Size i = 0; i < count; ++i) if (!Value<E>::check(items[i], level == 0 ? 1 : level)) return false;
        return true;
    }
    static std::array<E, N> load(Tcl_Obj* obj) {
        auto items = Value<std::vector<E>>::load(obj);
        if (items.size() != N) fail("expected a list of " + std::to_string(N) + " elements, got " + shown(obj));
        std::array<E, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = std::move(items[i]);
        return result;
    }
    static Tcl_Obj* cast(const std::array<E, N>& value) { return Sequence<std::vector<E>, E>::cast(value); }
};
// Maps are dicts (or any list of key value pairs).
template<class M, class K, class V> struct Mapping {
    static bool check(Tcl_Obj* obj, int level) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (instance_of(obj) || !elements(obj, count, items) || count % 2 != 0) return false;
        if (level == 0 && count == 0 && !list_rep(obj) && !is_empty(obj)) return false;
        for (Size i = 0; i < count; i += 2)
            if (!Value<K>::check(items[i], level == 0 ? 1 : level) || !Value<V>::check(items[i + 1], level == 0 ? 1 : level)) return false;
        return true;
    }
    static M load(Tcl_Obj* obj) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (!elements(obj, count, items) || count % 2 != 0) fail("expected a dict, got " + shown(obj));
        std::vector<Tcl_Obj*> held(items, items + count);
        for (auto* item : held) Tcl_IncrRefCount(item);
        struct Release { std::vector<Tcl_Obj*>& items; ~Release() { for (auto* item : items) Tcl_DecrRefCount(item); } } release{held};
        M result;
        for (std::size_t i = 0; i < held.size(); i += 2) result.insert_or_assign(Value<K>::load(held[i]), Value<V>::load(held[i + 1]));
        return result;
    }
    static Tcl_Obj* cast(const M& value) {
        Tcl_Obj* result = Tcl_NewDictObj();
        for (const auto& [key, item] : value) Tcl_DictObjPut(nullptr, result, Value<K>::cast(key), Value<V>::cast(item));
        return result;
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};
// Pairs and tuples are lists.
template<class A, class B> struct Value<std::pair<A, B>> {
    static bool check(Tcl_Obj* obj, int level) {
        Size count = 0; Tcl_Obj** items = nullptr;
        return elements(obj, count, items) && count == 2 && Value<A>::check(items[0], level == 0 ? 1 : level) &&
            Value<B>::check(items[1], level == 0 ? 1 : level);
    }
    static std::pair<A, B> load(Tcl_Obj* obj) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (!elements(obj, count, items) || count != 2) fail("expected a list of two elements, got " + shown(obj));
        Tcl_Obj* first = items[0]; Tcl_Obj* second = items[1];
        Tcl_IncrRefCount(first); Tcl_IncrRefCount(second);
        struct Release { Tcl_Obj* a; Tcl_Obj* b; ~Release() { Tcl_DecrRefCount(a); Tcl_DecrRefCount(b); } } release{first, second};
        return {Value<A>::load(first), Value<B>::load(second)};
    }
    static Tcl_Obj* cast(const std::pair<A, B>& value) {
        Tcl_Obj* items[2] = {Value<A>::cast(value.first), Value<B>::cast(value.second)};
        return Tcl_NewListObj(2, items);
    }
};
template<class... T> struct Value<std::tuple<T...>> {
    template<std::size_t... I> static bool check_items(Tcl_Obj** items, int level, std::index_sequence<I...>) {
        return (Value<T>::check(items[I], level) && ...);
    }
    static bool check(Tcl_Obj* obj, int level) {
        Size count = 0; Tcl_Obj** items = nullptr;
        return elements(obj, count, items) && count == static_cast<Size>(sizeof...(T)) &&
            check_items(items, level == 0 ? 1 : level, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static std::tuple<T...> load_items(const std::vector<Tcl_Obj*>& items, std::index_sequence<I...>) {
        return std::tuple<T...>{Value<T>::load(items[I])...};
    }
    static std::tuple<T...> load(Tcl_Obj* obj) {
        Size count = 0; Tcl_Obj** items = nullptr;
        if (!elements(obj, count, items) || count != static_cast<Size>(sizeof...(T)))
            fail("expected a list of " + std::to_string(sizeof...(T)) + " elements, got " + shown(obj));
        std::vector<Tcl_Obj*> held(items, items + count);
        for (auto* item : held) Tcl_IncrRefCount(item);
        struct Release { std::vector<Tcl_Obj*>& items; ~Release() { for (auto* item : items) Tcl_DecrRefCount(item); } } release{held};
        return load_items(held, std::index_sequence_for<T...>{});
    }
    template<std::size_t... I> static Tcl_Obj* cast_items(const std::tuple<T...>& value, std::index_sequence<I...>) {
        Tcl_Obj* result = new_list();
        (push(result, Value<T>::cast(std::get<I>(value))), ...);
        return result;
    }
    static Tcl_Obj* cast(const std::tuple<T...>& value) { return cast_items(value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(Tcl_Obj* obj, int level) { return (Value<T>::check(obj, level) || ...); }
    template<class First, class... Rest> static std::variant<T...> load_first(Tcl_Obj* obj, int level) {
        if (Value<First>::check(obj, level)) return std::variant<T...>(std::in_place_type<First>, Value<First>::load(obj));
        if constexpr (sizeof...(Rest) > 0) return load_first<Rest...>(obj, level);
        else fail("no variant alternative accepts " + shown(obj));
    }
    static std::variant<T...> load(Tcl_Obj* obj) {
        for (int level = 0; level < 2; ++level) if ((Value<T>::check(obj, level) || ...)) return load_first<T...>(obj, level);
        return load_first<T...>(obj, 2);
    }
    static Tcl_Obj* cast(const std::variant<T...>& value) {
        return std::visit([](const auto& item) -> Tcl_Obj* { return Value<std::decay_t<decltype(item)>>::cast(item); }, value);
    }
};
// Durations are seconds.
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(Tcl_Obj* obj, int level) { return Value<double>::check(obj, level == 0 ? 1 : level); }
    static D load(Tcl_Obj* obj) { return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(obj))); }
    static Tcl_Obj* cast(const D& value) { return Value<double>::cast(std::chrono::duration<double>(value).count()); }
};

// ---------------------------------------------------------------- arguments
// Converts a Tcl argument for a declared C++ parameter type P and keeps any temporary alive.
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(Tcl_Obj* obj, int level) { return Value<V>::check(obj, level); }
    explicit Param(Tcl_Obj* obj) : value_(Value<V>::load(obj)) {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    void writeback(Tcl_Obj*) {}
private:
    V value_;
};
// A mutable reference to a converted value takes a variable name: the variable's value goes in
// and the updated value comes back into the variable.
template<class P> struct Param<P, std::enable_if_t<std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>> &&
                                                    !bound_v<std::remove_reference_t<P>>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(Tcl_Obj* name, int level) {
        Tcl_Obj* value = Tcl_ObjGetVar2(interp(), name, nullptr, 0);
        return value && Value<V>::check(value, level == 0 ? 1 : level);
    }
    explicit Param(Tcl_Obj* name) : value_(load(name)) {}
    P get() { return value_; }
    void writeback(Tcl_Obj* name) {
        if constexpr (std::is_copy_constructible_v<V>) {
            if (!Tcl_ObjSetVar2(interp(), name, nullptr, Value<V>::cast(value_), TCL_LEAVE_ERR_MSG)) raise_pending(interp());
        }
    }
private:
    static V load(Tcl_Obj* name) {
        Tcl_Obj* value = Tcl_ObjGetVar2(interp(), name, nullptr, TCL_LEAVE_ERR_MSG);
        if (!value) raise_pending(interp());
        return Value<V>::load(value);
    }
    V value_;
};
// Exported classes are passed by reference to the Tcl-owned object.
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(Tcl_Obj* obj, int) { return is_instance<V>(obj); }
    explicit Param(Tcl_Obj* obj) : ptr_(native<V>(obj)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
    void writeback(Tcl_Obj*) {}
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj) || is_instance<V>(obj); }
    explicit Param(Tcl_Obj* obj) : ptr_(is_empty(obj) ? nullptr : native<V>(obj)) {}
    P get() { return ptr_; }
    void writeback(Tcl_Obj*) {}
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(Tcl_Obj* obj, int level) { return string_check(obj, level); }
    explicit Param(Tcl_Obj* obj) : text_(load_string(obj)) {}
    const char* get() { return text_.c_str(); }
    void writeback(Tcl_Obj*) {}
private:
    std::string text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(Tcl_Obj* obj, int level) { return string_check(obj, level); }
    explicit Param(Tcl_Obj* obj) : text_(load_string(obj)) {}
    std::string_view get() { return text_; }
    void writeback(Tcl_Obj*) {}
private:
    std::string text_;
};

// One native call: the receiver (methods), the arguments after the command or method name, and
// the words naming the call for messages.
struct Call {
    Instance* self = nullptr;
    Tcl_Object object = nullptr;
    Size n = 0;
    Tcl_Obj* const* a = nullptr;
    Size skip = 0;
    Tcl_Obj* const* objv = nullptr;
    bool arity_matched = false;
    bool match() { arity_matched = true; return true; }
    std::string prefix() const {
        std::string out;
        for (Size i = 0; i < skip; ++i) out += (i ? " " : "") + load_string(objv[i]);
        return out;
    }
};
[[noreturn]] inline void no_overload(const Call& call, const char* signatures) {
    std::string received;
    for (Size i = 0; i < call.n; ++i) received += (i ? " " : "") + shown(call.a[i]);
    if (!call.arity_matched) fail("wrong # args: should be one of\n" + std::string(signatures) + "(called as: " + call.prefix() +
                                  (received.empty() ? "" : " " + received) + ")");
    fail(call.prefix() + ": no overload accepts (" + received + "); supported signatures:\n" + signatures);
}
[[noreturn]] inline void wrong_args(const Call& call, const std::string& usage) {
    fail("wrong # args: should be \"" + call.prefix() + (usage.empty() ? "" : " " + usage) + "\"");
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
inline std::shared_ptr<void> keepalive(Instance* parent) { return parent ? parent->life : std::shared_ptr<void>(); }
template<class R> Tcl_Obj* result(R&& value, Instance* parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) return empty();
        if (policy == Return::owned) return wrap(ptr, Ownership::owned);
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) return wrap(new B(*ptr), Ownership::owned);
            else fail("C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, keepalive(parent));
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) return wrap(new V(value), Ownership::owned);
            else fail("C++ result is not copyable");
        }
        return wrap(ptr, Ownership::borrowed, keepalive(parent));
    } else if constexpr (bound_v<V>) {
        return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else if constexpr (std::is_lvalue_reference_v<R>) {
        return Value<V>::cast(static_cast<const V&>(value));
    } else {
        return Value<V>::cast(std::move(value));
    }
}
// Calls a native function (a lambda returning decltype(auto)) and converts its result.
template<class F> Tcl_Obj* call(F&& function, Instance* parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); return nullptr; }
    else return result<R>(function(), parent, policy);
}
template <class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail("native field type cannot be assigned");
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- C++ -> Tcl calls
// Converts arguments passed from C++ to Tcl; references to classes are borrowed.
template<class A> Tcl_Obj* argument(A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) return wrap(const_cast<V*>(&value), Ownership::borrowed);
        else return Value<V>::cast(std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        return wrap(const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        return Value<const char*>::cast(value);
    } else {
        return Value<V>::cast(value);
    }
}
template<class R> R returned(Tcl_Obj* value) {
    if constexpr (std::is_void_v<R>) { (void)value; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_lvalue_reference_v<R> && bound_v<V>) return *native<V>(value);
        else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(value);
        else {
            if (!Value<V>::check(value, 2))
                fail("Tcl callback returned " + shown(value) + ", which does not convert to the C++ result type");
            return Value<V>::load(value);
        }
    }
}
// Evaluates words (a command prefix with the converted arguments appended) and converts the
// result to R. A Tcl error becomes tcl_error.
struct Words {
    std::vector<Tcl_Obj*> items;
    Words() = default;
    Words(const Words&) = delete;
    Words& operator=(const Words&) = delete;
    void add(Tcl_Obj* item) { Tcl_IncrRefCount(item); items.push_back(item); }
    ~Words() { for (auto* item : items) Tcl_DecrRefCount(item); }
};
template<class R> R evaluate(State& s, Words& words, int flags) {
    struct Restore { State* saved; ~Restore() { current() = saved; } } restore{current()};
    current() = &s;
    if (s.deleted || Tcl_InterpDeleted(s.interp)) throw std::runtime_error("the Tcl interpreter of a callback was deleted");
    const int code = Tcl_EvalObjv(s.interp, static_cast<Size>(words.items.size()), words.items.data(), flags);
    if (code != TCL_OK) raise_pending(s.interp, code);
    if constexpr (std::is_void_v<R>) return;
    else {
        Tcl_Obj* value = Tcl_GetObjResult(s.interp);
        Tcl_IncrRefCount(value);
        struct Release { Tcl_Obj* value; ~Release() { Tcl_DecrRefCount(value); } } release{value};
        return returned<R>(value);
    }
}
// A Tcl command prefix held by C++ (a std::function argument).
struct Script {
    State* state;
    Tcl_Obj* prefix;
    Script(State* s, Tcl_Obj* p) : state(s), prefix(p) { Tcl_IncrRefCount(prefix); Tcl_Preserve(state->interp); }
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;
    ~Script() { Tcl_DecrRefCount(prefix); Tcl_Release(state->interp); }
};
inline bool command_prefix(Tcl_Obj* obj) {
    Size count = 0; Tcl_Obj** items = nullptr;
    if (!elements(obj, count, items) || count < 1) return false;
    return Tcl_GetCommandFromObj(interp(), items[0]) != nullptr;
}
// Natives functions returned to Tcl become commands.
struct Function {
    State* state;
    std::size_t arity;
    std::function<Tcl_Obj*(Call&)> invoke;
};
inline int function_command(void* data, Tcl_Interp*, int objc, Tcl_Obj* const objv[]) {
    auto* function = static_cast<Function*>(data);
    return guard(function->state, [&]() -> Tcl_Obj* {
        Call call;
        call.n = objc - 1; call.a = objv + 1; call.skip = 1; call.objv = objv;
        if (static_cast<std::size_t>(call.n) != function->arity)
            wrong_args(call, std::to_string(function->arity) + " argument" + (function->arity == 1 ? "" : "s"));
        return function->invoke(call);
    });
}
inline void function_delete(void* data) { delete static_cast<Function*>(data); }
template<class R, class... A, std::size_t... I>
Tcl_Obj* call_native(const std::function<R(A...)>& function, Call& c, std::index_sequence<I...>) {
    std::tuple<Param<A>...> params{Param<A>(c.a[I])...};
    (void)params;
    return call([&]() -> R { return function(std::get<I>(params).get()...); }, nullptr, Return::copy);
}
// A std::function takes a command prefix (called with the arguments appended, at global level);
// the empty string is an empty function.
template<class R, class... A> struct Value<std::function<R(A...)>> {
    static bool check(Tcl_Obj* obj, int) { return is_empty(obj) || command_prefix(obj); }
    static std::function<R(A...)> load(Tcl_Obj* obj) {
        if (is_empty(obj)) return {};
        if (!command_prefix(obj)) fail("expected a command prefix, got " + shown(obj));
        auto script = std::make_shared<Script>(&state(), obj);
        return [script](A... arguments) -> R {
            Words words;
            Size count = 0; Tcl_Obj** items = nullptr;
            elements(script->prefix, count, items);
            for (Size i = 0; i < count; ++i) words.add(items[i]);
            struct Restore { State* saved; ~Restore() { current() = saved; } } restore{current()};
            current() = script->state;
            (words.add(argument<A>(std::forward<A>(arguments))), ...);
            return evaluate<R>(*script->state, words, TCL_EVAL_GLOBAL);
        };
    }
    static Tcl_Obj* cast(const std::function<R(A...)>& value) {
        if (!value) return empty();
        State& s = state();
        auto* function = new Function{&s, sizeof...(A), [value](Call& c) -> Tcl_Obj* {
            return call_native<R, A...>(value, c, std::index_sequence_for<A...>{});
        }};
        const std::string name = s.ns + "::native_function#" + std::to_string(++s.functions);
        if (!Tcl_CreateObjCommand(s.interp, name.c_str(), function_command, function, function_delete)) {
            delete function;
            raise_pending(s.interp);
        }
        return text(name);
    }
};

// ---------------------------------------------------------------- directors
// Whether the Tcl object's method `name` comes from Tcl code rather than from a native class.
inline bool overridden(State& s, Instance* self, const char* name) {
    if (!self || !self->object || Tcl_ObjectDeleted(self->object)) return false;
    Words words;
    words.add(Tcl_NewStringObj("::info", -1));
    words.add(Tcl_NewStringObj("object", -1));
    words.add(Tcl_NewStringObj("call", -1));
    words.add(Tcl_GetObjectName(s.interp, self->object));
    words.add(Tcl_NewStringObj(name, -1));
    Tcl_InterpState saved = Tcl_SaveInterpState(s.interp, TCL_OK);
    bool result = false;
    if (Tcl_EvalObjv(s.interp, static_cast<Size>(words.items.size()), words.items.data(), TCL_EVAL_GLOBAL) == TCL_OK) {
        Size count = 0; Tcl_Obj** chain = nullptr;
        if (elements(Tcl_GetObjResult(s.interp), count, chain)) {
            for (Size i = 0; i < count; ++i) {
                Size parts = 0; Tcl_Obj** entry = nullptr;
                if (!elements(chain[i], parts, entry) || parts < 4) continue;
                const auto kind = load_string(entry[0]);
                if (kind == "filter") continue;
                result = kind == "method" && load_string(entry[3]) != "rosetta";
                break;
            }
        }
    }
    Tcl_RestoreInterpState(s.interp, saved);
    return result;
}
inline bool dispatchable(const Director* director, const char* name) {
    if (!director->rosetta_instance || !director->rosetta_state) return false;
    State& s = *director->rosetta_state;
    if (s.deleted || Tcl_InterpDeleted(s.interp)) return false;
    struct Restore { State* saved; ~Restore() { current() = saved; } } restore{current()};
    current() = &s;
    return overridden(s, director->rosetta_instance, name);
}
// Calls the Tcl subclass's method and converts its result to R.
template<class R, class... A> R invoke_method(const Director* director, const char* name, A&&... arguments) {
    State& s = *director->rosetta_state;
    struct Restore { State* saved; ~Restore() { current() = saved; } } restore{current()};
    current() = &s;
    Words words;
    words.add(object_name(director->rosetta_instance));
    words.add(Tcl_NewStringObj(name, -1));
    (words.add(argument<A>(std::forward<A>(arguments))), ...);
    return evaluate<R>(s, words, TCL_EVAL_GLOBAL);
}
[[noreturn]] inline void pure_virtual(const char* name) {
    fail_as("not_implemented", "", std::string("pure virtual method ") + name + " is not implemented by the Tcl subclass");
}
// A Tcl error from a noexcept override cannot reach C++: report it and terminate.
[[noreturn]] inline void unraisable(const char* name) noexcept {
    std::fprintf(stderr, "rosetta: error in the Tcl override of noexcept %s\n", name);
    std::terminate();
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- registration
using Wrapper = Tcl_Obj* (*)(Call&);
struct Entry { State* state; Wrapper fn; };
inline int command_call(void* data, Tcl_Interp*, int objc, Tcl_Obj* const objv[]) {
    auto* entry = static_cast<Entry*>(data);
    return guard(entry->state, [&]() -> Tcl_Obj* {
        Call call;
        call.n = objc - 1; call.a = objv + 1; call.skip = 1; call.objv = objv;
        return entry->fn(call);
    });
}
inline void entry_delete(void* data) { delete static_cast<Entry*>(data); }
inline int method_call(void* data, Tcl_Interp*, Tcl_ObjectContext context, int objc, Tcl_Obj* const* objv) {
    auto* entry = static_cast<Entry*>(data);
    return guard(entry->state, [&]() -> Tcl_Obj* {
        Call call;
        call.object = Tcl_ObjectContextObject(context);
        call.self = instance_of(call.object);
        if (call.self) call.self->object = call.object;
        call.skip = static_cast<Size>(Tcl_ObjectContextSkippedArgs(context));
        call.n = objc - call.skip; call.a = objv + call.skip; call.objv = objv;
        return entry->fn(call);
    });
}
inline int entry_clone(Tcl_Interp*, void* data, void** target) {
    *target = new Entry(*static_cast<Entry*>(data));
    return TCL_OK;
}
inline const Tcl_MethodType* method_type() {
    static const Tcl_MethodType value = {TCL_OO_METHOD_VERSION_CURRENT, "rosetta", method_call, entry_delete, entry_clone};
    return &value;
}
inline Tcl_Namespace* namespace_of(State& s, const std::string& name) {
    Tcl_Namespace* found = Tcl_FindNamespace(s.interp, name.c_str(), nullptr, 0);
    if (!found) found = Tcl_CreateNamespace(s.interp, name.c_str(), nullptr, nullptr);
    if (!found) raise_pending(s.interp);
    return found;
}
inline std::string parent_namespace(const std::string& name) {
    const auto at = name.rfind("::");
    return at == std::string::npos || at == 0 ? std::string("::") : name.substr(0, at);
}
inline void eval_words(State& s, std::initializer_list<std::string> items) {
    Words words;
    for (const auto& item : items) words.add(text(item));
    if (Tcl_EvalObjv(s.interp, static_cast<Size>(words.items.size()), words.items.data(), TCL_EVAL_GLOBAL) != TCL_OK) raise_pending(s.interp);
}
inline Tcl_Class find_class(State& s, const std::string& name) {
    Tcl_Obj* word = text(name);
    Tcl_IncrRefCount(word);
    Tcl_Object object = Tcl_GetObjectFromObj(s.interp, word);
    Tcl_DecrRefCount(word);
    if (!object) raise_pending(s.interp);
    Tcl_Class cls = Tcl_GetObjectAsClass(object);
    if (!cls) fail(name + " is not a class");
    return cls;
}
template<class T> void register_type(TypeInfo& info, const char* name) {
    info.name = name;
    if (!info.registered) {
        info.registered = true;
        info.id = type_list().size();
        type_list().push_back(&info);
    }
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* ptr) { delete static_cast<T*>(ptr); };
    if constexpr (copyable_v<T>) info.copy = [](const void* ptr) -> void* { return new T(*static_cast<const T*>(ptr)); };
    registry()[std::type_index(typeid(T))] = &info;
}
template<class Derived, class Base> void add_base(TypeInfo& info) {
    info.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* { return static_cast<Base*>(static_cast<Derived*>(ptr)); }});
}
// Creates the TclOO class NAME (its namespace first) with the given superclasses.
inline Tcl_Class define_class(State& s, TypeInfo& info, const std::string& name, const std::vector<std::string>& superclasses) {
    namespace_of(s, parent_namespace(name));
    eval_words(s, {"::oo::class", "create", name});
    Words words;
    for (const char* item : {"::oo::define", name.c_str(), "superclass"}) words.add(text(item));
    for (const auto& item : superclasses) words.add(text(item));
    if (Tcl_EvalObjv(s.interp, static_cast<Size>(words.items.size()), words.items.data(), TCL_EVAL_GLOBAL) != TCL_OK) raise_pending(s.interp);
    Tcl_Class cls = find_class(s, name);
    if (s.classes.size() <= info.id) s.classes.resize(info.id + 1, nullptr);
    s.classes[info.id] = cls;
    return cls;
}
inline void add_method(State& s, Tcl_Class cls, const char* name, Wrapper fn) {
    Tcl_Obj* word = Tcl_NewStringObj(name, -1);
    Tcl_IncrRefCount(word);
    Tcl_NewMethod(s.interp, cls, word, TCL_OO_METHOD_PUBLIC, method_type(), new Entry{&s, fn});
    Tcl_DecrRefCount(word);
}
// Static methods are methods of the class object itself (`Class name ...`).
inline void add_static(State& s, Tcl_Class cls, const char* name, Wrapper fn) {
    Tcl_Obj* word = Tcl_NewStringObj(name, -1);
    Tcl_IncrRefCount(word);
    Tcl_NewInstanceMethod(s.interp, Tcl_GetClassAsObject(cls), word, TCL_OO_METHOD_PUBLIC, method_type(), new Entry{&s, fn});
    Tcl_DecrRefCount(word);
}
inline void set_constructor(State& s, Tcl_Class cls, Wrapper fn) {
    Tcl_ClassSetConstructor(s.interp, cls, Tcl_NewMethod(s.interp, cls, nullptr, TCL_OO_METHOD_PUBLIC, method_type(), new Entry{&s, fn}));
}
inline void add_command(State& s, const std::string& name, Wrapper fn) {
    namespace_of(s, parent_namespace(name));
    if (!Tcl_CreateObjCommand(s.interp, name.c_str(), command_call, new Entry{&s, fn}, entry_delete)) raise_pending(s.interp);
}
// Exports a command from its namespace unless a global command of that name exists (so that
// `namespace import ns::*` never replaces one of Tcl's own commands).
inline void export_command(State& s, const std::string& name) {
    const auto at = name.rfind("::");
    const auto tail = name.substr(at + 2);
    if (Tcl_FindCommand(s.interp, ("::" + tail).c_str(), nullptr, TCL_GLOBAL_ONLY)) return;
    std::string pattern;
    for (const char c : tail) { if (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\') pattern += '\\'; pattern += c; }
    Tcl_Export(s.interp, namespace_of(s, parent_namespace(name)), pattern.c_str(), 0);
}
// The class of an object (the generated constructor compares it with its own class to
// recognize Tcl subclasses).
inline Tcl_Class class_of_object(State& s, Tcl_Object object) {
#if TCL_MAJOR_VERSION > 8 || TCL_MINOR_VERSION > 6
    (void)s;
    return Tcl_GetClassOfObject(object);
#else
    Words words;
    words.add(text("::info")); words.add(text("object")); words.add(text("class")); words.add(Tcl_GetObjectName(s.interp, object));
    if (Tcl_EvalObjv(s.interp, static_cast<Size>(words.items.size()), words.items.data(), TCL_EVAL_GLOBAL) != TCL_OK) raise_pending(s.interp);
    Tcl_Object cls = Tcl_GetObjectFromObj(s.interp, Tcl_GetObjResult(s.interp));
    if (!cls) raise_pending(s.interp);
    return Tcl_GetObjectAsClass(cls);
#endif
}
// The native side of a constructed object.
template<class T> void adopt(Call& call, T* created, Director* director) {
    State& s = state();
    auto* self = new Instance;
    self->ptr = created;
    self->info = &Info<T>::value;
    self->object = call.object;
    self->state = &s;
    own(self);
    if (director) {
        self->director = director;
        director->rosetta_state = &s;
        director->rosetta_instance = self;
    }
    Tcl_ObjectSetMetadata(call.object, instance_type(), self);
    track(self);
}
inline void check_unconstructed(Call& call) {
    if (Instance* self = instance_of(call.object); self && self->ptr) fail("native object already constructed");
}
template<class T> bool constructing_subclass(Call& call) {
    State& s = state();
    return class_of_object(s, call.object) != class_of(s, &Info<T>::value);
}
template<class T> void add_field(TypeInfo& info, const char* name, Tcl_Obj* (*get)(Instance*), void (*set)(Instance*, Tcl_Obj*)) {
    info.fields.push_back({name, get, set});
}
// `$obj field ?value?`: reads or writes one field.
inline Tcl_Obj* field_access(Call& call, const char* name, Tcl_Obj* (*get)(Instance*), void (*set)(Instance*, Tcl_Obj*)) {
    if (call.n == 0) return get(call.self);
    if (call.n == 1) {
        if (!set) fail("field " + std::string(name) + " is read-only");
        set(call.self, call.a[0]);
        return nullptr;
    }
    wrong_args(call, "?value?");
}
inline const FieldEntry* find_field(const TypeInfo* info, const std::string& name) {
    for (const auto& field : info->fields) if (field.name == name) return &field;
    for (const auto& link : info->bases) if (const auto* found = find_field(link.base, name)) return found;
    return nullptr;
}
inline void all_fields(const TypeInfo* info, std::vector<const FieldEntry*>& out) {
    for (const auto& link : info->bases) all_fields(link.base, out);
    for (const auto& field : info->fields) {
        bool seen = false;
        for (const auto* item : out) seen = seen || item->name == field.name;
        if (!seen) out.push_back(&field);
    }
}
inline const FieldEntry& option_field(Call& call, Tcl_Obj* option) {
    const auto spelled = load_string(option);
    if (spelled.size() < 2 || spelled[0] != '-') fail("bad option " + shown(option) + ": options are -fieldname");
    if (!call.self || !call.self->info) fail("not a native object");
    const auto* field = find_field(call.self->info, spelled.substr(1));
    if (!field) {
        std::vector<const FieldEntry*> fields;
        all_fields(call.self->info, fields);
        std::string names;
        for (const auto* item : fields) names += (names.empty() ? "-" : ", -") + item->name;
        fail("unknown option " + shown(option) + (names.empty() ? std::string(": the class has no fields") : ": must be one of " + names));
    }
    return *field;
}
inline Tcl_Obj* cget_method(Call& call) {
    if (call.n != 1) wrong_args(call, "-field");
    return option_field(call, call.a[0]).get(call.self);
}
inline Tcl_Obj* configure_method(Call& call) {
    if (!call.self || !call.self->info) fail("not a native object");
    if (call.n == 0) {
        std::vector<const FieldEntry*> fields;
        all_fields(call.self->info, fields);
        Tcl_Obj* out = new_list();
        for (const auto* field : fields) { push(out, text("-" + field->name)); push(out, field->get(call.self)); }
        return out;
    }
    if (call.n == 1) return option_field(call, call.a[0]).get(call.self);
    if (call.n % 2 != 0) wrong_args(call, "?-field value ...?");
    for (Size i = 0; i < call.n; i += 2) {
        const auto& field = option_field(call, call.a[i]);
        if (!field.set) fail("field -" + field.name + " is read-only");
    }
    for (Size i = 0; i < call.n; i += 2) option_field(call, call.a[i]).set(call.self, call.a[i + 1]);
    return nullptr;
}
inline Tcl_Obj* alive_method(Call& call) {
    if (call.n != 0) wrong_args(call, "");
    return Tcl_NewBooleanObj(call.self && call.self->ptr ? 1 : 0);
}
inline Tcl_Obj* owned_method(Call& call) {
    if (call.n != 0) wrong_args(call, "");
    return Tcl_NewBooleanObj(call.self && call.self->ptr && (owned(call.self) || call.self->shares) ? 1 : 0);
}
template<class T> Tcl_Obj* stream_method(Call& call) {
    std::ostringstream stream;
    stream << *native<T>(call.self);
    return text(stream.str());
}
template<class T> void define_enum(State& s, const std::string& name, bool flags, std::initializer_list<std::pair<const char*, long long>> members) {
    EnumInfo info;
    info.flags = flags;
    namespace_of(s, parent_namespace(name));
    for (const auto& [member, value] : members) {
        info.members.emplace_back(member, value);
        Tcl_Obj* number = Value<std::underlying_type_t<T>>::cast(static_cast<std::underlying_type_t<T>>(value));
        if (!Tcl_SetVar2Ex(s.interp, name.c_str(), member, number, TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG)) raise_pending(s.interp);
    }
    enum_infos()[std::type_index(typeid(T))] = std::move(info);
}
// Constants are namespace variables that refuse to change.
struct ConstantData { Tcl_Obj* value; };
inline char* constant_trace(void* data, Tcl_Interp* interp, const char* name1, const char* name2, int flags) {
    auto* constant = static_cast<ConstantData*>(data);
    if (Tcl_InterpDeleted(interp)) { Tcl_DecrRefCount(constant->value); delete constant; return nullptr; }
    if (flags & TCL_TRACE_UNSETS) {
        Tcl_SetVar2Ex(interp, name1, name2, constant->value, TCL_GLOBAL_ONLY);
        Tcl_TraceVar2(interp, name1, name2, TCL_GLOBAL_ONLY | TCL_TRACE_WRITES | TCL_TRACE_UNSETS, constant_trace, data);
        return nullptr;
    }
    Tcl_SetVar2Ex(interp, name1, name2, constant->value, TCL_GLOBAL_ONLY);
    return const_cast<char*>("is a constant");
}
inline void define_constant(State& s, const std::string& name, Tcl_Obj* value) {
    namespace_of(s, parent_namespace(name));
    Tcl_IncrRefCount(value);
    if (!Tcl_SetVar2Ex(s.interp, name.c_str(), nullptr, value, TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG)) { Tcl_DecrRefCount(value); raise_pending(s.interp); }
    Tcl_TraceVar2(s.interp, name.c_str(), nullptr, TCL_GLOBAL_ONLY | TCL_TRACE_WRITES | TCL_TRACE_UNSETS, constant_trace, new ConstantData{value});
}
inline void state_delete(void* data, Tcl_Interp*) {
    // Objects may still be torn down after this runs; the state stays allocated.
    static_cast<State*>(data)->deleted = true;
}
inline State* create_state(Tcl_Interp* interp, const char* code, const char* ns, const char* key) {
    auto* s = new State;
    s->interp = interp;
    s->code = code;
    s->ns = ns;
    Tcl_SetAssocData(interp, key, state_delete, s);
    return s;
}
// The module's runtime: the NativeObject root class with cget, configure, alive and owned.
inline void initialize_runtime(State& s) {
    namespace_of(s, s.ns);
    const auto root = s.ns + "::NativeObject";
    eval_words(s, {"::oo::class", "create", root});
    Tcl_Class cls = find_class(s, root);
    add_method(s, cls, "cget", cget_method);
    add_method(s, cls, "configure", configure_method);
    add_method(s, cls, "alive", alive_method);
    add_method(s, cls, "owned", owned_method);
}
} } // ROSETTA_TCL_MODULE::rosetta_tcl
)cpp"};
} // namespace rosetta::detail
