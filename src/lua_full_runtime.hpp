#pragma once

// Runtime for the full Lua backend (--lua-api full): emitted (as text) into every generated
// source, inside the module's namespace ROSETTA_LUA_MODULE so that split sources of one
// module share it and two modules never accept each other's objects.
//
// No Lua error (longjmp) may cross a live C++ object: conversions throw C++ exceptions, and
// boundary() raises the Lua error after the native frames are gone. Lua code called from C++
// (callbacks, overrides) runs under lua_pcall, and its errors come back as C++ exceptions.
namespace rosetta::detail {
inline constexpr const char* lua_full_runtime_parts[] = {
R"cpp(
namespace ROSETTA_LUA_MODULE { namespace rosetta_lua {
// ---------------------------------------------------------------- errors
[[noreturn]] inline void fail(const std::string& message) { throw std::runtime_error(message); }
// The argument being converted ("argument 'seed'"), named in conversion errors; null outside conversions.
inline const char*& converting() { static const char* label = nullptr; return label; }
// The Lua thread running the current native call: callbacks made during the call run on it.
inline lua_State*& active() { static thread_local lua_State* state = nullptr; return state; }

template<class F> int boundary(lua_State* L, F&& fn) {
    lua_State* outer = active();
    const char* outer_label = converting();
    active() = L;
    converting() = nullptr;
    bool failed = false;
    int results = 0;
    try { results = fn(); }
    catch (const std::exception& error) {
        failed = true;
        const std::string text = converting() ? std::string(converting()) + ": " + error.what() : std::string(error.what());
        lua_pushlstring(L, text.data(), text.size());
    } catch (...) { failed = true; lua_pushliteral(L, "unknown C++ exception"); }
    active() = outer;
    converting() = outer_label;
    if (failed) return lua_error(L);
    return results;
}
inline void reserve(lua_State* L, int slots) { if (!lua_checkstack(L, slots)) fail("Lua stack overflow"); }

// Registry keys (addresses unique to this module).
inline char& tag_key() { static char key; return key; }      // marks this module's object metatables
inline char& native_key() { static char key; return key; }   // marks this module's native class tables
inline char& selves_key() { static char key; return key; }   // weak table: director address -> Lua object
inline char& ref_key() { static char key; return key; }      // metatable of datamunge.ref() cells
inline char& callable_key() { static char key; return key; } // metatable of native callables
inline char& alive_key() { static char key; return key; }    // sentinel whose __gc marks the state closed

// Whether the Lua state (identified by its registry sentinel) is still open: values kept by
// C++ release their references only while it is.
struct StateLife { lua_State* main = nullptr; bool alive = true; };
inline std::shared_ptr<StateLife> life(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &alive_key());
    auto* holder = static_cast<std::shared_ptr<StateLife>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!holder) fail("native module is not initialized in this Lua state");
    return *holder;
}
// A Lua value kept alive by C++ (a registry reference).
struct Keeper {
    std::shared_ptr<StateLife> state;
    int ref = LUA_NOREF;
    Keeper(lua_State* L, int index) : state(life(L)) { lua_pushvalue(L, index); ref = luaL_ref(L, LUA_REGISTRYINDEX); }
    ~Keeper() { if (state->alive && ref != LUA_NOREF) luaL_unref(state->main, LUA_REGISTRYINDEX, ref); }
    Keeper(const Keeper&) = delete;
    Keeper& operator=(const Keeper&) = delete;
    // The thread to call on: the one running the current native call, else the main thread.
    lua_State* thread() const {
        if (!state->alive) fail("the Lua state of a callback was closed");
        return active() ? active() : state->main;
    }
    void push(lua_State* L) const { lua_rawgeti(L, LUA_REGISTRYINDEX, ref); }
};

inline std::string type_name(lua_State* L, int index) {
    if (lua_getmetatable(L, index)) {
        lua_getfield(L, -1, "__name");
        std::string name = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
        lua_pop(L, 2);
        if (!name.empty()) return name;
    }
    if (lua_type(L, index) == LUA_TNUMBER) return lua_isinteger(L, index) ? "integer" : "number";
    return luaL_typename(L, index);
}

// ---------------------------------------------------------------- classes
struct TypeInfo;
struct BaseLink { TypeInfo* base; void* (*upcast)(void*); };
struct TypeInfo {
    const char* name = nullptr; // Lua class name
    std::vector<BaseLink> bases;
    void (*destroy)(void*) = nullptr;
};
template<class T> struct Info { static TypeInfo value; };
template<class T> TypeInfo Info<T>::value;
template<class T> struct Bound : std::false_type {};
template<class T> inline constexpr bool bound_v = Bound<std::remove_cv_t<T>>::value;
template<class T> struct Copyable : std::bool_constant<std::is_copy_constructible_v<T> && !std::is_abstract_v<T>> {};
template<class T> inline constexpr bool copyable_v = Copyable<std::remove_cv_t<T>>::value;
inline std::map<std::type_index, TypeInfo*>& registry() { static std::map<std::type_index, TypeInfo*> value; return value; }
template<class T> void register_type(const char* name) {
    auto& info = Info<T>::value;
    info.name = name;
    if constexpr (std::is_destructible_v<T>) info.destroy = [](void* p) { delete static_cast<T*>(p); };
    registry()[std::type_index(typeid(T))] = &info;
}
template<class Derived, class Base> void add_base() {
    Info<Derived>::value.bases.push_back({&Info<Base>::value, [](void* ptr) -> void* {
        return static_cast<Base*>(static_cast<Derived*>(ptr));
    }});
}

// The native side of a Lua object: the full userdata's memory. User value 1 keeps a parent
// alive (borrowed results); user value 2 is the instance table of Lua subclass objects.
struct Holder {
    void* ptr = nullptr;
    TypeInfo* info = nullptr;
    bool owned = false;
    std::shared_ptr<void>* shared = nullptr;
    bool director = false; // a Lua subclass instance implemented through a generated director
};
inline void release(Holder* holder) noexcept {
    if (!holder) return;
    if (holder->shared) { delete holder->shared; holder->shared = nullptr; }
    else if (holder->owned && holder->ptr && holder->info && holder->info->destroy) holder->info->destroy(holder->ptr);
    holder->ptr = nullptr;
    holder->owned = false;
}
inline Holder* holder_at(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index)) return nullptr;
    lua_rawgetp(L, -1, &tag_key());
    const bool ours = lua_toboolean(L, -1);
    lua_pop(L, 2);
    return ours ? static_cast<Holder*>(lua_touserdata(L, index)) : nullptr;
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
template<class T> bool is_instance(lua_State* L, int index) {
    auto* holder = holder_at(L, index);
    return holder && holder->info && derives(holder->info, &Info<std::remove_cv_t<T>>::value);
}
template<class T> T* native(lua_State* L, int index) {
    using U = std::remove_cv_t<T>;
    auto* info = &Info<U>::value;
    auto* holder = holder_at(L, index);
    if (!holder) fail(std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + type_name(L, index));
    if (!holder->ptr) fail("native object is closed");
    void* result = cast_path(holder->ptr, holder->info, info);
    if (!result) fail(std::string("expected ") + (info->name ? info->name : "a native object") + ", got " + (holder->info && holder->info->name ? holder->info->name : "a native object"));
    return static_cast<U*>(result);
}

// Base of generated director classes: C++ subclasses forwarding virtual calls to Lua overrides.
// The Lua object owns its director; once C++ takes ownership, the director keeps the Lua object alive.
struct Director {
    std::shared_ptr<StateLife> rosetta_state;
    int rosetta_self_ref = LUA_NOREF; // strong reference while C++ owns the object
    Director() = default;
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;
    virtual ~Director() {
        if (!rosetta_state || !rosetta_state->alive) return;
        lua_State* L = rosetta_state->main;
        if (rosetta_self_ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, rosetta_self_ref);
            if (auto* holder = static_cast<Holder*>(lua_touserdata(L, -1))) { holder->ptr = nullptr; holder->owned = false; }
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, rosetta_self_ref);
        }
    }
    // Pushes the Lua object implementing this director; false (nothing pushed) when it is gone.
    bool rosetta_push_self(lua_State* L) const {
        if (!rosetta_state || !rosetta_state->alive) return false;
        if (rosetta_self_ref != LUA_NOREF) { lua_rawgeti(L, LUA_REGISTRYINDEX, rosetta_self_ref); return true; }
        lua_rawgetp(L, LUA_REGISTRYINDEX, &selves_key());
        lua_rawgetp(L, -1, static_cast<const void*>(this));
        lua_remove(L, -2);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }
        return true;
    }
    bool rosetta_has_self() const { return rosetta_state && rosetta_state->alive; }
    lua_State* rosetta_thread() const {
        if (!rosetta_state || !rosetta_state->alive) fail("the Lua state of an object was closed");
        return active() ? active() : rosetta_state->main;
    }
    // Pushes the Lua override of a virtual method, then the object; false (nothing pushed) when
    // the native implementation applies. Lookup covers the instance table and the Lua subclass
    // chain, and stops at the native class table.
    bool rosetta_override(lua_State* L, const char* name) const {
        reserve(L, 8);
        const int base = lua_gettop(L);
        if (!rosetta_push_self(L)) return false;
        const int self = base + 1;
        if (lua_getiuservalue(L, self, 2) != LUA_TTABLE) { lua_settop(L, base); return false; }
        // The instance table itself, then each class reached through metatable __index tables.
        for (int depth = 0; depth < 64 && lua_istable(L, -1); ++depth) {
            lua_rawgetp(L, -1, &native_key());
            const bool native_class = lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (native_class) break;
            lua_pushstring(L, name);
            lua_rawget(L, -2);
            if (lua_isfunction(L, -1)) {
                lua_replace(L, self + 1);  // the function over the table
                lua_settop(L, self + 1);
                lua_insert(L, self);       // function, self
                return true;
            }
            lua_pop(L, 1);
            if (!lua_getmetatable(L, -1)) break;
            lua_getfield(L, -1, "__index");
            lua_replace(L, self + 1);
            lua_settop(L, self + 1);
        }
        lua_settop(L, base);
        return false;
    }
};
inline bool is_director(lua_State* L, int index) {
    auto* holder = holder_at(L, index);
    return holder && holder->director;
}
[[noreturn]] inline void pure_virtual(const char* name) {
    fail(std::string("pure virtual method ") + name + " is not implemented by the Lua subclass");
}

enum class Ownership { borrowed, owned };
// Pushes a new Lua object for ptr; keepalive (a stack index, or 0) stays reachable from it.
inline void push_pointer(lua_State* L, void* ptr, TypeInfo* info, Ownership ownership, int keepalive) {
    reserve(L, 4);
    if (keepalive) keepalive = lua_absindex(L, keepalive);
    lua_rawgetp(L, LUA_REGISTRYINDEX, info);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail(std::string("class ") + (info->name ? info->name : "?") + " is not loaded in this Lua state");
    }
    auto* holder = static_cast<Holder*>(lua_newuserdatauv(L, sizeof(Holder), 2));
    new (holder) Holder{ptr, info, ownership == Ownership::owned, nullptr, false};
    lua_insert(L, -2);
    lua_setmetatable(L, -2);
    if (keepalive) { lua_pushvalue(L, keepalive); lua_setiuservalue(L, -2, 1); }
}
template<class T> void wrap(lua_State* L, T* value, Ownership ownership, int keepalive) {
    using U = std::remove_cv_t<T>;
    if (!value) { lua_pushnil(L); return; }
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value);
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value)) {
            if (director->rosetta_push_self(L)) {
                if (ownership == Ownership::owned) {
                    // C++ handed back ownership of a Lua-implemented object.
                    auto* self = const_cast<Director*>(director);
                    auto* holder = holder_at(L, -1);
                    if (holder && holder->ptr && !holder->owned && self->rosetta_self_ref != LUA_NOREF) {
                        holder->owned = true;
                        luaL_unref(L, LUA_REGISTRYINDEX, self->rosetta_self_ref);
                        self->rosetta_self_ref = LUA_NOREF;
                    }
                }
                return;
            }
        }
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value));
        }
    }
    if (!info->name) {
        if (ownership == Ownership::owned && info->destroy) info->destroy(ptr);
        fail("C++ result type is not exported to Lua");
    }
    push_pointer(L, ptr, info, ownership, keepalive);
}
template<class T> void wrap_shared(lua_State* L, std::shared_ptr<T> value) {
    if (!value) { lua_pushnil(L); return; }
    using U = std::remove_cv_t<T>;
    TypeInfo* info = &Info<U>::value;
    void* ptr = const_cast<U*>(value.get());
    if constexpr (std::is_polymorphic_v<U>) {
        if (auto* director = dynamic_cast<const Director*>(value.get()); director && director->rosetta_push_self(L)) return;
        const auto found = registry().find(std::type_index(typeid(*value)));
        if (found != registry().end() && found->second != info) {
            info = found->second;
            ptr = const_cast<void*>(dynamic_cast<const void*>(value.get()));
        }
    }
    if (!info->name) fail("C++ result type is not exported to Lua");
    push_pointer(L, ptr, info, Ownership::borrowed, 0);
    holder_at(L, -1)->shared = new std::shared_ptr<void>(std::const_pointer_cast<U>(value), ptr);
}
template<class T> std::shared_ptr<T> shared(lua_State* L, int index) {
    if (lua_isnil(L, index)) return {};
    T* ptr = native<T>(L, index);
    auto* holder = holder_at(L, index);
    // A borrowed object (or a director, owned by its Lua object) stays alive through a Lua reference.
    if (holder->director && !holder->shared) return std::shared_ptr<T>(std::make_shared<Keeper>(L, index), ptr);
    if (!holder->shared) {
        if (!holder->owned) return std::shared_ptr<T>(std::make_shared<Keeper>(L, index), ptr);
        auto* info = holder->info;
        holder->shared = new std::shared_ptr<void>(holder->ptr, [info](void* p) { if (info->destroy) info->destroy(p); });
        holder->owned = false;
    }
    return std::shared_ptr<T>(*holder->shared, ptr);
}
template<class T> std::unique_ptr<T> take(lua_State* L, int index) {
    if (lua_isnil(L, index)) return {};
    T* ptr = native<T>(L, index);
    auto* holder = holder_at(L, index);
    if (holder->shared || !holder->owned) fail("cannot transfer ownership of a shared or borrowed native object");
    if (holder->info != &Info<std::remove_cv_t<T>>::value && !std::has_virtual_destructor_v<T>)
        fail("cannot transfer a derived object without a virtual destructor");
    holder->owned = false;
    bool kept = false;
    if constexpr (std::is_polymorphic_v<T>) {
        if (auto* director = dynamic_cast<Director*>(ptr); director && holder->director) {
            // The C++ owner now keeps the Lua implementation alive; the Lua object stays usable.
            lua_pushvalue(L, index);
            director->rosetta_self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            kept = true;
        }
    }
    if (!kept) holder->ptr = nullptr;
    return std::unique_ptr<T>(ptr);
}
)cpp",
R"cpp(
// ---------------------------------------------------------------- values
template<class T, class = void> struct Value;

// Strict checks (the first overload pass) need the exact Lua type: integers for C++ integers,
// floats for C++ floating point. Lenient checks also accept whole floats and integers.
template<class T> bool fits(lua_State* L, int index, bool strict) {
    if (lua_type(L, index) != LUA_TNUMBER) return false;
    if (lua_isinteger(L, index)) {
        const lua_Integer n = lua_tointeger(L, index);
        if constexpr (std::is_unsigned_v<T>) return n >= 0 && static_cast<unsigned long long>(n) <= std::numeric_limits<T>::max();
        else return n >= static_cast<lua_Integer>(std::numeric_limits<T>::lowest()) && n <= static_cast<lua_Integer>(std::numeric_limits<T>::max());
    }
    if (strict) return false;
    const double n = lua_tonumber(L, index);
    if (!std::isfinite(n) || std::trunc(n) != n) return false;
    const long double wide = n;
    return wide >= static_cast<long double>(std::numeric_limits<T>::lowest()) && wide <= static_cast<long double>(std::numeric_limits<T>::max());
}
template<class T> void push_number(lua_State* L, T value) {
    if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(lua_Integer)) {
            if (value > static_cast<unsigned long long>(std::numeric_limits<lua_Integer>::max())) { lua_pushnumber(L, static_cast<lua_Number>(value)); return; }
        }
        lua_pushinteger(L, static_cast<lua_Integer>(value));
    } else lua_pushnumber(L, static_cast<lua_Number>(value));
}

template<class T> struct Value<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    static bool check(lua_State* L, int index, bool) { return lua_type(L, index) == LUA_TBOOLEAN; }
    static bool load(lua_State* L, int index) {
        if (lua_type(L, index) != LUA_TBOOLEAN) fail("expected a boolean, got " + type_name(L, index));
        return lua_toboolean(L, index) != 0;
    }
    static void push(lua_State* L, bool value) { lua_pushboolean(L, value); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>> {
    static bool check(lua_State* L, int index, bool strict) { return fits<T>(L, index, strict); }
    static T load(lua_State* L, int index) {
        if (lua_type(L, index) != LUA_TNUMBER) fail("expected an integer, got " + type_name(L, index));
        if (!fits<T>(L, index, false)) {
            if (lua_isinteger(L, index)) fail("integer outside C++ type range");
            fail("expected an integer, got a number with a fraction or outside C++ type range");
        }
        return lua_isinteger(L, index) ? static_cast<T>(lua_tointeger(L, index)) : static_cast<T>(lua_tonumber(L, index));
    }
    static void push(lua_State* L, T value) { push_number(L, value); }
};
template<class T> struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool check(lua_State* L, int index, bool strict) {
        return lua_type(L, index) == LUA_TNUMBER && (!strict || !lua_isinteger(L, index));
    }
    static T load(lua_State* L, int index) {
        if (lua_type(L, index) != LUA_TNUMBER) fail("expected a number, got " + type_name(L, index));
        const double n = lua_tonumber(L, index);
        if (std::isfinite(n) && std::abs(n) > static_cast<double>(std::numeric_limits<T>::max())) fail("number outside C++ type range");
        return static_cast<T>(n);
    }
    static void push(lua_State* L, T value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
};
inline std::string text(lua_State* L, int index) {
    std::size_t size = 0;
    const char* data = lua_tolstring(L, index, &size);
    return std::string(data, size);
}
template<> struct Value<char> {
    static bool check(lua_State* L, int index, bool strict) {
        if (lua_type(L, index) == LUA_TSTRING) return lua_rawlen(L, index) == 1;
        return !strict && Value<int>::check(L, index, false);
    }
    static char load(lua_State* L, int index) {
        if (lua_type(L, index) == LUA_TSTRING) {
            if (lua_rawlen(L, index) != 1) fail("expected a single character");
            return lua_tostring(L, index)[0];
        }
        return static_cast<char>(Value<int>::load(L, index));
    }
    static void push(lua_State* L, char value) { lua_pushlstring(L, &value, 1); }
};
template<> struct Value<std::string> {
    static bool check(lua_State* L, int index, bool) { return lua_type(L, index) == LUA_TSTRING; }
    static std::string load(lua_State* L, int index) {
        if (lua_type(L, index) != LUA_TSTRING) fail("expected a string, got " + type_name(L, index));
        return text(L, index);
    }
    static void push(lua_State* L, const std::string& value) { lua_pushlstring(L, value.data(), value.size()); }
};
template<> struct Value<std::string_view> {
    static bool check(lua_State* L, int index, bool strict) { return Value<std::string>::check(L, index, strict); }
    static void push(lua_State* L, std::string_view value) { lua_pushlstring(L, value.data(), value.size()); }
};
template<> struct Value<const char*> {
    static void push(lua_State* L, const char* value) { if (value) lua_pushstring(L, value); else lua_pushnil(L); }
};
template<> struct Value<char*> {
    static void push(lua_State* L, const char* value) { Value<const char*>::push(L, value); }
};
template<> struct Value<std::filesystem::path> {
    static bool check(lua_State* L, int index, bool) { return lua_type(L, index) == LUA_TSTRING; }
    static std::filesystem::path load(lua_State* L, int index) {
        const std::string value = Value<std::string>::load(L, index);
#if defined(__cpp_char8_t)
        return std::filesystem::path(std::u8string(value.begin(), value.end()));
#else
        return std::filesystem::u8path(value);
#endif
    }
    static void push(lua_State* L, const std::filesystem::path& value) {
        const auto utf8 = value.u8string();
        lua_pushlstring(L, reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
};
template<> struct Value<std::nullptr_t> {
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index); }
    static std::nullptr_t load(lua_State*, int) { return nullptr; }
    static void push(lua_State* L, std::nullptr_t) { lua_pushnil(L); }
};
template<> struct Value<std::monostate> {
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index); }
    static std::monostate load(lua_State*, int) { return {}; }
    static void push(lua_State* L, std::monostate) { lua_pushnil(L); }
};
// Complex numbers are {re, im} tables (numbers are accepted on input).
template<class T> struct Value<std::complex<T>> {
    static bool check(lua_State* L, int index, bool strict) {
        if (lua_istable(L, index)) return lua_rawlen(L, index) == 2;
        return !strict && lua_type(L, index) == LUA_TNUMBER;
    }
    static std::complex<T> load(lua_State* L, int index) {
        if (lua_type(L, index) == LUA_TNUMBER) return {static_cast<T>(lua_tonumber(L, index)), T{}};
        if (!lua_istable(L, index) || lua_rawlen(L, index) != 2) fail("expected a number or a {re, im} table");
        lua_rawgeti(L, index, 1); lua_rawgeti(L, index, 2);
        const bool ok = lua_type(L, -2) == LUA_TNUMBER && lua_type(L, -1) == LUA_TNUMBER;
        const std::complex<T> result{static_cast<T>(lua_tonumber(L, -2)), static_cast<T>(lua_tonumber(L, -1))};
        lua_pop(L, 2);
        if (!ok) fail("expected a {re, im} table of numbers");
        return result;
    }
    static void push(lua_State* L, const std::complex<T>& value) {
        lua_createtable(L, 2, 0);
        lua_pushnumber(L, static_cast<lua_Number>(value.real())); lua_rawseti(L, -2, 1);
        lua_pushnumber(L, static_cast<lua_Number>(value.imag())); lua_rawseti(L, -2, 2);
    }
};

// Enumerations cross as integers; enumerator names (strings) are accepted on input.
template<class E> struct EnumTable { static inline std::vector<std::pair<std::string, long long>> entries; };
template<class T> struct Value<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    static bool check(lua_State* L, int index, bool strict) {
        if (lua_type(L, index) == LUA_TSTRING) {
            // Loosely any name matches, so that a sole overload reports the unknown enumerator.
            if (!strict) return true;
            const auto name = text(L, index);
            for (const auto& entry : EnumTable<T>::entries) if (entry.first == name) return true;
            return false;
        }
        if (!Value<U>::check(L, index, false)) return false;
        if (!strict || EnumTable<T>::entries.empty()) return true;
        const auto n = static_cast<long long>(Value<U>::load(L, index));
        for (const auto& entry : EnumTable<T>::entries) if (entry.second == n) return true;
        return false;
    }
    static T load(lua_State* L, int index) {
        if (lua_type(L, index) == LUA_TSTRING) {
            const auto name = text(L, index);
            for (const auto& entry : EnumTable<T>::entries) if (entry.first == name) return static_cast<T>(entry.second);
            std::string names;
            for (const auto& entry : EnumTable<T>::entries) names += (names.empty() ? "" : ", ") + entry.first;
            fail("unknown enumerator '" + name + "' (expected one of " + names + ")");
        }
        return static_cast<T>(Value<U>::load(L, index));
    }
    static void push(lua_State* L, T value) { push_number(L, static_cast<U>(value)); }
};

// Exported classes by value: copies in, owned copies out.
template<class T> struct Value<T, std::enable_if_t<bound_v<T>>> {
    static bool check(lua_State* L, int index, bool) { return is_instance<T>(L, index); }
    static T load(lua_State* L, int index) {
        if constexpr (copyable_v<T>) return *native<T>(L, index);
        else { (void)L; (void)index; fail("C++ class is not copyable"); }
    }
    static void push(lua_State* L, T&& value) {
        if constexpr (std::is_move_constructible_v<T>) wrap(L, new T(std::move(value)), Ownership::owned, 0);
        else { (void)value; fail("C++ class is not movable"); }
    }
    static void push(lua_State* L, const T& value) {
        if constexpr (copyable_v<T>) wrap(L, new T(value), Ownership::owned, 0);
        else { (void)value; fail("C++ class is not copyable"); }
    }
};
template<class T> struct Value<T*, std::enable_if_t<bound_v<T>>> {
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index) || is_instance<T>(L, index); }
    static T* load(lua_State* L, int index) { return lua_isnil(L, index) ? nullptr : native<T>(L, index); }
    static void push(lua_State* L, T* value) { wrap(L, value, Ownership::borrowed, 0); }
};
template<class T> struct Value<std::shared_ptr<T>, std::enable_if_t<bound_v<T>>> {
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index) || is_instance<T>(L, index); }
    static std::shared_ptr<T> load(lua_State* L, int index) { return shared<T>(L, index); }
    static void push(lua_State* L, const std::shared_ptr<T>& value) { wrap_shared(L, value); }
};
template<class T, class D> struct Value<std::unique_ptr<T, D>, std::enable_if_t<bound_v<T>>> {
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index) || is_instance<T>(L, index); }
    static std::unique_ptr<T, D> load(lua_State* L, int index) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) return take<T>(L, index);
        else fail("unique_ptr with a custom deleter cannot take ownership");
    }
    static void push(lua_State* L, std::unique_ptr<T, D>&& value) {
        if constexpr (std::is_same_v<D, std::default_delete<T>>) wrap(L, value.release(), Ownership::owned, 0);
        else fail("unique_ptr with a custom deleter cannot be returned");
    }
};
template<class T> struct Value<std::optional<T>> {
    static bool check(lua_State* L, int index, bool strict) { return lua_isnil(L, index) || Value<T>::check(L, index, strict); }
    static std::optional<T> load(lua_State* L, int index) {
        if (lua_isnil(L, index)) return std::nullopt;
        return Value<T>::load(L, index);
    }
    static void push(lua_State* L, const std::optional<T>& value) { if (value) Value<T>::push(L, *value); else lua_pushnil(L); }
    static void push(lua_State* L, std::optional<T>&& value) { if (value) Value<T>::push(L, std::move(*value)); else lua_pushnil(L); }
};
template<class T> struct Value<std::reference_wrapper<T>> {
    static void push(lua_State* L, std::reference_wrapper<T> value) {
        using U = std::remove_cv_t<T>;
        if constexpr (bound_v<U>) wrap(L, const_cast<U*>(&value.get()), Ownership::borrowed, 0);
        else Value<U>::push(L, value.get());
    }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- tables
// Sequences are Lua arrays (1-based); maps are tables keyed by the C++ keys. nil is an empty
// container when checks are lenient.
template<class E> bool check_elements(lua_State* L, int index, bool strict) {
    if (lua_isnil(L, index)) return !strict;
    if (!lua_istable(L, index) || holder_at(L, index)) return false;
    index = lua_absindex(L, index);
    reserve(L, 2);
    const auto n = static_cast<lua_Integer>(lua_rawlen(L, index));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, index, i);
        const bool ok = Value<E>::check(L, -1, strict);
        lua_pop(L, 1);
        if (!ok) return false;
    }
    return true;
}
template<class E> E load_element(lua_State* L, int index, lua_Integer i) {
    lua_rawgeti(L, index, i);
    struct Pop { lua_State* L; ~Pop() { lua_pop(L, 1); } } pop{L};
    return Value<E>::load(L, -1);
}
template<class E, class Range> void push_elements(lua_State* L, const Range& range, std::size_t size) {
    reserve(L, 3);
    lua_createtable(L, size > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? 0 : static_cast<int>(size), 0);
    lua_Integer i = 0;
    for (const auto& item : range) { Value<E>::push(L, item); lua_rawseti(L, -2, ++i); }
}
template<class C, class E> struct Sequence {
    static bool check(lua_State* L, int index, bool strict) { return check_elements<E>(L, index, strict); }
    static C load(lua_State* L, int index) {
        C result;
        if (lua_isnil(L, index)) return result;
        if (!lua_istable(L, index)) fail("expected a table (array), got " + type_name(L, index));
        index = lua_absindex(L, index);
        reserve(L, 2);
        const auto n = static_cast<lua_Integer>(lua_rawlen(L, index));
        if constexpr (std::is_same_v<C, std::vector<E>>) result.reserve(static_cast<std::size_t>(n));
        for (lua_Integer i = 1; i <= n; ++i) {
            if constexpr (std::is_same_v<C, std::set<E>> || std::is_same_v<C, std::unordered_set<E>>) result.insert(load_element<E>(L, index, i));
            else result.push_back(load_element<E>(L, index, i));
        }
        return result;
    }
    static void push(lua_State* L, const C& value) { push_elements<E>(L, value, value.size()); }
};
template<class E, class A> struct Value<std::vector<E, A>> : Sequence<std::vector<E, A>, E> {};
template<class A> struct Value<std::vector<bool, A>> {
    static bool check(lua_State* L, int index, bool strict) { return check_elements<bool>(L, index, strict); }
    static std::vector<bool, A> load(lua_State* L, int index) {
        std::vector<bool, A> result;
        if (lua_isnil(L, index)) return result;
        if (!lua_istable(L, index)) fail("expected a table (array), got " + type_name(L, index));
        index = lua_absindex(L, index);
        const auto n = static_cast<lua_Integer>(lua_rawlen(L, index));
        for (lua_Integer i = 1; i <= n; ++i) result.push_back(load_element<bool>(L, index, i));
        return result;
    }
    static void push(lua_State* L, const std::vector<bool, A>& value) {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        for (std::size_t i = 0; i < value.size(); ++i) { lua_pushboolean(L, value[i]); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); }
    }
};
template<class E, class A> struct Value<std::deque<E, A>> : Sequence<std::deque<E, A>, E> {};
template<class E, class A> struct Value<std::list<E, A>> : Sequence<std::list<E, A>, E> {};
template<class E> struct Value<std::set<E>> : Sequence<std::set<E>, E> {};
template<class E> struct Value<std::unordered_set<E>> : Sequence<std::unordered_set<E>, E> {};
template<class E> struct Value<std::valarray<E>> {
    static bool check(lua_State* L, int index, bool strict) { return check_elements<E>(L, index, strict); }
    static std::valarray<E> load(lua_State* L, int index) {
        const auto items = Value<std::vector<E>>::load(L, index);
        return std::valarray<E>(items.data(), items.size());
    }
    static void push(lua_State* L, const std::valarray<E>& value) { push_elements<E>(L, std::vector<E>(std::begin(value), std::end(value)), value.size()); }
};
template<class E, std::size_t N> struct Value<std::array<E, N>> {
    static bool check(lua_State* L, int index, bool strict) {
        if (!lua_istable(L, index) || lua_rawlen(L, index) != N) return N == 0 && lua_isnil(L, index);
        return check_elements<E>(L, index, strict);
    }
    static std::array<E, N> load(lua_State* L, int index) {
        if (!lua_istable(L, index) || lua_rawlen(L, index) != N) fail("expected a table of " + std::to_string(N) + " elements");
        index = lua_absindex(L, index);
        std::array<E, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = load_element<E>(L, index, static_cast<lua_Integer>(i + 1));
        return result;
    }
    static void push(lua_State* L, const std::array<E, N>& value) { push_elements<E>(L, value, N); }
};
template<class M, class K, class V> struct Mapping {
    static bool check(lua_State* L, int index, bool strict) {
        if (lua_isnil(L, index)) return !strict;
        if (!lua_istable(L, index) || holder_at(L, index)) return false;
        index = lua_absindex(L, index);
        reserve(L, 3);
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (!Value<K>::check(L, -2, false) || !Value<V>::check(L, -1, false)) { lua_pop(L, 2); return false; }
            lua_pop(L, 1);
        }
        return true;
    }
    static M load(lua_State* L, int index) {
        M result;
        if (lua_isnil(L, index)) return result;
        if (!lua_istable(L, index)) fail("expected a table, got " + type_name(L, index));
        index = lua_absindex(L, index);
        reserve(L, 3);
        lua_pushnil(L);
        while (lua_next(L, index)) {
            struct Pop { lua_State* L; ~Pop() { lua_pop(L, 1); } } pop{L};
            // lua_next needs the key unchanged: convert it from a copy.
            lua_pushvalue(L, -2);
            struct PopKey { lua_State* L; ~PopKey() { lua_pop(L, 1); } } pop_key{L};
            K key = Value<K>::load(L, -1);
            result.emplace(std::move(key), Value<V>::load(L, -2));
        }
        return result;
    }
    static void push(lua_State* L, const M& value) {
        reserve(L, 4);
        lua_createtable(L, 0, value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? 0 : static_cast<int>(value.size()));
        for (const auto& item : value) { Value<K>::push(L, item.first); Value<V>::push(L, item.second); lua_rawset(L, -3); }
    }
};
template<class K, class V, class C, class A> struct Value<std::map<K, V, C, A>> : Mapping<std::map<K, V, C, A>, K, V> {};
template<class K, class V, class H, class E, class A> struct Value<std::unordered_map<K, V, H, E, A>> : Mapping<std::unordered_map<K, V, H, E, A>, K, V> {};

// Pairs and tuples are arrays ({a, b}); pairs also accept {first = a, second = b}.
template<class... T> struct Product {
    static constexpr std::size_t N = sizeof...(T);
    template<std::size_t... I> static bool check_each(lua_State* L, int index, bool strict, std::index_sequence<I...>) {
        if (!lua_istable(L, index) || holder_at(L, index) || lua_rawlen(L, index) != N) return false;
        index = lua_absindex(L, index);
        bool ok = true;
        ((ok = ok && [&] { lua_rawgeti(L, index, static_cast<lua_Integer>(I + 1)); const bool good = Value<T>::check(L, -1, strict); lua_pop(L, 1); return good; }()), ...);
        return ok;
    }
    template<class Tuple, std::size_t... I> static Tuple load_each(lua_State* L, int index, std::index_sequence<I...>) {
        if (!lua_istable(L, index) || lua_rawlen(L, index) != N) fail("expected a table of " + std::to_string(N) + " elements");
        index = lua_absindex(L, index);
        return Tuple(load_element<T>(L, index, static_cast<lua_Integer>(I + 1))...);
    }
    template<class Tuple, std::size_t... I> static void push_each(lua_State* L, const Tuple& value, std::index_sequence<I...>) {
        reserve(L, 3);
        lua_createtable(L, static_cast<int>(N), 0);
        ((Value<T>::push(L, std::get<I>(value)), lua_rawseti(L, -2, static_cast<lua_Integer>(I + 1))), ...);
    }
};
template<class A, class B> struct Value<std::pair<A, B>> {
    using P = Product<A, B>;
    // {first = a, second = b} is rearranged into {a, b} on a copy.
    static bool named(lua_State* L, int index) {
        if (!lua_istable(L, index) || lua_rawlen(L, index) != 0) return false;
        lua_getfield(L, index, "first"); lua_getfield(L, index, "second");
        const bool both = !lua_isnil(L, -2) && !lua_isnil(L, -1);
        lua_pop(L, 2);
        return both;
    }
    static int arrayed(lua_State* L, int index) {
        index = lua_absindex(L, index);
        lua_createtable(L, 2, 0);
        lua_getfield(L, index, "first"); lua_rawseti(L, -2, 1);
        lua_getfield(L, index, "second"); lua_rawseti(L, -2, 2);
        return lua_gettop(L);
    }
    static bool check(lua_State* L, int index, bool strict) {
        if (named(L, index)) { reserve(L, 3); const int copy = arrayed(L, index); const bool ok = P::check_each(L, copy, strict, std::index_sequence_for<A, B>{}); lua_pop(L, 1); return ok; }
        return P::check_each(L, index, strict, std::index_sequence_for<A, B>{});
    }
    static std::pair<A, B> load(lua_State* L, int index) {
        if (named(L, index)) {
            reserve(L, 3);
            const int copy = arrayed(L, index);
            struct Pop { lua_State* L; ~Pop() { lua_pop(L, 1); } } pop{L};
            return P::template load_each<std::pair<A, B>>(L, copy, std::index_sequence_for<A, B>{});
        }
        return P::template load_each<std::pair<A, B>>(L, index, std::index_sequence_for<A, B>{});
    }
    static void push(lua_State* L, const std::pair<A, B>& value) { P::push_each(L, value, std::index_sequence_for<A, B>{}); }
};
template<class... T> struct Value<std::tuple<T...>> {
    using P = Product<T...>;
    static bool check(lua_State* L, int index, bool strict) { return P::check_each(L, index, strict, std::index_sequence_for<T...>{}); }
    static std::tuple<T...> load(lua_State* L, int index) { return P::template load_each<std::tuple<T...>>(L, index, std::index_sequence_for<T...>{}); }
    static void push(lua_State* L, const std::tuple<T...>& value) { P::push_each(L, value, std::index_sequence_for<T...>{}); }
};
template<class... T> struct Value<std::variant<T...>> {
    static bool check(lua_State* L, int index, bool strict) { return (Value<T>::check(L, index, strict) || ...); }
    static std::variant<T...> load(lua_State* L, int index) {
        std::optional<std::variant<T...>> result;
        for (const bool strict : {true, false}) {
            ((!result && Value<T>::check(L, index, strict) ? (void)result.emplace(std::in_place_type<T>, Value<T>::load(L, index)) : (void)0), ...);
            if (result) return std::move(*result);
        }
        fail("value matches no alternative of the C++ variant");
    }
    static void push(lua_State* L, const std::variant<T...>& value) {
        std::visit([L](const auto& item) { Value<std::remove_cv_t<std::remove_reference_t<decltype(item)>>>::push(L, item); }, value);
    }
};
template<class Rep, class Period> struct Value<std::chrono::duration<Rep, Period>> {
    using D = std::chrono::duration<Rep, Period>;
    static bool check(lua_State* L, int index, bool) { return lua_type(L, index) == LUA_TNUMBER; }
    static D load(lua_State* L, int index) { return std::chrono::duration_cast<D>(std::chrono::duration<double>(Value<double>::load(L, index))); }
    static void push(lua_State* L, const D& value) { lua_pushnumber(L, std::chrono::duration<double>(value).count()); }
};
)cpp",
R"cpp(
// ---------------------------------------------------------------- arguments
// A ref() cell: a table whose `value` a mutable reference parameter reads and updates.
inline bool is_ref(lua_State* L, int index) {
    if (!lua_istable(L, index) || !lua_getmetatable(L, index)) return false;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &ref_key());
    const bool same = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return same;
}
// The value a parameter converts from: a ref() cell's value pushed on the stack, or the argument.
struct Source {
    lua_State* L;
    int index;
    bool pushed = false;
    Source(lua_State* state, int argument, bool out) : L(state), index(argument) {
        if (out && is_ref(L, argument)) { reserve(L, 2); lua_getfield(L, argument, "value"); index = lua_gettop(L); pushed = true; }
    }
    ~Source() { if (pushed) lua_remove(L, index); }
};
template<class V, class = void> struct pushable : std::false_type {};
template<class V> struct pushable<V, std::void_t<decltype(Value<V>::push(std::declval<lua_State*>(), std::declval<const V&>()))>> : std::true_type {};
template<class P, class = void> struct Param {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static constexpr bool out = std::is_lvalue_reference_v<P> && !std::is_const_v<std::remove_reference_t<P>>;
    static bool check(lua_State* L, int index, bool strict) { Source source(L, index, out); return Value<V>::check(L, source.index, strict); }
    Param(lua_State* L, int index) : value_([&] { Source source(L, index, out); return Value<V>::load(L, source.index); }()) {}
    decltype(auto) get() {
        if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(value_);
        else return static_cast<V&&>(value_);
    }
    // A mutable reference parameter passed a ref() cell stores the updated value back in it.
    void writeback(lua_State* L, int index) {
        if constexpr (out && pushable<V>::value) {
            if (!is_ref(L, index)) return;
            reserve(L, 2);
            Value<V>::push(L, value_);
            lua_setfield(L, index, "value");
        } else { (void)L; (void)index; }
    }
private:
    V value_;
};
template<class P> struct Param<P, std::enable_if_t<bound_v<std::remove_reference_t<P>> && !std::is_pointer_v<P>>> {
    using V = std::remove_cv_t<std::remove_reference_t<P>>;
    static bool check(lua_State* L, int index, bool) { return is_instance<V>(L, index); }
    Param(lua_State* L, int index) : ptr_(native<V>(L, index)) {}
    decltype(auto) get() {
        if constexpr (std::is_rvalue_reference_v<P>) {
            if constexpr (copyable_v<V>) { copy_.emplace(*ptr_); return static_cast<V&&>(*copy_); }
            else return static_cast<V&&>(*ptr_);
        } else if constexpr (std::is_lvalue_reference_v<P>) return static_cast<P>(*ptr_);
        else return static_cast<V&>(*ptr_);
    }
private:
    V* ptr_;
    std::conditional_t<std::is_rvalue_reference_v<P> && copyable_v<V>, std::optional<V>, char> copy_{};
};
template<class P> struct Param<P, std::enable_if_t<std::is_pointer_v<P> && bound_v<std::remove_pointer_t<P>>>> {
    using V = std::remove_cv_t<std::remove_pointer_t<P>>;
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index) || is_instance<V>(L, index); }
    Param(lua_State* L, int index) : ptr_(lua_isnil(L, index) ? nullptr : native<V>(L, index)) {}
    P get() { return ptr_; }
private:
    V* ptr_;
};
template<> struct Param<const char*> {
    static bool check(lua_State* L, int index, bool strict) { return lua_isnil(L, index) || Value<std::string>::check(L, index, strict); }
    Param(lua_State* L, int index) { if (!lua_isnil(L, index)) text_ = Value<std::string>::load(L, index); }
    const char* get() { return text_ ? text_->c_str() : nullptr; }
private:
    std::optional<std::string> text_;
};
template<class P> struct Param<P, std::enable_if_t<std::is_same_v<std::remove_cv_t<std::remove_reference_t<P>>, std::string_view>>> {
    static bool check(lua_State* L, int index, bool strict) { return Value<std::string>::check(L, index, strict); }
    Param(lua_State* L, int index) : text_(Value<std::string>::load(L, index)) {}
    std::string_view get() { return text_; }
private:
    std::string text_;
};

[[noreturn]] inline void no_overload(lua_State* L, const char* name, const char* signatures, int first) {
    std::string received;
    for (int i = first; i <= lua_gettop(L); ++i) {
        std::string type = type_name(L, i);
        if (lua_istable(L, i) && !holder_at(L, i)) type += "[" + std::to_string(static_cast<long long>(lua_rawlen(L, i))) + "]";
        received += std::string(i > first ? ", " : "") + type;
    }
    fail(std::string(name) + "(): incompatible arguments (" + received + "); supported signatures:\n" + signatures);
}

// ---------------------------------------------------------------- results
enum class Return { automatic, copy, owned, borrowed };
inline bool same_object(lua_State* L, int parent, void* ptr, TypeInfo* info) {
    auto* holder = parent ? holder_at(L, parent) : nullptr;
    return holder && holder->ptr && cast_path(holder->ptr, holder->info, info) == ptr;
}
// Pushes a C++ result; parent (a stack index, or 0) is the object a borrowed result lives in.
template<class R> void result(lua_State* L, R&& value, int parent, Return policy) {
    using Q = std::remove_reference_t<R>;
    using V = std::remove_cv_t<Q>;
    reserve(L, 4);
    if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        using B = std::remove_cv_t<std::remove_pointer_t<V>>;
        B* ptr = const_cast<B*>(value);
        if (!ptr) { lua_pushnil(L); return; }
        if (same_object(L, parent, ptr, &Info<B>::value)) { lua_pushvalue(L, parent); return; }
        if (policy == Return::owned) { wrap(L, ptr, Ownership::owned, 0); return; }
        if (policy == Return::copy) {
            if constexpr (copyable_v<B>) { wrap(L, new B(*ptr), Ownership::owned, 0); return; }
            else fail("C++ result is not copyable");
        }
        wrap(L, ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V> && std::is_lvalue_reference_v<R>) {
        V* ptr = const_cast<V*>(&value);
        if (same_object(L, parent, ptr, &Info<V>::value)) { lua_pushvalue(L, parent); return; }
        if (policy == Return::copy || policy == Return::owned) {
            if constexpr (copyable_v<V>) { wrap(L, new V(value), Ownership::owned, 0); return; }
            else fail("C++ result is not copyable");
        }
        wrap(L, ptr, Ownership::borrowed, parent);
    } else if constexpr (bound_v<V>) {
        Value<V>::push(L, std::move(value));
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        Value<const char*>::push(L, value);
    } else if constexpr (std::is_lvalue_reference_v<R>) {
        Value<V>::push(L, static_cast<const V&>(value));
    } else {
        Value<V>::push(L, std::move(value));
    }
}
template<class F> int call(lua_State* L, F&& function, int parent, Return policy) {
    using R = decltype(function());
    if constexpr (std::is_void_v<R>) { function(); return 0; }
    else { result<R>(L, function(), parent, policy); return 1; }
}
template<class T, class V> void assign_field(T& target, V&& value) {
    if constexpr (std::is_copy_assignable_v<T>) target = std::forward<V>(value);
    else fail("field type is not assignable");
}

// ---------------------------------------------------------------- C++ -> Lua calls
template<class A> void argument(lua_State* L, A&& value) {
    using V = std::remove_cv_t<std::remove_reference_t<A>>;
    if constexpr (bound_v<V>) {
        if constexpr (std::is_lvalue_reference_v<A>) wrap(L, const_cast<V*>(&value), Ownership::borrowed, 0);
        else Value<V>::push(L, std::move(value));
    } else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) {
        wrap(L, const_cast<std::remove_cv_t<std::remove_pointer_t<V>>*>(value), Ownership::borrowed, 0);
    } else if constexpr (std::is_same_v<V, char*> || std::is_same_v<V, const char*>) {
        Value<const char*>::push(L, value);
    } else {
        Value<V>::push(L, value);
    }
}
template<class R> R returned(lua_State* L, int index) {
    if constexpr (std::is_void_v<R>) { (void)L; (void)index; }
    else {
        using V = std::remove_cv_t<std::remove_reference_t<R>>;
        if constexpr (std::is_lvalue_reference_v<R> && bound_v<V>) return *native<V>(L, index);
        else if constexpr (std::is_pointer_v<V> && bound_v<std::remove_pointer_t<V>>) return Value<V>::load(L, index);
        else {
            if (!Value<V>::check(L, index, false)) fail("Lua callback returned " + type_name(L, index) + ", which does not convert to the C++ result type");
            return Value<V>::load(L, index);
        }
    }
}
inline int traceback(lua_State* L) {
    const char* message = lua_tostring(L, 1);
    if (!message) message = luaL_typename(L, 1);
    luaL_traceback(L, L, message, 1);
    return 1;
}
// Calls the function below `count` pushed arguments; Lua errors become C++ exceptions.
template<class R> R pcall(lua_State* L, int count) {
    reserve(L, 2);
    const int function = lua_gettop(L) - count;
    lua_pushcfunction(L, traceback);
    lua_insert(L, function);
    const int status = lua_pcall(L, count, std::is_void_v<R> ? 0 : 1, function);
    lua_remove(L, function);
    if (status != LUA_OK) {
        std::string message = lua_type(L, -1) == LUA_TSTRING ? text(L, -1) : "error in Lua callback";
        lua_pop(L, 1);
        fail(message);
    }
    if constexpr (std::is_void_v<R>) return;
    else {
        struct Pop { lua_State* L; ~Pop() { lua_pop(L, 1); } } pop{L};
        return returned<R>(L, -1);
    }
}
template<class R, class... A> R invoke_lua(lua_State* L, const Keeper& function, A&&... arguments) {
    const int top = lua_gettop(L);
    try {
        reserve(L, static_cast<int>(sizeof...(A)) + 4);
        function.push(L);
        (argument<A>(L, std::forward<A>(arguments)), ...);
    } catch (...) { lua_settop(L, top); throw; }
    return pcall<R>(L, static_cast<int>(sizeof...(A)));
}
// A director's override: the function and self are on the stack.
template<class R, class... A> R invoke_override(lua_State* L, A&&... arguments) {
    const int top = lua_gettop(L) - 2;
    try {
        reserve(L, static_cast<int>(sizeof...(A)) + 4);
        (argument<A>(L, std::forward<A>(arguments)), ...);
    } catch (...) { lua_settop(L, top); throw; }
    return pcall<R>(L, static_cast<int>(sizeof...(A)) + 1);
}
// A native std::function returned to Lua: a callable userdata.
struct Callable { std::function<int(lua_State*)> call; };
template<class R, class... A, std::size_t... I>
int invoke_native(lua_State* L, const std::function<R(A...)>& function, std::index_sequence<I...>) {
    if (lua_gettop(L) - 1 != static_cast<int>(sizeof...(A))) fail("wrong number of arguments for native callable");
    std::tuple<Param<A>...> params{Param<A>(L, static_cast<int>(I) + 2)...};
    return call(L, [&]() -> decltype(auto) { return function(std::get<I>(params).get()...); }, 0, Return::copy);
}
inline int callable_call(lua_State* L) {
    return boundary(L, [&] {
        auto* callable = static_cast<Callable*>(luaL_testudata(L, 1, ROSETTA_LUA_CALLABLE));
        if (!callable || !callable->call) fail("native callable is no longer available");
        return callable->call(L);
    });
}
inline int callable_gc(lua_State* L) {
    if (auto* callable = static_cast<Callable*>(luaL_testudata(L, 1, ROSETTA_LUA_CALLABLE))) callable->~Callable();
    return 0;
}
template<class R, class... A> struct Value<std::function<R(A...)>> {
    using F = std::function<R(A...)>;
    static bool check(lua_State* L, int index, bool) { return lua_isnil(L, index) || lua_isfunction(L, index) || luaL_testudata(L, index, ROSETTA_LUA_CALLABLE); }
    static F load(lua_State* L, int index) {
        if (lua_isnil(L, index)) return {};
        if (!lua_isfunction(L, index) && !luaL_testudata(L, index, ROSETTA_LUA_CALLABLE)) fail("expected a function, got " + type_name(L, index));
        auto target = std::make_shared<Keeper>(L, index);
        return [target](A... arguments) -> R { return invoke_lua<R, A...>(target->thread(), *target, std::forward<A>(arguments)...); };
    }
    static void push(lua_State* L, const F& value) {
        if (!value) { lua_pushnil(L); return; }
        reserve(L, 3);
        auto* callable = new (lua_newuserdatauv(L, sizeof(Callable), 0)) Callable{};
        callable->call = [value](lua_State* state) { return invoke_native(state, value, std::index_sequence_for<A...>{}); };
        luaL_setmetatable(L, ROSETTA_LUA_CALLABLE);
    }
};
} } // ROSETTA_LUA_MODULE::rosetta_lua
)cpp",
R"cpp(
namespace ROSETTA_LUA_MODULE { namespace rosetta_lua {
// ---------------------------------------------------------------- module setup
inline int object_gc(lua_State* L) {
    if (auto* holder = holder_at(L, 1)) release(holder);
    return 0;
}
inline int object_tostring(lua_State* L) {
    auto* holder = holder_at(L, 1);
    const char* name = holder && holder->info && holder->info->name ? holder->info->name : "object";
    if (holder && !holder->ptr) lua_pushfstring(L, "%s (closed)", name);
    else lua_pushfstring(L, "%s: %p", name, holder ? holder->ptr : nullptr);
    return 1;
}
// Objects are equal when they are the same native object (or by a bound operator==).
inline int object_eq(lua_State* L) {
    auto* a = holder_at(L, 1);
    auto* b = holder_at(L, 2);
    lua_pushboolean(L, a && b && a->ptr && a->ptr == b->ptr);
    return 1;
}
// obj.name: the Lua instance table (subclass objects), class members, fields, then obj[i].
// Upvalues: class table, getters table, index operator (or nil).
inline int object_index(lua_State* L) {
    if (lua_getiuservalue(L, 1, 2) == LUA_TTABLE) {
        lua_pushvalue(L, 2);
        if (lua_gettable(L, -2) != LUA_TNIL) return 1;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushvalue(L, 2);
    if (lua_rawget(L, lua_upvalueindex(1)) != LUA_TNIL) return 1;
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_pushvalue(L, 2);
        if (lua_rawget(L, lua_upvalueindex(2)) != LUA_TNIL) { lua_pushvalue(L, 1); lua_call(L, 1, 1); return 1; }
        lua_pop(L, 1);
    } else if (lua_type(L, 2) == LUA_TNUMBER && !lua_isnil(L, lua_upvalueindex(3))) {
        lua_pushvalue(L, lua_upvalueindex(3));
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        lua_call(L, 2, 1);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}
// obj.field = value through a setter; Lua subclass objects keep other values in their instance table.
// Upvalues: setters table, class name.
inline int object_newindex(lua_State* L) {
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_pushvalue(L, 2);
        if (lua_rawget(L, lua_upvalueindex(1)) != LUA_TNIL) {
            lua_pushvalue(L, 1);
            lua_pushvalue(L, 3);
            lua_call(L, 2, 0);
            return 0;
        }
        lua_pop(L, 1);
    }
    if (lua_getiuservalue(L, 1, 2) == LUA_TTABLE) {
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_rawset(L, -3);
        return 0;
    }
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING) return luaL_error(L, "%s has no field '%s'", lua_tostring(L, lua_upvalueindex(2)), lua_tostring(L, 2));
    return luaL_error(L, "%s objects cannot be indexed for assignment", lua_tostring(L, lua_upvalueindex(2)));
}
// Class:extend{...}: a Lua subclass whose methods override the native class's virtual methods.
inline int class_extend(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_isnoneornil(L, 2)) { lua_settop(L, 1); lua_newtable(L); }
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_settop(L, 2);
    if (!lua_getmetatable(L, 1)) return luaL_error(L, "extend: not a class");
    lua_getfield(L, -1, "__call");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "extend: the class has no constructor");
    lua_createtable(L, 0, 3);           // the subclass's metatable
    lua_pushvalue(L, 1); lua_setfield(L, -2, "__index");
    lua_insert(L, -2); lua_setfield(L, -2, "__call");
    lua_getfield(L, -2, "__name");
    if (!lua_isnil(L, -1)) lua_setfield(L, -2, "__name"); else lua_pop(L, 1);
    lua_setmetatable(L, 2);
    lua_settop(L, 2);
    return 1;
}
// Calls upvalue 1 with the first argument only (Lua passes the operand twice to __len).
inline int unary(lua_State* L) {
    lua_settop(L, 1);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, 1, 1);
    return 1;
}
// A class without a usable constructor: upvalue 1 is the message.
inline int refuse(lua_State* L) { return luaL_error(L, "%s", lua_tostring(L, lua_upvalueindex(1))); }
inline int make_ref(lua_State* L) {
    lua_settop(L, 1);
    lua_createtable(L, 0, 1);
    lua_insert(L, 1);
    lua_setfield(L, 1, "value");
    lua_rawgetp(L, LUA_REGISTRYINDEX, &ref_key());
    lua_setmetatable(L, 1);
    return 1;
}
inline int state_closed(lua_State* L) {
    auto* holder = static_cast<std::shared_ptr<StateLife>*>(lua_touserdata(L, 1));
    if (holder) { (*holder)->alive = false; holder->~shared_ptr(); }
    return 0;
}
// Per-state tables shared by every class: the life sentinel, the weak director table, ref() cells.
inline void open_state(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &alive_key());
    const bool ready = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (ready) return;
    auto* life = new (lua_newuserdatauv(L, sizeof(std::shared_ptr<StateLife>), 0)) std::shared_ptr<StateLife>(std::make_shared<StateLife>());
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    (*life)->main = lua_tothread(L, -1);
    lua_pop(L, 1);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, state_closed);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &alive_key());
    lua_newtable(L);
    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &selves_key());
    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "ref");
    lua_setfield(L, -2, "__name");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &ref_key());
    if (luaL_newmetatable(L, ROSETTA_LUA_CALLABLE)) {
        lua_pushcfunction(L, callable_call); lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, callable_gc); lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);
}
// Gives an object created through a Lua subclass its instance table (whose metatable finds
// the subclass), and for a director its weak back-reference.
inline void adopt_instance(lua_State* L, int self, int cls, Director* director) {
    self = lua_absindex(L, self);
    cls = lua_absindex(L, cls);
    reserve(L, 4);
    if (director) {
        holder_at(L, self)->director = true;
        director->rosetta_state = life(L);
        lua_rawgetp(L, LUA_REGISTRYINDEX, &selves_key());
        lua_pushvalue(L, self);
        lua_rawsetp(L, -2, static_cast<const void*>(director));
        lua_pop(L, 1);
    }
    lua_newtable(L);
    lua_createtable(L, 0, 1);
    lua_pushvalue(L, cls);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setiuservalue(L, self, 2);
}
// Whether the class table (first constructor argument) is a Lua subclass.
inline bool subclass_call(lua_State* L, int cls) {
    if (!lua_istable(L, cls)) return false;
    lua_rawgetp(L, cls, &native_key());
    const bool native_class = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !native_class;
}
} } // ROSETTA_LUA_MODULE::rosetta_lua
)cpp"};
} // namespace rosetta::detail
