#include "demo.hpp"
#include "async_task.hpp"
#include "filesystem_library.hpp"
#include "concurrency_library.hpp"
#include "class_callback_library.hpp"
#include "executor_library.hpp"
#include "interop/shared_record.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
int luaopen_demo(lua_State*);
int luaopen_interop_a(lua_State*);
int luaopen_interop_b(lua_State*);
}

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <limits>
#include <filesystem>

// Lua uses realloc directly. Count ordinary and aligned C++ allocations
// independently, including native storage managed by custom deleters.
static std::atomic<long long> native_allocations{0};
static std::atomic<bool> fail_native_allocations{false};
static std::atomic<int> native_allocation_budget{-1};
static bool arm_native_from_callback = false;

void check_native_allocation() {
    if (fail_native_allocations.load()) throw std::bad_alloc();
    int remaining = native_allocation_budget.load();
    while (remaining >= 0) {
        if (remaining == 0) throw std::bad_alloc();
        if (native_allocation_budget.compare_exchange_weak(remaining, remaining - 1)) break;
    }
}
void* operator new(std::size_t size) {
    check_native_allocation();
    if (void* memory = std::malloc(size ? size : 1)) {
        ++native_allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept {
    if (memory) { --native_allocations; std::free(memory); }
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    check_native_allocation();
    const auto align = static_cast<std::size_t>(alignment);
    const auto actual = size ? size : 1;
    if (actual > std::numeric_limits<std::size_t>::max() - (align - 1)) throw std::bad_alloc();
    const auto rounded = ((actual + align - 1) / align) * align;
    if (void* memory = std::aligned_alloc(align, rounded)) { ++native_allocations; return memory; }
    throw std::bad_alloc();
}
void operator delete(void* memory, std::align_val_t) noexcept { ::operator delete(memory); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete[](void* memory, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }
void operator delete(void* memory, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }
void operator delete[](void* memory, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }

namespace {
struct AllocationControl {
    bool fail_growth = false;
    std::size_t denied = 0;
    std::size_t growth_budget = std::numeric_limits<std::size_t>::max();
};

void* lua_allocate(void* context, void* memory, std::size_t old_size, std::size_t new_size) {
    auto& control = *static_cast<AllocationControl*>(context);
    if (new_size == 0) { std::free(memory); return nullptr; }
    if (!memory || new_size > old_size) {
        if (control.fail_growth || control.growth_budget == 0) {
            ++control.denied;
            return nullptr;
        }
        --control.growth_budget;
    }
    return std::realloc(memory, new_size);
}

struct LuaVm {
    AllocationControl allocation;
    lua_State* state = lua_newstate(lua_allocate, &allocation);
    ~LuaVm() { if (state) lua_close(state); }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void function(lua_State* L, const char* name) {
    lua_settop(L, 0);
    lua_getglobal(L, "demo");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    require(lua_isfunction(L, -1), "missing demo function");
}

void payload(lua_State* L, const std::string& text) { lua_pushlstring(L, text.data(), text.size()); }
int filesystem_descriptors() {
#if defined(__linux__)
    DIR* directory = ::opendir("/proc/self/fd");
    require(directory != nullptr, "descriptor accounting could not open /proc/self/fd");
    int count = 0;
    while (const auto* entry = ::readdir(directory))
        if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) ++count;
    ::closedir(directory);
    return count;
#else
    return 0; // OS-level descriptor accounting is exercised on Linux.
#endif
}
void product_payload(lua_State* L, const std::string& text, bool valid = true) {
    lua_newtable(L);
    payload(L, text); lua_rawseti(L, -2, 1);
    if (valid) lua_pushinteger(L, 42); else lua_pushboolean(L, false);
    lua_rawseti(L, -2, 2);
    payload(L, text); lua_rawseti(L, -2, 3);
}
void const_product_payload(lua_State* L, const std::string& text) {
    lua_newtable(L);
    lua_pushboolean(L, true); lua_rawseti(L, -2, 1);
    lua_newtable(L);
    payload(L, text); lua_rawseti(L, -2, 1);
    payload(L, text); lua_rawseti(L, -2, 2);
    lua_rawseti(L, -2, 2);
    lua_pushboolean(L, false); lua_rawseti(L, -2, 3);
}
void enum_product_payload(lua_State* L, const std::string& text, bool valid = true) {
    lua_newtable(L);
    payload(L, text); lua_rawseti(L, -2, 1);
    lua_pushinteger(L, valid ? 1 : 3); lua_rawseti(L, -2, 2);
    lua_pushinteger(L, 129); lua_rawseti(L, -2, 3);
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_rawseti(L, -2, 1);
    lua_pushinteger(L, 3); lua_rawseti(L, -2, 2);
    lua_rawseti(L, -2, 4);
}
void map_payload(lua_State* L, const std::string& text, bool valid = true) {
    lua_newtable(L);
    payload(L, text);
    if (valid) payload(L, text); else lua_pushboolean(L, false);
    lua_rawset(L, -3);
    if (valid) { lua_pushliteral(L, "second"); payload(L, text); lua_rawset(L, -3); }
}
void enum_map_payload(lua_State* L, const std::string& text) {
    lua_newtable(L);
    payload(L, text); lua_pushinteger(L, 1); lua_rawset(L, -3);
    lua_pushliteral(L, "second"); lua_pushinteger(L, 2); lua_rawset(L, -3);
}
void optional_map_payload(lua_State* L, const std::string& text) {
    lua_newtable(L);
    lua_newtable(L); payload(L, text); lua_rawseti(L, -2, 1); lua_rawseti(L, -2, 1);
    lua_newtable(L);
    lua_pushliteral(L, "second"); lua_rawseti(L, -2, 1);
    payload(L, text); lua_rawseti(L, -2, 2); lua_rawseti(L, -2, 2);
}
std::size_t string_pins(lua_State* L) {
    std::size_t result = 0;
    lua_pushnil(L);
    while (lua_next(L, LUA_REGISTRYINDEX)) {
        if (lua_type(L, -2) == LUA_TNUMBER && lua_type(L, -1) == LUA_TSTRING) ++result;
        lua_pop(L, 1);
    }
    return result;
}

void constructor(lua_State* L, const char* name) {
    lua_settop(L, 0);
    lua_getglobal(L, "demo");
    lua_getfield(L, -1, name);
    lua_getfield(L, -1, "new");
    lua_remove(L, -2);
    lua_remove(L, -2);
    require(lua_isfunction(L, -1), "missing class constructor");
}

void method(lua_State* L, const char* name, const char* owner = "oom_owner") {
    lua_settop(L, 0);
    lua_getglobal(L, owner);
    lua_getfield(L, -1, name);
    lua_insert(L, -2);
    require(lua_isfunction(L, -2), "missing class method");
}

void check_balanced_call(lua_State* L, int arguments, bool success) {
    const auto before = native_allocations.load();
    const int status = lua_pcall(L, arguments, 1, 0);
    require((status == LUA_OK) == success, "unexpected native call status");
    require(native_allocations.load() == before, "C++ temporaries leaked across a Lua/native error");
    lua_pop(L, 1);
}
} // namespace

int main(int argc, char** argv) {
    try {
        LuaVm vm;
        auto* L = vm.state;
        require(L != nullptr, "cannot create Lua state");
        luaL_openlibs(L);
        luaL_requiref(L, "demo", luaopen_demo, 1);
        lua_pop(L, 1);
        lua_getglobal(L,"package"); lua_getfield(L,-1,"preload");
        lua_pushcfunction(L,luaopen_interop_a); lua_setfield(L,-2,"interop_a");
        lua_pushcfunction(L,luaopen_interop_b); lua_setfield(L,-2,"interop_b"); lua_pop(L,2);
        luaL_requiref(L,"interop_a",luaopen_interop_a,1); lua_pop(L,1);
        luaL_requiref(L,"interop_b",luaopen_interop_b,1); lua_pop(L,1);
        require(lua_checkstack(L, 256), "cannot reserve test stack");
        const std::string text(8192, 'x'); // long strings always allocate in Lua
        const auto initial_native = native_allocations.load();
        const auto initial_descriptors = filesystem_descriptors();
        require(luaL_dostring(L, "assert(demo.demo__invoke_callback(function(v) return v end, 42) == 42)") == LUA_OK,
            "callback VM warmup failed");

        int registration_failures = 0, registration_successes = 0;
        for (std::size_t budget = 0; budget <= 20; ++budget) {
            const auto before = native_allocations.load();
            LuaVm probe;
            require(probe.state != nullptr, "cannot create callback failure probe");
            auto* P = probe.state;
            luaL_openlibs(P); luaL_requiref(P, "demo", luaopen_demo, 1); lua_pop(P, 1);
            require(lua_checkstack(P, 256), "cannot reserve callback probe stack");
            require(luaL_dostring(P, "probe_weak = setmetatable({}, {__mode='v'}); do local marker = {}; probe_weak[1] = marker; "
                "probe_callback = function(v) return marker and v + 1 end end") == LUA_OK, "probe closure setup failed");
            function(P, "demo__invoke_callback"); lua_getglobal(P, "probe_callback"); lua_pushinteger(P, 41);
            probe.allocation.growth_budget = budget;
            const int status = lua_pcall(P, 2, 1, 0);
            probe.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
            if (status == LUA_OK) { ++registration_successes; require(lua_tointeger(P, -1) == 42, "probe callback result"); }
            else ++registration_failures;
            lua_settop(P, 0); lua_pushnil(P); lua_setglobal(P, "probe_callback");
            lua_gc(P, LUA_GCCOLLECT); lua_gc(P, LUA_GCCOLLECT);
            lua_getglobal(P, "probe_weak"); lua_rawgeti(P, -1, 1);
            require(lua_isnil(P, -1), "partial callback registration leaked a Lua reference");
            lua_close(P); probe.state = nullptr;
            require(native_allocations.load() == before, "partial callback VM/reference registration leaked native state");
        }
        require(registration_failures > 0 && registration_successes > 0, "callback registration failure sweep did not reach success and failure paths");

        function(L, "demo__echo");
        payload(L, text);
        const auto before_success = native_allocations.load();
        require(lua_pcall(L, 1, 1, 0) == LUA_OK, "string echo failed");
        std::size_t size = 0;
        const char* result = lua_tolstring(L, -1, &size);
        require(result && size == text.size() && std::memcmp(result, text.data(), size) == 0, "incorrect copied string");
        require(native_allocations.load() == before_success, "C++ result was not destroyed after publishing");
        lua_pop(L, 1);

        // Default expressions allocate native temporaries. A view into a default
        // temporary must stay valid through protected Lua result publication.
        for (const char* name : {"demo__text_then_integer", "demo__default_view", "demo__default_cstring"}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget < 10; ++budget) {
                function(L, name);
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, 0, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) { ++successes; require(lua_rawlen(L, -1) >= 128, "default value was corrupted"); }
                else ++failures;
                lua_pop(L, 1);
                require(native_allocations.load() == before, "default expression allocation leaked");
            }
            require(successes && failures, "default allocation sweep missed a path");
            lua_gc(L, LUA_GCCOLLECT); // clear the C-string API cache before denying publication allocation
            function(L, name);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 0, 1, 0);
            vm.allocation.fail_growth = false;
            require(status != LUA_OK, "default result publication did not fail");
            lua_pop(L, 1);
            require(native_allocations.load() == before, "default publication leaked native temporaries");
        }
        function(L, "demo__default_failure");
        check_balanced_call(L, 0, false);
        function(L, "demo__default_failure");
        payload(L, text);
        check_balanced_call(L, 1, false);

        for (int repetition = 0; repetition < 20; ++repetition) {
            function(L, "demo__text_then_integer");
            payload(L, text);
            lua_pushliteral(L, "bad");
            check_balanced_call(L, 2, false);

            function(L, "demo__text_failure");
            payload(L, text);
            check_balanced_call(L, 1, false);

            function(L, "demo__text_large_result");
            payload(L, text);
            check_balanced_call(L, 1, false);

            function(L, "demo__reverse_words");
            lua_newtable(L);
            payload(L, text); lua_rawseti(L, -2, 1);
            lua_pushboolean(L, 1); lua_rawseti(L, -2, 2);
            check_balanced_call(L, 1, false);

            function(L, "demo__echo");
            payload(L, text);
            const auto before = native_allocations.load();
            const auto calls = demo::echo_calls();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM, "expected a protected Lua allocation failure");
            require(demo::echo_calls() == calls + 1, "allocation failure occurred before native invocation");
            require(native_allocations.load() == before, "C++ string arguments/result leaked on Lua allocation failure");
            lua_pop(L, 1);

            function(L, "demo__reverse_words");
            lua_newtable(L);
            payload(L, text); lua_rawseti(L, -2, 1);
            payload(L, text); lua_rawseti(L, -2, 2);
            const auto before_vector = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int vector_status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(vector_status == LUA_ERRMEM, "expected protected Lua table allocation failure");
            require(native_allocations.load() == before_vector, "C++ vector/string elements leaked on Lua allocation failure");
            lua_pop(L, 1);
        }
        require(vm.allocation.denied >= 40, "allocator failure injection was not exercised");
        function(L, "demo__echo"); payload(L, text); check_balanced_call(L, 1, true);

        for (int repetition = 0; repetition < 20; ++repetition) {
            for (const char* factory : {"demo__make_counter_value", "demo__make_counter", "demo__make_shared_counter"}) {
                function(L, factory); lua_pushinteger(L, 42);
                const auto before = native_allocations.load();
                const auto destroyed = demo::Counter::destroyed();
                vm.allocation.fail_growth = true;
                const int status = lua_pcall(L, 1, 1, 0);
                vm.allocation.fail_growth = false;
                require(status == LUA_ERRMEM, "expected object publication allocation failure");
                require(demo::Counter::destroyed() > destroyed && demo::Counter::live() == 0,
                    "factory object not destroyed after failed publication");
                require(native_allocations.load() == before, "factory ownership leaked on Lua allocation failure");
                lua_pop(L, 1);
            }
            function(L, "demo__make_move_only"); payload(L, text);
            const auto before_move = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int move_status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(move_status == LUA_ERRMEM && demo::MoveOnly::live() == 0, "move-only object publication did not clean up");
            require(native_allocations.load() == before_move, "move-only native allocation leaked");
            lua_pop(L, 1);

            constructor(L, "demo__Counter"); lua_pushinteger(L, -1); payload(L, text);
            check_balanced_call(L, 2, false);
            require(demo::Counter::live() == 0, "throwing constructor left a live native object");

            constructor(L, "demo__Counter"); lua_pushinteger(L, 42); payload(L, text);
            const auto before_constructor = native_allocations.load();
            const auto destroyed = demo::Counter::destroyed();
            vm.allocation.fail_growth = true;
            const int constructor_status = lua_pcall(L, 2, 1, 0);
            vm.allocation.fail_growth = false;
            require(constructor_status == LUA_ERRMEM && demo::Counter::live() == 0 && demo::Counter::destroyed() == destroyed,
                "constructor ran before userdata allocation completed");
            require(native_allocations.load() == before_constructor, "constructor argument leaked on allocation failure");
            lua_pop(L, 1);

            constructor(L, "demo__Point");
            const auto before_native = native_allocations.load();
            fail_native_allocations = true;
            const int native_status = lua_pcall(L, 0, 1, 0);
            fail_native_allocations = false;
            require(native_status == LUA_ERRRUN && native_allocations.load() == before_native,
                "native allocation failure after userdata creation did not clean up");
            lua_pop(L, 1);
            lua_gc(L, LUA_GCCOLLECT);
        }

        constructor(L, "demo__Counter"); lua_pushinteger(L, 42); payload(L, text);
        require(lua_pcall(L, 2, 1, 0) == LUA_OK, "cannot construct ownership fixture");
        lua_setglobal(L, "oom_owner");
        for (const char* name : {"alias", "readonly", "point", "readonly_point", "clone"}) {
            method(L, name);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::Counter::live() == 1, "failed alias/clone publication damaged its owner");
            require(native_allocations.load() == before, "failed method result publication leaked native state");
            lua_pop(L, 1);
        }
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "fixture close failed");
        require(demo::Counter::live() == 0, "fixture was not closed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner");
        lua_gc(L, LUA_GCCOLLECT);

        if (argc >= 2) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[1]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::Counter::live() == 0 && demo::MoveOnly::live() == 0, "ownership script left live objects");
            require(native_allocations.load() == before, "ownership script leaked native state");
        }

        if (argc >= 3) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[2]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::EventSource::live() == 0, "callback script left an event owner alive");
            require(native_allocations.load() == before, "callback script leaked native callback state");
        }

        if (argc >= 4) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[3]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::EntityView::live() == 0, "hierarchy script left a native entity alive");
            require(native_allocations.load() == before, "hierarchy script leaked native ownership state");
        }

        if (argc >= 5) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[4]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::IntSeries::live() == 0 && demo::SeriesCursor::live() == 0, "protocol script left native storage alive");
            require(native_allocations.load() == before, "protocol script leaked native state");
        }

        if (argc >= 6) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[5]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::AsyncTask::live() == 0 && demo::AsyncTask::workers() == 0, "async script left tasks or workers alive");
            require(native_allocations.load() == before, "async script leaked native state");
        }

        if (argc >= 7) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[6]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::TaggedRecord::live() == 0 && demo::ValueStore::live() == 0, "advanced value script left native storage alive");
            require(native_allocations.load() == before, "advanced value script leaked native state");
        }

        if (argc >= 8) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[7]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "compile-time adapter script leaked native state");
        }

        if (argc >= 9) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[8]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::ByteBuffer::live() == 0 && demo::ByteSlice::live() == 0, "buffer script left native storage alive");
            require(native_allocations.load() == before, "buffer script leaked native state");
        }

        if (argc >= 10) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[9]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::CallObject::live() == 0, "call adapter script left native objects alive");
            require(native_allocations.load() == before, "call adapter script leaked native state");
        }

        if (argc >= 11) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[10]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::IntResult::live() == 0, "error adapter script left native results alive");
            require(native_allocations.load() == before, "error adapter script leaked native state");
        }

        if (argc >= 12) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[11]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::ResourceSlot::live() == 0, "ownership transfer script left native resources alive");
            require(native_allocations.load() == before, "ownership transfer script leaked native state");
        }

        if (argc >= 13) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[12]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::Generator::frames() == 0 && demo::Generator::bodies() == 0, "coroutine script left native frames alive");
            require(native_allocations.load() == before, "coroutine script leaked native state");
        }

        if (argc >= 14) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[13]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "Unicode adapter script leaked native state");
        }

        if (argc >= 15) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[14]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "extended numeric script leaked native state");
        }

        if (argc >= 16) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[15]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::TimePoint::live() == 0 && native_allocations.load() == before, "chrono script leaked native state");
        }
        if (argc >= 17) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[16]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "product script leaked native state");
        }
        if (argc >= 18) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[17]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "enum flag script leaked native state");
        }
        if (argc >= 19) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[18]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "enum value script leaked native state");
        }
        if (argc >= 20) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[19]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(demo::NestedOwner::live() == 0 && demo::NestedOwner::Item::live() == 0 && native_allocations.load() == before,
                "nested class script leaked native state");
        }
        if (argc >= 21) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[20]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "map script leaked native values across failures");
        }
        if (argc >= 22) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[21]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "variant script leaked native values");
        }
        if (argc >= 23) {
            const auto before = native_allocations.load();
            const auto descriptors = filesystem_descriptors();
            if (luaL_loadfile(L, argv[22]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && filesystem_descriptors() == descriptors && demo::FileWorkspace::live() == 0 && demo::FileWorkspace::cleanup_errors() == 0,
                "filesystem script leaked native storage or workspace files");
        }
        if (argc >= 24) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[23]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before, "extended container script leaked native storage");
        }
        if (argc >= 25) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[24]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::Counter::live() == 0, "imported name script leaked native ownership");
        }
        if (argc >= 26) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[25]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::Worklist::live() == 0, "worklist script leaked native state");
        }
        if (argc >= 27) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[26]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::ConcurrentState::live() == 0 && demo::ConcurrentState::workers() == 0,
                "concurrency script leaked native states or workers");
        }
        if (argc >= 28) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[27]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::MemberObject::live() == 0 && demo::MemberSelector::live() == 0,
                "member selector script leaked native state");
        }
        if (argc >= 29) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[28]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::NativeBoundary::live() == 0,
                "native ABI script leaked native state");
        }
        if (argc >= 30) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[29]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::native_module_live() == 0,
                "native module script leaked storage");
        }
        if (argc >= 31) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[30]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::NativeStationary::live() == 0,
                "native declaration/storage forms script leaked state");
        }
        if (argc >= 32) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[31]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L, -1));
            require(native_allocations.load() == before && demo::NativeCallable::live() == 0 &&
                demo::NativeCallable::captures() == 0 && demo::NativeCallable::targets() == 0,
                "native returned callable script leaked state");
        }
        if (argc >= 35) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L,argv[34]) != LUA_OK || lua_pcall(L,0,0,0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L,-1));
            require(native_allocations.load() == before && rosetta_example::exchange::live() == 0 && rosetta_example::exchange::tickets() == 0,
                "independent module script leaked native records or registry entries");
        }
        {
            const auto before = native_allocations.load();
            {
                LuaVm first,second;
                require(first.state && second.state,"cannot create exchange test VMs");
                luaL_openlibs(first.state); luaL_openlibs(second.state);
                luaL_requiref(first.state,"interop_a",luaopen_interop_a,1); lua_pop(first.state,1);
                luaL_requiref(second.state,"interop_b",luaopen_interop_b,1); lua_pop(second.state,1);
                require(luaL_dostring(first.state,"source=interop_a.interop_a__Record.new(string.rep('cross-vm',1000),42); transfer=source:ticket()") == LUA_OK,
                    "cross-VM exchange source failed");
                lua_getglobal(first.state,"transfer");
                std::size_t size = 0; const char* token = lua_tolstring(first.state,-1,&size);
                require(token != nullptr,"exchange ticket missing");
                lua_pushlstring(second.state,token,size); lua_setglobal(second.state,"transfer");
                require(luaL_dostring(second.state,"imported=interop_b.interop_b__Record.from_ticket(transfer)") == LUA_OK,"cross-VM exchange import failed");
                lua_close(first.state); first.state = nullptr;
                require(rosetta_example::exchange::live() == 1 && luaL_dostring(second.state,"assert(imported:value()==42 and #imported:text()==8000); imported:shift(1); assert(imported:value()==43)") == LUA_OK,
                    "source VM shutdown invalidated imported ownership");
            }
            require(native_allocations.load() == before && rosetta_example::exchange::live() == 0 && rosetta_example::exchange::tickets() == 0,
                "cross-VM exchange ownership leaked native state");
        }
        const auto exchange_factory = [&](const char* side,const char* factory) {
            lua_settop(L,0); lua_getglobal(L,side);
            lua_getfield(L,-1,std::string_view(side)=="interop_a" ? "interop_a__Record" : "interop_b__Record");
            lua_getfield(L,-1,factory); lua_remove(L,-2); lua_remove(L,-2);
        };
        for (const char* side : {"interop_a","interop_b"}) {
            for (const bool lua_failure : {false,true}) {
                int successes=0,failures=0;
                for (int budget=0; budget<=24; ++budget) {
                    exchange_factory(side,"new"); payload(L,text); lua_pushinteger(L,42);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget=static_cast<std::size_t>(budget);
                    else native_allocation_budget=budget;
                    const int status=lua_pcall(L,2,1,0);
                    vm.allocation.growth_budget=std::numeric_limits<std::size_t>::max(); native_allocation_budget=-1;
                    if (status==LUA_OK) ++successes; else ++failures;
                    lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                    require(native_allocations.load()==before && rosetta_example::exchange::live()==0 && rosetta_example::exchange::tickets()==0,
                        "exchange construction/registration/publication leaked state");
                }
                require(successes>0 && failures>0,"exchange construction sweep missed success/failure");
            }
        }
        {
            const auto before_fixture=native_allocations.load();
            require(luaL_dostring(L,"exchange_source=interop_a.interop_a__Record.new(string.rep('source',1000),42); exchange_token=exchange_source:ticket()") == LUA_OK,
                "exchange import fault fixture failed");
            for (const bool lua_failure : {false,true}) {
                int successes=0,failures=0;
                for (int budget=0; budget<=24; ++budget) {
                    exchange_factory("interop_b","from_ticket"); lua_getglobal(L,"exchange_token");
                    const auto before=native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget=static_cast<std::size_t>(budget);
                    else native_allocation_budget=budget;
                    const int status=lua_pcall(L,1,1,0);
                    vm.allocation.growth_budget=std::numeric_limits<std::size_t>::max(); native_allocation_budget=-1;
                    if (status==LUA_OK) ++successes; else ++failures;
                    lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                    require(native_allocations.load()==before && rosetta_example::exchange::live()==1 && rosetta_example::exchange::tickets()==1,
                        "exchange import/publication lost or leaked shared native ownership");
                }
                require(successes>0 && failures>0,"exchange import sweep missed success/failure");
            }
            require(luaL_dostring(L,"exchange_imported=interop_b.interop_b__Record.from_ticket(exchange_token)") == LUA_OK,"exchange method fixture failed");
            for (const char* owner : {"exchange_source","exchange_imported"}) {
                for (const char* operation : {"ticket","text","share","readonly"}) {
                    for (const bool lua_failure : {false,true}) {
                        for (int budget=0; budget<=24; ++budget) {
                            method(L,operation,owner);
                            const auto before=native_allocations.load();
                            if (lua_failure) vm.allocation.growth_budget=static_cast<std::size_t>(budget);
                            else native_allocation_budget=budget;
                            (void)lua_pcall(L,1,1,0);
                            vm.allocation.growth_budget=std::numeric_limits<std::size_t>::max(); native_allocation_budget=-1;
                            lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                            require(native_allocations.load()==before && rosetta_example::exchange::live()==1,
                                "exchange copied result/borrow/share publication leaked native state");
                        }
                    }
                }
            }
            require(luaL_dostring(L,"exchange_source:close(); assert(exchange_imported:value()==42); exchange_imported:close(); exchange_source=nil; exchange_imported=nil; exchange_token=nil; collectgarbage('collect')") == LUA_OK,
                "exchange fixture cleanup failed");
            require(native_allocations.load()==before_fixture && rosetta_example::exchange::live()==0 && rosetta_example::exchange::tickets()==0,
                "exchange fault sweep left native registry state");
        }
        if (argc >= 34) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L,argv[33]) != LUA_OK || lua_pcall(L,0,0,0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L,-1));
            require(native_allocations.load() == before && demo::AwaitExecutor::live() == 0 && demo::AwaitTask::live() == 0 &&
                demo::AwaitExecutor::frames() == 0 && demo::AwaitExecutor::bodies() == 0,"awaitable script leaked executor/task/frame state");
        }
        {
            const auto before = native_allocations.load();
            {
                namespace native_await = feature_library::awaitables;
                auto executor = std::make_shared<native_await::Executor>();
                auto task = executor->submit(40,1,text,false,{});
                int rejected = 0;
                std::thread worker([&] {
                    const auto check = [&](auto operation) {
                        try { operation(); }
                        catch (const std::exception& error) { if (std::strstr(error.what(),"owning thread")) ++rejected; }
                    };
                    check([&] { (void)executor->run_one(); }); check([&] { (void)executor->pending(); });
                    check([&] { (void)task->cancel(); }); check([&] { (void)task->state(); }); check([&] { (void)task->result(); });
                });
                worker.join();
                require(rejected == 5 && executor->pending() == 1 && native_await::Executor::bodies == 0,
                    "wrong-thread awaitable operation touched native task state");
                task->cancel();
                require(!executor->run_one(),"cancelled awaitable remained dispatchable");
                std::vector<std::shared_ptr<native_await::Control>> tasks;
                for (int count = 0; count < 128; ++count) tasks.push_back(executor->submit(0,2,text,false,{}));
                native_allocation_budget = 0;
                try { while (executor->run_one()) {} }
                catch (...) { native_allocation_budget = -1; throw; }
                native_allocation_budget = -1;
                for (const auto& item : tasks) require(item->state() == native_await::Phase::failed,
                    "awaitable preparation/scheduling failure escaped promise error storage");
                require(executor->pending() == 0 && native_await::Executor::frames == 0 && native_await::Executor::bodies == 0,
                    "awaitable allocation exhaustion left runnable frames");
            }
            require(native_allocations.load() == before && demo::AwaitExecutor::live() == 0 && demo::AwaitTask::live() == 0,
                "native awaitable thread/scheduling checks leaked state");
        }
        for (const bool lua_failure : {false, true}) {
            for (int budget = 0; budget <= 24; ++budget) {
                constructor(L,"demo__AwaitExecutor");
                const auto before = native_allocations.load();
                if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                else native_allocation_budget = budget;
                (void)lua_pcall(L,0,1,0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                require(native_allocations.load() == before && demo::AwaitExecutor::live() == 0,"executor construction/publication leaked state");
            }
        }
        {
            const auto before_fixture = native_allocations.load();
            require(luaL_dostring(L,"await_executor=demo.demo__AwaitExecutor.new(); await_callback=function(v) return v+1 end") == LUA_OK,"executor failure fixture failed");
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 48; ++budget) {
                    method(L,"submit","await_executor"); lua_pushinteger(L,40); lua_pushinteger(L,2); payload(L,text); lua_pushboolean(L,false); lua_getglobal(L,"await_callback");
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L,6,1,0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                    require(demo::AwaitTask::live() == 0 && demo::AwaitExecutor::frames() == 0,"awaitable submit/publication leaked a task frame");
                    method(L,"run_one","await_executor"); require(lua_pcall(L,1,1,0) == LUA_OK && !lua_toboolean(L,-1),"dead awaitable queue entry resumed");
                    lua_settop(L,0);
                }
                require(successes > 0 && failures > 0,"awaitable submit fault sweep missed success/failure");
            }
            for (const bool lua_failure : {false, true}) {
                for (int budget = 0; budget <= 48; ++budget) {
                    require(luaL_dostring(L,"await_task=await_executor:submit(40,1,string.rep('result',1000),false,await_callback)") == LUA_OK,"awaitable dispatch fault fixture failed");
                    method(L,"run_one","await_executor");
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    (void)lua_pcall(L,1,1,0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    lua_settop(L,0);
                    method(L,"run_one","await_executor");
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    (void)lua_pcall(L,1,1,0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    lua_settop(L,0);
                    // A pre-resume Lua failure may leave work pending; finish it normally.
                    require(luaL_dostring(L,"while await_executor:run_one() do end; if await_task:state()==demo.demo__AwaitPhase.succeeded then assert(#await_task:result()>0) end; await_task:close(); await_task=nil; collectgarbage('collect')") == LUA_OK,
                        "awaitable dispatch/result recovery failed");
                    require(demo::AwaitTask::live() == 0 && demo::AwaitExecutor::frames() == 0 && demo::AwaitExecutor::bodies() == 0,
                        "awaitable dispatch/callback failure retained a frame");
                }
            }
            require(luaL_dostring(L,"await_task=await_executor:submit(40,0,string.rep('stable',1000),false,nil); assert(await_executor:run_one())") == LUA_OK,
                "awaitable result-publication fixture failed");
            for (const char* operation : {"result", "share"}) {
                for (const bool lua_failure : {false, true}) {
                    for (int budget = 0; budget <= 24; ++budget) {
                        method(L,operation,"await_task");
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        (void)lua_pcall(L,1,1,0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                        require(demo::AwaitTask::live() == 1 && demo::AwaitExecutor::frames() == 0,"awaitable result/share publication leaked task state");
                    }
                }
            }
            require(luaL_dostring(L,"assert(await_task:state()==demo.demo__AwaitPhase.succeeded and await_task:result()==string.rep('stable',1000)..'40'); await_task:close(); await_task=nil; collectgarbage('collect')") == LUA_OK,
                "awaitable result publication failure changed completed state");
            require(luaL_dostring(L,"await_executor:close(); await_executor=nil; await_callback=nil; collectgarbage('collect')") == LUA_OK,"executor fault cleanup failed");
            require(native_allocations.load() == before_fixture && demo::AwaitExecutor::live() == 0 && demo::AwaitTask::live() == 0 &&
                demo::AwaitExecutor::allocations() == demo::AwaitExecutor::releases(),"awaitable fault sweeps leaked storage");
        }
        {
            const auto before = native_allocations.load();
            {
                namespace native_class = feature_library::class_callbacks;
                native_class::Record input(text,42);
                native_class::Frame frame;
                auto result = frame.transform(input,[&] {
                    frame.set_result(input);
                    bool rejected = false;
                    native_allocation_budget = 0;
                    try { frame.set_result(input); }
                    catch (const std::bad_alloc&) { rejected = true; }
                    native_allocation_budget = -1;
                    require(rejected,"class callback result replacement did not hit allocation failure");
                });
                require(result.text == text && result.number == 42 && frame.depth() == 0,
                    "failed class-value copy did not preserve the previous result");
                std::function<void()> recursive;
                recursive = [&] { (void)frame.transform(input,recursive); };
                bool limited = false;
                try { (void)frame.transform(input,recursive); }
                catch (const std::exception& error) { limited = std::strstr(error.what(),"nesting limit") != nullptr; }
                require(limited && frame.depth() == 0,"native class callback recursion did not restore the frame");
            }
            require(native_allocations.load() == before && demo::CallbackRecord::live() == 0 && demo::ClassCallbackFrame::live() == 0,
                "native class callback strong-copy/recursion checks leaked state");
        }
        if (argc >= 33) {
            const auto before = native_allocations.load();
            if (luaL_loadfile(L, argv[32]) != LUA_OK || lua_pcall(L,0,0,0) != LUA_OK)
                throw std::runtime_error(lua_tostring(L,-1));
            require(native_allocations.load() == before && demo::CallbackRecord::live() == 0 && demo::ClassCallbackFrame::live() == 0,
                "class-value callback script leaked records or frames");
        }
        for (const bool lua_failure : {false, true}) {
            for (int budget = 0; budget <= 24; ++budget) {
                constructor(L,"demo__CallbackRecord"); payload(L,text); lua_pushinteger(L,42);
                const auto before = native_allocations.load();
                if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                else native_allocation_budget = budget;
                (void)lua_pcall(L,2,1,0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                require(native_allocations.load() == before && demo::CallbackRecord::live() == 0,
                    "callback record construction/publication leaked state");
                constructor(L,"demo__ClassCallbackFrame");
                if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                else native_allocation_budget = budget;
                (void)lua_pcall(L,0,1,0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                require(native_allocations.load() == before && demo::ClassCallbackFrame::live() == 0,
                    "class callback frame construction/publication leaked state");
            }
        }
        {
            const auto before_fixture = native_allocations.load();
            require(luaL_dostring(L,"class_input=demo.demo__CallbackRecord.new(string.rep('record',1000),42); class_frame=demo.demo__ClassCallbackFrame.new(); class_ok=function() local a <close> = class_frame:argument(); class_frame:set_result(a) end; class_error=function() local a <close> = class_frame:argument(); class_frame:set_result(a); error('class callback failure') end; class_nested=function() local a <close> = class_frame:argument(); local b <close> = class_frame:transform(a,class_ok); class_frame:set_result(b) end") == LUA_OK,
                "class callback fault fixture failed");
            for (const char* callback : {"class_ok", "class_error", "class_nested"}) {
                for (const bool lua_failure : {false, true}) {
                    int successes = 0, failures = 0;
                    for (int budget = 0; budget <= 48; ++budget) {
                        method(L,"transform","class_frame"); lua_getglobal(L,"class_input"); lua_getglobal(L,callback);
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        const int status = lua_pcall(L,3,1,0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        if (status == LUA_OK) ++successes; else ++failures;
                        lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                        require(demo::CallbackRecord::live() == 1 && demo::ClassCallbackFrame::live() == 1,
                            "class callback argument/result/error leaked state");
                        method(L,"depth","class_frame"); require(lua_pcall(L,1,1,0) == LUA_OK && lua_tointeger(L,-1) == 0,
                            "class callback fault left an active frame"); lua_settop(L,0);
                    }
                    if (std::string_view(callback) == "class_error") require(successes == 0 && failures > 0,"class callback error was lost");
                    else require(successes > 0 && failures > 0,"class callback sweep missed success/failure");
                }
            }
            require(luaL_dostring(L,"class_input:close(); class_frame:close(); class_input=nil; class_frame=nil; class_ok=nil; class_error=nil; class_nested=nil; collectgarbage('collect')") == LUA_OK,
                "class callback fixture cleanup failed");
            require(native_allocations.load() == before_fixture && demo::CallbackRecord::live() == 0 && demo::ClassCallbackFrame::live() == 0,
                "class-value callback fault sweep leaked storage");
        }
        for (const char* factory : {"demo__make_native_capture", "demo__make_native_bound", "demo__make_native_pointer", "demo__make_native_empty", "demo__make_native_callback"}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    lua_settop(L, 0); lua_getglobal(L, "demo"); lua_getfield(L, -1, factory); lua_remove(L,-2);
                    const bool text_factory = std::string_view(factory) == "demo__make_native_capture" || std::string_view(factory) == "demo__make_native_bound";
                    const bool callback_factory = std::string_view(factory) == "demo__make_native_callback";
                    int arguments = 0;
                    if (text_factory) { payload(L, text); lua_pushinteger(L, 20); arguments = 2; }
                    if (callback_factory) { require(luaL_loadstring(L, "return function(v) return tostring(v) end") == LUA_OK && lua_pcall(L,0,1,0) == LUA_OK,
                        "callable callback fixture failed"); arguments = 1; }
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, arguments, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                    require(native_allocations.load() == before && demo::NativeCallable::live() == 0 &&
                        demo::NativeCallable::captures() == 0 && demo::NativeCallable::targets() == 0,
                        "native callable factory/publication leaked storage");
                }
                require(successes > 0 && failures > 0, "callable factory failure sweep missed success/failure");
            }
        }
        for (const char* factory : {"demo__make_native_capture", "demo__make_native_bound", "demo__make_native_callback"}) {
            const auto before_fixture = native_allocations.load();
            lua_settop(L,0); lua_getglobal(L,"demo"); lua_getfield(L,-1,factory); lua_remove(L,-2);
            const bool callback = std::string_view(factory) == "demo__make_native_callback";
            if (callback) {
                require(luaL_loadstring(L, "return function(v) return string.rep('result',1000)..v end") == LUA_OK && lua_pcall(L,0,1,0) == LUA_OK,
                    "callable retained fixture failed");
            } else { payload(L,text); lua_pushinteger(L,0); }
            require(lua_pcall(L,callback ? 1 : 2,1,0) == LUA_OK,"callable operation fixture failed");
            lua_setglobal(L,"callable_owner");
            int native_expected = 0;
            for (const char* operation : {"invoke", "copy", "share"}) {
                for (const bool lua_failure : {false, true}) {
                    for (int budget = 0; budget <= 24; ++budget) {
                        method(L,operation,"callable_owner");
                        const bool invoke = std::string_view(operation) == "invoke";
                        if (invoke) lua_pushinteger(L,1);
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        const int status = lua_pcall(L,invoke ? 2 : 1,1,0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        lua_settop(L,0); lua_gc(L,LUA_GCCOLLECT);
                        if (invoke && !lua_failure && !callback) {
                            if (status == LUA_OK) ++native_expected;
                            method(L,"invoke","callable_owner"); lua_pushinteger(L,0);
                            require(lua_pcall(L,2,1,0) == LUA_OK,"callable state probe failed");
                            std::size_t length = 0;
                            const char* value = lua_tolstring(L,-1,&length);
                            require(value && std::string_view(value,length) == text + std::to_string(native_expected),
                                "native callable allocation failure changed capture/target state");
                            lua_settop(L,0);
                        }
                        require(demo::NativeCallable::live() == 1,"callable copy/publication retained an unexpected slot");
                    }
                }
            }
            require(luaL_dostring(L,"callable_owner:close(); callable_owner=nil; collectgarbage('collect')") == LUA_OK,"callable fixture cleanup failed");
            require(native_allocations.load() == before_fixture && demo::NativeCallable::live() == 0 &&
                demo::NativeCallable::captures() == 0 && demo::NativeCallable::targets() == 0,"callable invoke/copy/share leaked storage");
        }
        for (const bool error : {false, true}) {
            for (const bool lua_failure : {false, true}) {
                for (int budget = 0; budget <= 24; ++budget) {
                    constructor(L, "demo__NativeStationary"); payload(L, text); lua_pushboolean(L, error);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, 2, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (error) require(status != LUA_OK, "throwing stationary constructor succeeded");
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && demo::NativeStationary::live() == 0,
                        "stationary construction/publication leaked storage");
                }
            }
        }
        {
            const auto before_fixture = native_allocations.load();
            require(luaL_dostring(L, "stationary_owner=demo.demo__NativeStationary.new(string.rep('stationary',1000),false); matrix_input={{1,2,3},{4,5,6}}") == LUA_OK,
                "native forms failure fixture failed");
            for (const char* operation : {"text", "share", "readonly"}) {
                for (const bool lua_failure : {false, true}) {
                    for (int budget = 0; budget <= 24; ++budget) {
                        method(L, operation, "stationary_owner");
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        (void)lua_pcall(L, 1, 1, 0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                        require(demo::NativeStationary::live() == 1, "stationary publication changed owner identity");
                    }
                }
            }
            for (int budget = 0; budget <= 24; ++budget) {
                lua_settop(L, 0); lua_getglobal(L, "demo"); lua_getfield(L, -1, "demo__native_matrix_shift"); lua_remove(L,-2);
                lua_getglobal(L, "matrix_input"); lua_pushinteger(L, 40);
                vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                (void)lua_pcall(L, 2, 1, 0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
            }
            require(luaL_dostring(L, "stationary_owner:close(); stationary_owner=nil; assert(matrix_input[1][1]==1 and matrix_input[2][3]==6); matrix_input=nil; collectgarbage('collect')") == LUA_OK,
                "native forms fault fixture cleanup failed");
            require(native_allocations.load() == before_fixture && demo::NativeStationary::live() == 0,
                "stationary or matrix publication failure leaked storage");
        }
        if (demo::native_modules_available()) {
            for (const bool error : {false, true}) {
                for (const bool lua_failure : {false, true}) {
                    for (int budget = 0; budget <= 24; ++budget) {
                        lua_settop(L, 0); lua_getglobal(L, "demo"); lua_getfield(L, -1, "demo__native_module_text");
                        lua_remove(L, -2); payload(L, text); lua_pushboolean(L, error);
                        const auto before = native_allocations.load();
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        const int status = lua_pcall(L, 2, 1, 0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        if (error) require(status != LUA_OK, "native module exception was lost");
                        lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                        require(native_allocations.load() == before && demo::native_module_live() == 0,
                            "native module construction/result/error leaked storage");
                    }
                }
            }
        }
        for (int operation : {1, 2}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    constructor(L, "demo__NativeBoundary"); lua_pushinteger(L, operation); lua_pushinteger(L, 2); payload(L, text);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, 3, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && demo::NativeBoundary::live() == 0,
                        "native ABI construction/publication leaked storage");
                }
                require(successes > 0 && failures > 0, "native ABI failure sweep missed success or failure");
            }
        }
        {
            const auto before_fixture = native_allocations.load();
            require(luaL_dostring(L, "abi_owner = demo.demo__NativeBoundary.new(demo.demo__BoundaryOperation.add, 20, string.rep('payload',1000)); abi_callback = function(v) return v + 1 end; abi_error = function() error('native ABI callback error') end") == LUA_OK,
                "native ABI fault fixture failed");
            for (const char* operation : {"payload", "share", "with_callback"}) {
                for (const bool lua_failure : {false, true}) {
                    for (int budget = 0; budget <= 24; ++budget) {
                        method(L, operation, "abi_owner");
                        const bool callback = std::string_view(operation) == "with_callback";
                        if (callback) { lua_getglobal(L, "abi_callback"); lua_pushinteger(L, 21); }
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        (void)lua_pcall(L, callback ? 3 : 1, 1, 0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                        require(demo::NativeBoundary::live() == 1, "native ABI fault changed owner lifetime");
                    }
                }
            }
            for (int budget = 0; budget <= 24; ++budget) {
                method(L, "with_callback", "abi_owner"); lua_getglobal(L, "abi_error"); lua_pushinteger(L, 21);
                native_allocation_budget = budget;
                const int status = lua_pcall(L, 3, 1, 0);
                native_allocation_budget = -1;
                require(status != LUA_OK, "native ABI callback error was lost");
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
            }
            require(luaL_dostring(L, "abi_owner:close(); abi_owner=nil; abi_callback=nil; abi_error=nil; collectgarbage('collect')") == LUA_OK,
                "native ABI fault fixture cleanup failed");
            require(native_allocations.load() == before_fixture && demo::NativeBoundary::live() == 0,
                "native ABI callback/publication fault leaked storage");
        }
        for (const bool derived : {false, true}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    constructor(L, "demo__MemberObject"); lua_pushinteger(L, 20); lua_pushinteger(L, 21); payload(L, text); lua_pushboolean(L, derived);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, 4, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && demo::MemberObject::live() == 0, "member target construction/publication leaked native storage");
                }
                require(successes && failures, "member target construction sweep missed a path");
            }
        }
        for (int choice = 0; choice <= 6; ++choice) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 12; ++budget) {
                    constructor(L, "demo__MemberSelector"); lua_pushinteger(L, choice);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && demo::MemberSelector::live() == 0, "member selection construction/publication leaked native state");
                }
                require(successes && failures, "member selection construction sweep missed a path");
            }
        }
        for (const char* operation : {"read_text", "write_text", "copy", "share"}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    const auto before = native_allocations.load();
                    constructor(L, "demo__MemberObject"); lua_pushinteger(L, 20); lua_pushinteger(L, 21); payload(L, text); lua_pushboolean(L, true);
                    require(lua_pcall(L, 4, 1, 0) == LUA_OK, "member operation target fixture failed");
                    lua_setglobal(L, "member_object");
                    constructor(L, "demo__MemberSelector"); lua_pushinteger(L, 3);
                    require(lua_pcall(L, 1, 1, 0) == LUA_OK, "member operation selection fixture failed");
                    lua_setglobal(L, "member_selector");
                    const bool share = std::strcmp(operation, "share") == 0;
                    const bool read = std::strcmp(operation, "read_text") == 0;
                    const bool write = std::strcmp(operation, "write_text") == 0;
                    method(L, operation, share ? "member_object" : "member_selector");
                    if (read || write) lua_getglobal(L, "member_object");
                    if (write) { const std::string replacement(text.size(), 'y'); payload(L, replacement); }
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, write ? 3 : read ? 2 : 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    lua_pushboolean(L, write && status == LUA_OK); lua_setglobal(L, "member_write_committed");
                    require(luaL_dostring(L, "assert(member_selector:read_text(member_object) == string.rep(member_write_committed and 'y' or 'x',8192)); "
                        "member_object:close(); member_selector:close(); member_object=nil; member_selector=nil; collectgarbage('collect')") == LUA_OK,
                        "member operation failure corrupted its source or cleanup");
                    require(native_allocations.load() == before && demo::MemberObject::live() == 0 && demo::MemberSelector::live() == 0,
                        "member copy/access/publication leaked native values");
                }
                require(successes && (failures || (lua_failure && std::strcmp(operation, "write_text") == 0)), "member operation sweep missed a path");
            }
        }
        lua_pushnil(L); lua_setglobal(L, "member_write_committed");
        {
            const auto before = native_allocations.load();
            {
                demo::ConcurrentState state(0, 0);
                feature_library::concurrency::JoinedThreads writers(2);
                for (int worker = 0; worker < 2; ++worker) writers.start([&] {
                    for (int i = 0; i < 1000; ++i) { state.add(1); state.register_exchange(static_cast<unsigned>(i)); }
                });
                const auto completed = state.parallel_add(4, 200, 1);
                writers.join();
                require(state.load() == 2800 && completed.size() == 4 && demo::ConcurrentState::workers() == 0,
                    "overlapping native atomic updates lost changes");
                state.store(0);
                std::atomic<int> winners{0};
                std::atomic<bool> valid{true};
                feature_library::concurrency::JoinedThreads contenders(8);
                for (int i = 1; i <= 8; ++i) contenders.start([&, i] {
                    const auto result = state.compare_exchange(0, i);
                    if (std::get<0>(result)) { ++winners; if (std::get<1>(result) != 0) valid.store(false); }
                    else if (std::get<1>(result) < 1 || std::get<1>(result) > 8) valid.store(false);
                });
                contenders.join();
                require(winners.load() == 1 && valid.load() && state.load() >= 1 && state.load() <= 8,
                    "native compare/exchange did not retain one winner");
            }
            require(native_allocations.load() == before && demo::ConcurrentState::live() == 0,
                "concurrent native updates leaked state");
        }
        for (const bool lua_failure : {false, true}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 24; ++budget) {
                constructor(L, "demo__ConcurrentState"); lua_pushinteger(L, 0); lua_pushinteger(L, 42);
                const auto before = native_allocations.load();
                if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                else native_allocation_budget = budget;
                const int status = lua_pcall(L, 2, 1, 0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                if (status == LUA_OK) ++successes; else ++failures;
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(native_allocations.load() == before && demo::ConcurrentState::live() == 0, "concurrency construction/publication leaked state");
            }
            require(successes && failures, "concurrency construction sweep missed a path");
        }
        for (const bool overflow : {false, true}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    const auto before = native_allocations.load();
                    constructor(L, "demo__ConcurrentState");
                    lua_pushinteger(L, overflow ? std::numeric_limits<int>::max() : 0); lua_pushinteger(L, 42);
                    require(lua_pcall(L, 2, 1, 0) == LUA_OK, "concurrent worker fixture construction failed");
                    lua_setglobal(L, "concurrent_owner");
                    method(L, "parallel_add", "concurrent_owner");
                    lua_pushinteger(L, 3); lua_pushinteger(L, 50); lua_pushinteger(L, 1);
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, 4, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0);
                    require(demo::ConcurrentState::workers() == 0, "failed worker launch/invocation did not join its threads");
                    if (overflow) require(status != LUA_OK, "worker overflow succeeded");
                    require(luaL_dostring(L, "concurrent_owner:close(); concurrent_owner=nil; collectgarbage('collect')") == LUA_OK,
                        "concurrent worker fixture cleanup failed");
                    require(native_allocations.load() == before && demo::ConcurrentState::live() == 0,
                        "worker failure/publication leaked states, captures or exceptions");
                }
                require(failures && (overflow || successes), "worker failure sweep missed a path");
            }
        }
        for (const bool lua_failure : {false, true}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 16; ++budget) {
                function(L, "demo__thread_local_probe"); lua_pushinteger(L, 42);
                const auto before = native_allocations.load();
                if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                else native_allocation_budget = budget;
                const int status = lua_pcall(L, 1, 1, 0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                if (status == LUA_OK) ++successes; else ++failures;
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(native_allocations.load() == before, "TLS thread/probe publication leaked native storage");
            }
            require(successes && failures, "TLS thread/probe sweep missed a path");
        }
        for (int order = 1; order <= 4; ++order) {
            const auto before_growth = native_allocations.load();
            {
                demo::Worklist values(static_cast<demo::WorkOrder>(order));
                int failures = 0;
                for (int i = 0; i < 64; ++i) {
                    std::string item = text; // construct before arming failure
                    bool failed = false;
                    native_allocation_budget = 0;
                    try { values.push(std::move(item)); }
                    catch (const std::bad_alloc&) { failed = true; }
                    native_allocation_budget = -1;
                    if (failed) {
                        ++failures;
                        require(values.size() == static_cast<unsigned>(i), "native container growth failure changed worklist contents");
                        values.push(text);
                    }
                    require(values.size() == static_cast<unsigned>(i + 1), "worklist growth recovery lost an entry");
                }
                require(failures > 0, "native worklist growth allocation failure was not exercised");
            }
            require(native_allocations.load() == before_growth && demo::Worklist::live() == 0, "native worklist growth leaked storage");
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 32; ++budget) {
                    constructor(L, "demo__Worklist"); lua_pushinteger(L, order);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && demo::Worklist::live() == 0, "worklist construction/publication leaked native storage");
                }
                require(successes && failures, "worklist construction sweep missed a path");
            }
            for (const char* operation : {"push", "peek", "pop", "snapshot", "clone", "share"}) {
                for (const bool lua_failure : {false, true}) {
                    int successes = 0, failures = 0;
                    for (int budget = 0; budget <= 32; ++budget) {
                        lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                        const auto before = native_allocations.load();
                        constructor(L, "demo__Worklist"); lua_pushinteger(L, order);
                        require(lua_pcall(L, 1, 1, 0) == LUA_OK, "worklist operation fixture construction failed");
                        lua_setglobal(L, "worklist_owner");
                        for (int i = 0; i < 4; ++i) {
                            method(L, "push", "worklist_owner"); payload(L, text);
                            require(lua_pcall(L, 2, 0, 0) == LUA_OK, "worklist operation fixture push failed");
                        }
                        method(L, operation, "worklist_owner");
                        const bool push = std::strcmp(operation, "push") == 0;
                        if (push) payload(L, text);
                        if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                        else native_allocation_budget = budget;
                        const int status = lua_pcall(L, push ? 2 : 1, 1, 0);
                        vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max(); native_allocation_budget = -1;
                        if (status == LUA_OK) ++successes; else ++failures;
                        lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                        if ((push && status != LUA_OK) || (!push && std::strcmp(operation, "pop") != 0))
                            require(luaL_dostring(L, "assert(worklist_owner:size() == 4)") == LUA_OK, "worklist preparation failure changed its source");
                        require(luaL_dostring(L, "worklist_owner:close(); worklist_owner=nil; collectgarbage('collect')") == LUA_OK,
                            "worklist operation fixture cleanup failed");
                        require(native_allocations.load() == before && demo::Worklist::live() == 0,
                            "worklist mutation/copy/publication leaked native blocks, nodes or strings");
                    }
                    require(successes && (failures || (lua_failure && std::strcmp(operation, "push") == 0)), "worklist operation sweep missed a path");
                }
            }
        }
        require(luaL_dostring(L, "container_identity = function(value) return value end") == LUA_OK, "container callback fixture failed");
        for (const char* operation : {"deque_values", "list_operations", "forward_values", "ordered_set", "hashed_set",
            "multi_set", "hashed_multi_set", "optional_set", "optional_hash_set", "monostate_multi_set", "nested_set",
            "multi_map", "hashed_multi_map", "sequence_callback", "set_callback", "set_views_after", "sequence_view"}) {
            const bool callback = std::strcmp(operation, "sequence_callback") == 0 || std::strcmp(operation, "set_callback") == 0;
            const bool views = std::strcmp(operation, "set_views_after") == 0;
            const bool defaults = std::strcmp(operation, "sequence_view") == 0;
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 64; ++budget) {
                    const std::string exported = std::string("demo__") + operation;
                    function(L, exported.c_str());
                    if (callback) lua_getglobal(L, "container_identity");
                    if (!defaults) {
                        lua_newtable(L);
                        for (int i = 1; i <= 2; ++i) {
                            if (std::strcmp(operation, "list_operations") == 0 || std::strcmp(operation, "hashed_set") == 0)
                                lua_pushinteger(L, i);
                            else if (std::strcmp(operation, "forward_values") == 0) {
                                lua_newtable(L); lua_pushinteger(L, 3); lua_setfield(L, -2, "index");
                                payload(L, text); lua_setfield(L, -2, "value");
                            } else if (std::strcmp(operation, "optional_set") == 0 || std::strcmp(operation, "optional_hash_set") == 0 ||
                                       std::strcmp(operation, "monostate_multi_set") == 0 || std::strcmp(operation, "nested_set") == 0) {
                                lua_newtable(L);
                                if (std::strcmp(operation, "nested_set") == 0 || (i == 2 && std::strcmp(operation, "monostate_multi_set") != 0)) {
                                    if (std::strcmp(operation, "optional_set") == 0) payload(L, text);
                                    else lua_pushinteger(L, i);
                                    lua_rawseti(L, -2, 1);
                                }
                            } else if (std::strcmp(operation, "multi_map") == 0 || std::strcmp(operation, "hashed_multi_map") == 0) {
                                lua_newtable(L); lua_pushliteral(L, "duplicate"); lua_rawseti(L, -2, 1);
                                payload(L, text); lua_rawseti(L, -2, 2);
                            } else if (i == 1) payload(L, text);
                            else lua_pushliteral(L, "second");
                            lua_rawseti(L, -2, i);
                        }
                    }
                    if (views) lua_getglobal(L, "container_identity");
                    const auto before = native_allocations.load();
                    const auto pins = string_pins(L);
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, defaults ? 0 : callback || views ? 2 : 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                    native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && string_pins(L) == pins,
                        "extended container conversion/publication leaked native nodes, blocks or roots");
                }
                require(successes && failures, (std::string("container allocation sweep missed a path: ") + operation).c_str());
            }
        }
        lua_pushnil(L); lua_setglobal(L, "container_identity");
        for (const char* operation : {"demo__sequence_failure", "demo__sequence_oversized_result"}) {
            function(L, operation); lua_newtable(L);
            payload(L, text); lua_rawseti(L, -2, 1);
            payload(L, text); lua_rawseti(L, -2, 2);
            check_balanced_call(L, 1, false);
        }
        // A link introduced by external native code is never followed by the
        // workspace API or cleanup, including a link to another live workspace.
        {
            feature_library::files::Workspace target;
            target.write("sentinel", "preserved");
            {
                feature_library::files::Workspace source;
                std::filesystem::create_directory_symlink(target.location(), std::filesystem::path(source.location()) / "escape");
                bool rejected = false;
                try { source.read("escape/sentinel"); }
                catch (const std::invalid_argument&) { rejected = true; }
                require(rejected && source.entries("", true).size() == 1, "workspace followed an external symbolic link");
            }
            require(target.read("sentinel") == "preserved", "workspace cleanup followed a symbolic link");
        }
        for (const bool lua_failure : {false, true}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 48; ++budget) {
                constructor(L, "demo__FileWorkspace");
                const auto before = native_allocations.load();
                const auto descriptors = filesystem_descriptors();
                if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                else native_allocation_budget = budget;
                const int status = lua_pcall(L, 0, 1, 0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                native_allocation_budget = -1;
                if (status == LUA_OK) ++successes; else ++failures;
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(native_allocations.load() == before && filesystem_descriptors() == descriptors && demo::FileWorkspace::live() == 0 && demo::FileWorkspace::cleanup_errors() == 0,
                    "failed workspace construction/publication leaked resources");
            }
            require(successes && failures, "workspace construction sweep missed a path");
        }
        require(luaL_dostring(L, "filesystem_owner = demo.demo__FileWorkspace.new(); filesystem_owner:write('data', string.rep('binary', 1000))") == LUA_OK,
            "filesystem allocation fixture failed");
        for (const char* operation : {"read", "write", "entries", "share", "directories", "rename", "erase"}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 80; ++budget) {
                    const bool write = std::strcmp(operation, "write") == 0;
                    const bool read = std::strcmp(operation, "read") == 0;
                    const bool entries = std::strcmp(operation, "entries") == 0;
                    const bool directories = std::strcmp(operation, "directories") == 0;
                    const bool rename = std::strcmp(operation, "rename") == 0;
                    const bool erase = std::strcmp(operation, "erase") == 0;
                    if (write) require(luaL_dostring(L, "filesystem_owner:write('data', 'original')") == LUA_OK,
                        "atomic write fixture reset failed");
                    if (entries || erase) require(luaL_dostring(L,
                        "filesystem_owner:directories('branch/nested'); filesystem_owner:write('branch/nested/file', 'snapshot')") == LUA_OK,
                        "recursive filesystem fixture reset failed");
                    method(L, operation, "filesystem_owner");
                    if (write || read) lua_pushliteral(L, "data");
                    if (write) payload(L, text);
                    if (entries) { lua_pushliteral(L, ""); lua_pushboolean(L, true); }
                    if (directories) lua_pushliteral(L, "branch/nested");
                    if (rename) { lua_pushliteral(L, "data"); lua_pushliteral(L, "renamed"); }
                    if (erase) { lua_pushliteral(L, "branch"); lua_pushboolean(L, true); }
                    const auto before = native_allocations.load();
                    const auto descriptors = filesystem_descriptors();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, write || entries || rename || erase ? 3 : read || directories ? 2 : 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                    native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && filesystem_descriptors() == descriptors && demo::FileWorkspace::live() == 1 && demo::FileWorkspace::cleanup_errors() == 0,
                        "filesystem conversion/publication leaked storage or descriptors");
                    if (write) require(luaL_dostring(L, status == LUA_OK
                        ? "assert(filesystem_owner:read('data') == string.rep('x', 8192))"
                        : "assert(filesystem_owner:read('data') == 'original')") == LUA_OK,
                        "failed atomic write replaced its destination");
                    require(luaL_dostring(L,
                        "if filesystem_owner:exists('renamed') then filesystem_owner:rename('renamed', 'data') end; "
                        "filesystem_owner:erase('branch', true)") == LUA_OK, "filesystem sweep reset failed");
                    require(luaL_dostring(L, "assert(#filesystem_owner:entries() == 1)") == LUA_OK,
                        "failed filesystem write left a staging file");
                }
                // Void and scalar results need no Lua publication allocation.
                const bool allocating_result = std::strcmp(operation, "read") == 0 ||
                    std::strcmp(operation, "entries") == 0 || std::strcmp(operation, "share") == 0;
                require(successes && (failures || (lua_failure && !allocating_result)),
                    "filesystem operation sweep missed a path");
            }
        }
        require(luaL_dostring(L, "filesystem_owner:close(); filesystem_owner=nil; collectgarbage('collect')") == LUA_OK,
            "filesystem allocation fixture cleanup failed");
        require(luaL_dostring(L, "variant_identity_callback = function(value) return value end") == LUA_OK, "variant callback fixture failed");
        for (const char* operation : {"demo__variant_identity", "demo__const_variant", "demo__variant_callback", "demo__variant_view"}) {
            const bool callback = std::strcmp(operation, "demo__variant_callback") == 0;
            const bool defaults = std::strcmp(operation, "demo__variant_view") == 0;
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 32; ++budget) {
                    function(L, operation);
                    if (callback) lua_getglobal(L, "variant_identity_callback");
                    if (!defaults) {
                        lua_createtable(L, 0, 2);
                        lua_pushinteger(L, std::strcmp(operation, "demo__const_variant") == 0 ? 2 : 3);
                        lua_setfield(L, -2, "index");
                        lua_pushlstring(L, text.data(), text.size()); lua_setfield(L, -2, "value");
                    }
                    const auto before = native_allocations.load();
                    const auto pins = string_pins(L);
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, defaults ? 0 : callback ? 2 : 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                    native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && string_pins(L) == pins,
                        "variant conversion/publication leaked native storage or roots");
                }
                require(successes && failures, "variant allocation sweep missed a path");
            }
        }
        lua_pushnil(L); lua_setglobal(L, "variant_identity_callback");
        for (int repetition = 0; repetition < 10; ++repetition) {
            function(L, "demo__string_map"); map_payload(L, text, false);
            check_balanced_call(L, 1, false);
            function(L, "demo__map_failure"); map_payload(L, text);
            check_balanced_call(L, 1, false);
            function(L, "demo__map_oversized_result"); map_payload(L, text);
            check_balanced_call(L, 1, false);
        }
        require(luaL_dostring(L, "map_identity_callback = function(value) return value end") == LUA_OK, "map callback fixture failed");
        for (const char* operation : {"demo__string_map", "demo__map_callback", "demo__unordered_enum_map", "demo__optional_map", "demo__map_views_after"}) {
            const bool callback = std::strcmp(operation, "demo__map_callback") == 0;
            const bool views = std::strcmp(operation, "demo__map_views_after") == 0;
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 32; ++budget) {
                    function(L, operation);
                    if (callback) lua_getglobal(L, "map_identity_callback");
                    if (std::strcmp(operation, "demo__unordered_enum_map") == 0) enum_map_payload(L, text);
                    else if (std::strcmp(operation, "demo__optional_map") == 0) optional_map_payload(L, text);
                    else map_payload(L, text);
                    if (views) lua_getglobal(L, "map_identity_callback");
                    const auto before = native_allocations.load();
                    const auto pins = string_pins(L);
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, callback || views ? 2 : 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                    native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && string_pins(L) == pins, "partial map/callback conversion leaked native nodes, values or roots");
                }
                require(successes && failures, "map allocation sweep missed a path");
            }
        }
        lua_pushnil(L); lua_setglobal(L, "map_identity_callback");
        for (const char* name : {"demo__NestedOwner", "demo__NestedOwner__Item"}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 8; ++budget) {
                constructor(L, name); lua_pushinteger(L, 42);
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, 1, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) {
                    ++successes;
                    lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                    require(lua_pcall(L, 1, 0, 0) == LUA_OK, "nested allocation result could not close");
                } else { ++failures; require(status == LUA_ERRRUN, "unexpected nested native allocation error"); }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(demo::NestedOwner::live() == 0 && demo::NestedOwner::Item::live() == 0 && native_allocations.load() == before,
                    "partial nested construction leaked native storage");
            }
            require(successes && failures, "nested construction sweep missed a path");
        }
        require(luaL_dostring(L, "oom_owner = demo.demo__NestedOwner.new(42)") == LUA_OK, "nested fixture failed");
        for (const char* name : {"item", "readonly_item"}) {
            method(L, name);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::NestedOwner::live() == 1 && demo::NestedOwner::Item::live() == 1 && native_allocations.load() == before,
                "nested borrow publication damaged its owner");
            lua_pop(L, 1);
        }
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "nested fixture close failed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner"); lua_gc(L, LUA_GCCOLLECT);
        for (int repetition = 0; repetition < 10; ++repetition) {
            function(L, "demo__enum_product"); enum_product_payload(L, text, false);
            check_balanced_call(L, 1, false);
            function(L, "demo__enum_product_failure"); enum_product_payload(L, text);
            check_balanced_call(L, 1, false);
            function(L, "demo__enum_oversized_result"); enum_product_payload(L, text);
            check_balanced_call(L, 1, false);
        }
        require(luaL_dostring(L, "enum_value_callback = function(value) return value end") == LUA_OK, "enum callback fixture failed");
        for (const bool callback : {false, true}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    function(L, callback ? "demo__enum_product_callback" : "demo__enum_product");
                    if (callback) lua_getglobal(L, "enum_value_callback");
                    enum_product_payload(L, text);
                    const auto before = native_allocations.load();
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, callback ? 2 : 1, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                    native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before, "enum value/callback failure leaked native values");
                }
                require(successes && failures, "enum value/callback allocation sweep missed a path");
            }
        }
        lua_pushnil(L); lua_setglobal(L, "enum_value_callback");
        for (int repetition = 0; repetition < 10; ++repetition) {
            function(L, "demo__flags_text"); payload(L, text); lua_pushinteger(L, 2);
            check_balanced_call(L, 2, false); // unknown bits after an owned string conversion
            function(L, "demo__flags_text"); payload(L, text); lua_pushinteger(L, 129);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 2, 1, 0);
            vm.allocation.fail_growth = false;
            require(status != LUA_OK && native_allocations.load() == before, "flag text publication leaked native values");
            lua_pop(L, 1);
        }
        for (int repetition = 0; repetition < 10; ++repetition) {
            function(L, "demo__product_identity"); product_payload(L, text, false);
            check_balanced_call(L, 1, false); // first owned element must unwind when the second fails
            function(L, "demo__product_failure"); product_payload(L, text);
            check_balanced_call(L, 1, false);
            function(L, "demo__product_large_result"); product_payload(L, text);
            check_balanced_call(L, 1, false); // Lua publication fails after copying an earlier element
        }
        require(luaL_dostring(L, "product_identity_callback = function(value) return value end") == LUA_OK,
            "product callback fixture failed");
        for (const char* operation : {"demo__product_identity", "demo__pair_identity", "demo__product_const", "demo__product_view", "demo__product_callback"}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 16; ++budget) {
                function(L, operation);
                int arguments = 1;
                if (std::strcmp(operation, "demo__product_view") == 0) arguments = 0;
                else if (std::strcmp(operation, "demo__product_const") == 0) const_product_payload(L, text);
                else if (std::strcmp(operation, "demo__pair_identity") == 0) {
                    lua_newtable(L); payload(L, text); lua_rawseti(L, -2, 1);
                    lua_pushboolean(L, true); lua_rawseti(L, -2, 2);
                } else {
                    if (std::strcmp(operation, "demo__product_callback") == 0) {
                        lua_getglobal(L, "product_identity_callback"); arguments = 2;
                    }
                    product_payload(L, text);
                }
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, arguments, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) ++successes;
                else { ++failures; require(status == LUA_ERRRUN, "unexpected product native allocation error"); }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(native_allocations.load() == before, "product/native callback allocation leaked partial values");
            }
            require(successes && failures, "product native allocation sweep missed a path");
        }
        for (const char* operation : {"demo__product_identity", "demo__product_const", "demo__product_view", "demo__product_callback"}) {
            int successes = 0, failures = 0;
            for (std::size_t budget = 0; budget <= 24; ++budget) {
                function(L, operation);
                int arguments = 1;
                if (std::strcmp(operation, "demo__product_view") == 0) arguments = 0;
                else if (std::strcmp(operation, "demo__product_const") == 0) const_product_payload(L, text);
                else {
                    if (std::strcmp(operation, "demo__product_callback") == 0) {
                        lua_getglobal(L, "product_identity_callback"); arguments = 2;
                    }
                    product_payload(L, text);
                }
                const auto before = native_allocations.load();
                vm.allocation.growth_budget = budget;
                const int status = lua_pcall(L, arguments, 1, 0);
                vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                if (status == LUA_OK) ++successes; else ++failures;
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(native_allocations.load() == before, "product Lua publication/callback failure leaked native values");
            }
            require(successes && failures, "product publication sweep missed a path");
        }
        lua_pushnil(L); lua_setglobal(L, "product_identity_callback");
        require(luaL_dostring(L, "view_noop = function() end") == LUA_OK, "view scope fixture failed");
        for (const char* operation : {"demo__view_identity", "demo__product_view_after", "demo__vector_view_after"}) {
            for (const bool lua_failure : {false, true}) {
                int successes = 0, failures = 0;
                for (int budget = 0; budget <= 24; ++budget) {
                    function(L, operation);
                    int arguments = 1;
                    if (std::strcmp(operation, "demo__view_identity") == 0) payload(L, text);
                    else {
                        arguments = 2;
                        lua_newtable(L); payload(L, text); lua_rawseti(L, -2, 1);
                        if (std::strcmp(operation, "demo__product_view_after") == 0) lua_pushinteger(L, 42);
                        else payload(L, text);
                        lua_rawseti(L, -2, 2);
                        lua_getglobal(L, "view_noop");
                    }
                    const auto before = native_allocations.load();
                    const auto pins = string_pins(L);
                    if (lua_failure) vm.allocation.growth_budget = static_cast<std::size_t>(budget);
                    else native_allocation_budget = budget;
                    const int status = lua_pcall(L, arguments, 1, 0);
                    vm.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
                    native_allocation_budget = -1;
                    if (status == LUA_OK) ++successes; else ++failures;
                    lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                    require(native_allocations.load() == before && string_pins(L) == pins, "partial view pinning leaked native values or registry roots");
                }
                require(successes && failures, "view allocation sweep missed a path");
            }
        }
        lua_pushnil(L); lua_setglobal(L, "view_noop");
        int fresh_view_successes = 0, fresh_view_failures = 0;
        for (std::size_t budget = 0; budget <= 40; ++budget) {
            const auto before = native_allocations.load();
            LuaVm probe;
            auto* P = probe.state;
            require(P != nullptr, "cannot create view pin probe");
            luaL_openlibs(P);
            luaL_requiref(P, "demo", luaopen_demo, 1); lua_pop(P, 1);
            require(lua_checkstack(P, 256), "cannot reserve view pin probe stack");
            require(luaL_dostring(P, "view_noop = function() end") == LUA_OK, "fresh view fixture failed");
            function(P, "demo__vector_view_after");
            lua_newtable(P);
            for (int i = 1; i <= 8; ++i) { payload(P, text); lua_rawseti(P, -2, i); }
            lua_getglobal(P, "view_noop");
            const auto pins = string_pins(P);
            // Fresh registries force ref-table growth while earlier decoded
            // views and the native vector are live, not just result allocation.
            probe.allocation.growth_budget = budget;
            const int status = lua_pcall(P, 2, 1, 0);
            probe.allocation.growth_budget = std::numeric_limits<std::size_t>::max();
            if (status == LUA_OK) ++fresh_view_successes; else ++fresh_view_failures;
            lua_settop(P, 0);
            require(string_pins(P) == pins, "fresh registry pin allocation leaked roots");
            lua_close(P); probe.state = nullptr;
            require(native_allocations.load() == before, "fresh view pin VM leaked native storage");
        }
        require(fresh_view_successes && fresh_view_failures, "fresh view registry sweep missed a path");
        require(luaL_dostring(L, "oom_owner = demo.demo__TimePoint.new(42, true)") == LUA_OK, "chrono fixture failed");
        for (int operation = 0; operation < 3; ++operation) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 8; ++budget) {
                int arguments = 1;
                if (operation == 0) { constructor(L, "demo__TimePoint"); lua_pushinteger(L, 42); }
                else if (operation == 1) { method(L, "shifted"); lua_pushinteger(L, -42); arguments = 2; }
                else { function(L, "demo__time_now"); lua_pushboolean(L, true); }
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, arguments, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) {
                    ++successes;
                    lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                    require(lua_pcall(L, 1, 0, 0) == LUA_OK, "chrono result could not close");
                } else { ++failures; require(status == LUA_ERRRUN, "unexpected chrono native allocation error"); }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(demo::TimePoint::live() == 1 && native_allocations.load() == before, "partial chrono allocation leaked");
            }
            require(successes && failures, "chrono allocation sweep missed success/failure paths");
        }
        for (const char* operation : {"shifted", "clock_info", "now"}) {
            int arguments = 1;
            if (std::strcmp(operation, "shifted") == 0) { method(L, "shifted"); lua_pushinteger(L, 1); arguments = 2; }
            else { function(L, std::strcmp(operation, "now") == 0 ? "demo__time_now" : "demo__time_clock_info"); lua_pushboolean(L, true); }
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, arguments, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::TimePoint::live() == 1 && native_allocations.load() == before,
                "chrono publication failure leaked or damaged its source");
            lua_pop(L, 1);
        }
        method(L, "ticks"); require(lua_pcall(L, 1, 1, 0) == LUA_OK && lua_tointeger(L, -1) == 42, "chrono source changed after failure");
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "chrono fixture close failed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner"); lua_gc(L, LUA_GCCOLLECT);
        require(demo::TimePoint::live() == 0, "chrono fixture leaked");

        for (const char* operation : {"demo__u64_decimal", "demo__signed128_decimal", "demo__long_double_decimal", "demo__long_double_add"}) {
            if (std::strcmp(operation, "demo__signed128_decimal") == 0 && !demo::int128_available()) continue;
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 8; ++budget) {
                function(L, operation);
                const bool add = std::strcmp(operation, "demo__long_double_add") == 0;
                const bool floating = add || std::strcmp(operation, "demo__long_double_decimal") == 0;
                if (floating) lua_pushliteral(L, "1.234567890123456789");
                else payload(L, std::string(128, '0') + "18446744073709551615");
                if (add) lua_pushliteral(L, "2.5");
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, add ? 2 : 1, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) ++successes;
                else { ++failures; require(status == LUA_ERRRUN, "unexpected numeric native-allocation error"); }
                require(native_allocations.load() == before, "numeric conversion leaked native temporaries");
                lua_pop(L, 1);
            }
            require(successes && failures, "numeric allocation sweep missed success/failure paths");
        }
        for (int budget = 0; budget <= 12; ++budget) {
            function(L, "demo__long_double_bounds");
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 0, 1, 0);
            native_allocation_budget = -1;
            require((status == LUA_OK || status == LUA_ERRRUN) && native_allocations.load() == before,
                "native long-double metadata formatting leaked strings");
            lua_pop(L, 1);
        }
        function(L, "demo__long_double_bounds");
        const auto before_numeric_publication = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int numeric_status = lua_pcall(L, 0, 1, 0);
        vm.allocation.fail_growth = false;
        require(numeric_status == LUA_ERRMEM && native_allocations.load() == before_numeric_publication,
            "failed numeric metadata publication leaked native values");
        lua_pop(L, 1);

        for (const char* conversion : {"demo__unicode_decode", "demo__unicode_utf16", "demo__unicode_char8",
            "demo__unicode_wide", "demo__unicode_encode", "demo__unicode_from_utf16"}) {
            int successes = 0, failures = 0;
            const bool encode = std::strcmp(conversion, "demo__unicode_encode") == 0;
            const bool from_units = std::strcmp(conversion, "demo__unicode_from_utf16") == 0;
            for (int budget = 0; budget <= 12; ++budget) {
                function(L, conversion);
                if (encode || from_units) {
                    lua_createtable(L, 8192, 0);
                    for (int i = 1; i <= 8192; ++i) { lua_pushinteger(L, encode ? 0x1f600 : 0x20ac); lua_rawseti(L, -2, i); }
                } else payload(L, text);
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, 1, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) ++successes;
                else { ++failures; require(status == LUA_ERRRUN, "unexpected Unicode native-allocation status"); }
                require(native_allocations.load() == before, "partial Unicode conversion leaked native intermediates");
                lua_pop(L, 1);
            }
            require(successes && failures, "Unicode sweep missed success/failure paths");
            function(L, conversion);
            if (encode || from_units) {
                lua_createtable(L, 8192, 0);
                for (int i = 1; i <= 8192; ++i) { lua_pushinteger(L, encode ? 0x1f600 : 0x20ac); lua_rawseti(L, -2, i); }
            } else payload(L, text);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && native_allocations.load() == before, "failed Unicode result publication leaked native state");
            lua_pop(L, 1);
        }
        {
            const std::string malformed = text + '\xff';
            for (const char* conversion : {"demo__unicode_decode", "demo__unicode_utf16", "demo__unicode_char8", "demo__unicode_wide"}) {
                function(L, conversion); payload(L, malformed);
                check_balanced_call(L, 1, false);
            }
        }

        int generator_successes = 0, generator_failures = 0;
        for (int budget = 0; budget <= 8; ++budget) {
            constructor(L, "demo__Generator"); lua_pushinteger(L, 40); lua_pushinteger(L, 3); payload(L, text); lua_pushinteger(L, -1);
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 4, 1, 0);
            native_allocation_budget = -1;
            if (status == LUA_OK) {
                ++generator_successes;
                lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                require(lua_pcall(L, 1, 0, 0) == LUA_OK, "generator construction fixture close failed");
            } else { ++generator_failures; require(status == LUA_ERRRUN, "unexpected generator construction status"); }
            lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
            require(demo::Generator::frames() == 0 && demo::Generator::bodies() == 0 &&
                demo::Generator::allocations() == demo::Generator::releases() && native_allocations.load() == before,
                "partial coroutine construction leaked its frame/parameters");
        }
        require(generator_successes && generator_failures, "coroutine allocation sweep missed success/failure paths");
        payload(L, text); lua_setglobal(L, "generator_prefix");
        const auto before_generator = native_allocations.load();
        require(luaL_dostring(L, "oom_owner = demo.demo__Generator.new(40, 3, generator_prefix, -1); assert(oom_owner:advance())") == LUA_OK,
            "generator publication fixture setup failed");
        method(L, "value");
        const auto with_yield = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int yield_status = lua_pcall(L, 1, 1, 0);
        vm.allocation.fail_growth = false;
        require(yield_status == LUA_ERRMEM && demo::Generator::bodies() == 1 && native_allocations.load() == with_yield,
            "failed yield publication damaged the suspended frame");
        lua_pop(L, 1);
        require(luaL_dostring(L, "assert(oom_owner:value() == generator_prefix .. '40')") == LUA_OK,
            "failed yield publication lost the current value");
        method(L, "advance");
        fail_native_allocations = true;
        const int resume_status = lua_pcall(L, 1, 1, 0);
        fail_native_allocations = false;
        require(resume_status == LUA_ERRRUN && demo::Generator::frames() == 1 && demo::Generator::bodies() == 0,
            "resume allocation failure did not unwind the coroutine body");
        lua_pop(L, 1);
        method(L, "advance");
        require(lua_pcall(L, 1, 1, 0) == LUA_ERRRUN, "coroutine resume failure was not retained"); lua_pop(L, 1);
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "failed generator close failed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner"); lua_gc(L, LUA_GCCOLLECT);
        require(demo::Generator::frames() == 0 && native_allocations.load() == before_generator,
            "failed coroutine frame leaked native storage");
        if (argc >= 13) {
            {
                const auto adapter = std::filesystem::path(argv[12]).parent_path().parent_path() / "examples" / "demo_generators.lua";
                require(luaL_loadfile(L, adapter.c_str()) == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK &&
                    lua_pcall(L, 0, 1, 0) == LUA_OK, "generator iterator adapter load failed");
                lua_getfield(L, -1, "values"); lua_setglobal(L, "generator_values"); lua_pop(L, 1);
            }
            lua_pushcfunction(L, [](lua_State* state) -> int {
                if (!lua_checkstack(state, 256)) return luaL_error(state, "cannot reserve iterator fixture stack");
                void* context = nullptr;
                lua_getallocf(state, &context);
                static_cast<AllocationControl*>(context)->fail_growth = true;
                return 0;
            });
            lua_setglobal(L, "arm_generator_allocations");
            const auto before_iterator = native_allocations.load();
            require(luaL_dostring(L, "oom_owner = demo.demo__Generator.new(40, 3, generator_prefix, -1); "
                "do local native, arm = oom_owner, true; retry_iterator = generator_values({ "
                "advance = function() return native:advance() end, value = function() "
                "if arm then arm = false; arm_generator_allocations() end; return native:value() end }) end") == LUA_OK,
                "generator iterator fixture failed");
            lua_getglobal(L, "retry_iterator");
            const int status = lua_pcall(L, 0, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::Generator::bodies() == 1, "iterator failure did not suspend at its yield");
            lua_pop(L, 1);
            require(luaL_dostring(L, "assert(retry_iterator() == generator_prefix .. '40'); "
                "assert(retry_iterator() == generator_prefix .. '41'); oom_owner:close(); "
                "oom_owner = nil; retry_iterator = nil; generator_values = nil; arm_generator_allocations = nil; "
                "collectgarbage('collect'); collectgarbage('collect')") == LUA_OK,
                "iterator retry skipped a yield or failed to close");
            require(demo::Generator::frames() == 0 && demo::Generator::bodies() == 0 && native_allocations.load() == before_iterator,
                "iterator retry fixture leaked native storage");
        }
        lua_pushnil(L); lua_setglobal(L, "generator_prefix");

        int resource_successes = 0, resource_failures = 0;
        for (bool fail_constructor : {false, true}) {
            for (int budget = 0; budget <= 8; ++budget) {
                function(L, "demo__make_resource"); payload(L, text); lua_pushinteger(L, 123); lua_pushboolean(L, fail_constructor);
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, 3, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) {
                    ++resource_successes;
                    require(!fail_constructor, "throwing placement constructor unexpectedly succeeded");
                    lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                    require(lua_pcall(L, 1, 0, 0) == LUA_OK, "resource construction fixture close failed");
                } else { ++resource_failures; require(status == LUA_ERRRUN, "unexpected resource construction error status"); }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(demo::ResourceSlot::live() == 0 && demo::ResourceSlot::allocations() == demo::ResourceSlot::releases() &&
                    native_allocations.load() == before, "partial aligned resource construction leaked storage");
            }
        }
        require(resource_successes && resource_failures, "aligned resource sweep missed success/failure paths");
        function(L, "demo__make_resource"); payload(L, text); lua_pushinteger(L, 123); lua_pushboolean(L, false);
        const auto before_resource_publication = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int resource_status = lua_pcall(L, 3, 1, 0);
        vm.allocation.fail_growth = false;
        require(resource_status == LUA_ERRMEM && demo::ResourceSlot::live() == 0 &&
            native_allocations.load() == before_resource_publication, "failed resource publication missed its custom deleter");
        lua_pop(L, 1);
        const auto before_transfer_fixture = native_allocations.load();
        require(luaL_dostring(L, "oom_owner = demo.demo__make_resource('transfer', 123, false); "
            "move_target = demo.demo__empty_resource(); shared_target = demo.demo__SharedResource.new()") == LUA_OK,
            "transfer failure fixture setup failed");
        method(L, "move_to"); lua_getglobal(L, "move_target");
        fail_native_allocations = true; vm.allocation.fail_growth = true;
        const int move_status = lua_pcall(L, 2, 0, 0);
        fail_native_allocations = false; vm.allocation.fail_growth = false;
        require(move_status == LUA_OK, "move commit unexpectedly required allocation");
        require(luaL_dostring(L, "assert(not oom_owner:has_value() and move_target:has_value()); "
            "oom_owner:close(); oom_owner = move_target; move_target = nil") == LUA_OK, "move did not transfer ownership");
        method(L, "promote_to"); lua_getglobal(L, "shared_target");
        const auto before_promotion = native_allocations.load();
        fail_native_allocations = true;
        const int promote_status = lua_pcall(L, 2, 0, 0);
        fail_native_allocations = false;
        require(promote_status == LUA_ERRRUN && native_allocations.load() == before_promotion,
            "failed shared promotion leaked native state");
        lua_pop(L, 1);
        require(luaL_dostring(L, "assert(oom_owner:has_value() and not shared_target:has_value())") == LUA_OK,
            "failed shared promotion consumed its source");
        method(L, "promote_to"); lua_getglobal(L, "shared_target");
        vm.allocation.fail_growth = true;
        const int promote_lua_status = lua_pcall(L, 2, 0, 0);
        vm.allocation.fail_growth = false;
        require(promote_lua_status == LUA_OK, "promotion needed allocation after its native commit");
        require(luaL_dostring(L, "assert(not oom_owner:has_value() and shared_target:has_value()); "
            "oom_owner:close(); oom_owner = shared_target; shared_target = nil") == LUA_OK, "promotion did not transfer ownership");
        for (const char* operation : {"clone", "weak"}) {
            method(L, operation);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::ResourceSlot::live() == 1 && native_allocations.load() == before,
                "failed shared/weak publication damaged resource ownership");
            lua_pop(L, 1);
        }
        method(L, "weak"); require(lua_pcall(L, 1, 1, 0) == LUA_OK, "weak fixture creation failed");
        lua_setglobal(L, "weak_owner");
        for (int budget = 0; budget <= 4; ++budget) {
            method(L, "lock", "weak_owner");
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 1, 1, 0);
            native_allocation_budget = -1;
            if (status == LUA_OK) {
                lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                require(lua_pcall(L, 1, 0, 0) == LUA_OK, "weak lock fixture close failed");
            } else require(status == LUA_ERRRUN, "unexpected weak lock allocation status");
            lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
            require(demo::ResourceSlot::live() == 1 && native_allocations.load() == before, "weak locking leaked or destroyed its resource");
        }
        method(L, "lock", "weak_owner");
        const auto before_lock = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int lock_status = lua_pcall(L, 1, 1, 0);
        vm.allocation.fail_growth = false;
        require(lock_status == LUA_ERRMEM && demo::ResourceSlot::live() == 1 && native_allocations.load() == before_lock,
            "weak lock publication changed resource ownership");
        lua_pop(L, 1);
        require(luaL_dostring(L, "oom_owner:close(); assert(weak_owner:expired() and weak_owner:lock() == nil); "
            "weak_owner:close(); oom_owner = nil; weak_owner = nil; collectgarbage('collect'); collectgarbage('collect')") == LUA_OK,
            "transfer fixture cleanup failed");
        require(demo::ResourceSlot::live() == 0 && demo::ResourceSlot::last_deleter_tag() == 123 &&
            native_allocations.load() == before_transfer_fixture, "transfer fixture leaked aligned/control storage or lost deleter state");

        for (const char* factory : {"demo__status_result", "demo__parse_result", "demo__nested_result"}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 16; ++budget) {
                function(L, factory);
                const bool status_result = std::strcmp(factory, "demo__status_result") == 0;
                const bool parse_result = std::strcmp(factory, "demo__parse_result") == 0;
                if (status_result) { lua_pushboolean(L, true); payload(L, text); }
                if (parse_result) lua_pushliteral(L, "invalid");
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, status_result ? 2 : parse_result ? 1 : 0, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) {
                    ++successes;
                    lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                    require(lua_pcall(L, 1, 0, 0) == LUA_OK, "structured error fixture close failed");
                } else { ++failures; require(status == LUA_ERRRUN, "unexpected structured-error allocation status"); }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(demo::IntResult::live() == 0 && native_allocations.load() == before,
                    "partial structured error construction leaked native state");
            }
            require(successes && failures, "structured error sweep missed success/failure paths");
        }
        function(L, "demo__status_result"); lua_pushboolean(L, true); payload(L, text);
        const auto before_result = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int result_status = lua_pcall(L, 2, 1, 0);
        vm.allocation.fail_growth = false;
        require(result_status == LUA_ERRMEM && demo::IntResult::live() == 0 && native_allocations.load() == before_result,
            "failed structured result publication leaked native state");
        lua_pop(L, 1);
        function(L, "demo__status_result"); lua_pushboolean(L, true); payload(L, text);
        require(lua_pcall(L, 2, 1, 0) == LUA_OK, "structured error inspection fixture failed");
        lua_setglobal(L, "oom_owner");
        for (const char* operation : {"message", "causes"}) {
            method(L, operation);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::IntResult::live() == 1 && native_allocations.load() == before,
                "failed error inspection publication damaged its native result");
            lua_pop(L, 1);
        }
        method(L, "value");
        const auto before_rethrow = native_allocations.load();
        fail_native_allocations = true;
        const int rethrow_status = lua_pcall(L, 1, 0, 0);
        fail_native_allocations = false;
        require(rethrow_status == LUA_ERRRUN && demo::IntResult::live() == 1 && native_allocations.load() == before_rethrow,
            "failed status rethrow leaked or damaged its stored error");
        lua_pop(L, 1);
        method(L, "message");
        require(lua_pcall(L, 1, 1, 0) == LUA_OK && lua_rawlen(L, -1) == text.size(), "stored error was lost after rethrow failure");
        lua_pop(L, 1);
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "structured error owner close failed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner"); lua_gc(L, LUA_GCCOLLECT);

        for (const char* factory : {"demo__make_call_default", "demo__make_call_number", "demo__make_call_text"}) {
            int successes = 0, failures = 0;
            for (int budget = 0; budget <= 4; ++budget) {
                function(L, factory);
                const bool number = std::strcmp(factory, "demo__make_call_number") == 0;
                const bool parsed = std::strcmp(factory, "demo__make_call_text") == 0;
                if (number) lua_pushinteger(L, 42);
                if (parsed) lua_pushliteral(L, "42");
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, number || parsed ? 1 : 0, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) {
                    ++successes;
                    lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                    require(lua_pcall(L, 1, 0, 0) == LUA_OK, "call factory allocation fixture close failed");
                } else { ++failures; require(status == LUA_ERRRUN, "unexpected call factory allocation error"); }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(demo::CallObject::live() == 0 && native_allocations.load() == before,
                    "partially constructed call object leaked native state");
            }
            require(successes && failures, "call object allocation sweep missed success/failure paths");
        }
        function(L, "demo__make_call_number"); lua_pushinteger(L, 42);
        const auto before_call_factory = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int call_factory_status = lua_pcall(L, 1, 1, 0);
        vm.allocation.fail_growth = false;
        require(call_factory_status == LUA_ERRMEM && demo::CallObject::live() == 0 &&
            native_allocations.load() == before_call_factory, "failed call factory publication leaked native objects");
        lua_pop(L, 1);
        for (int budget = 0; budget <= 4; ++budget) {
            function(L, "demo__overload_text"); payload(L, text);
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 1, 1, 0);
            native_allocation_budget = -1;
            require((status == LUA_OK || status == LUA_ERRRUN) && native_allocations.load() == before,
                "overloaded string call leaked native temporaries");
            lua_pop(L, 1);
        }
        function(L, "demo__overload_text"); payload(L, text);
        const auto before_overload = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int overload_status = lua_pcall(L, 1, 1, 0);
        vm.allocation.fail_growth = false;
        require(overload_status == LUA_ERRMEM && native_allocations.load() == before_overload,
            "overload result publication leaked native values");
        lua_pop(L, 1);

        int output_failures = 0, output_successes = 0;
        for (int budget = 0; budget <= 4; ++budget) {
            function(L, "demo__out_buffer"); lua_pushinteger(L, 8192); lua_pushinteger(L, 255);
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 2, 1, 0);
            native_allocation_budget = -1;
            if (status == LUA_OK) { ++output_successes; require(lua_rawlen(L, -1) == 8192, "incorrect output buffer length"); }
            else { ++output_failures; require(status == LUA_ERRRUN, "incorrect output-buffer failure status"); }
            require(native_allocations.load() == before, "pointer-to-pointer output leaked native storage");
            lua_pop(L, 1);
        }
        require(output_failures && output_successes, "output buffer sweep missed success/failure paths");
        function(L, "demo__out_buffer"); lua_pushinteger(L, 8192); lua_pushinteger(L, 255);
        const auto before_output = native_allocations.load();
        vm.allocation.fail_growth = true;
        const int output_status = lua_pcall(L, 2, 1, 0);
        vm.allocation.fail_growth = false;
        require(output_status == LUA_ERRMEM && native_allocations.load() == before_output,
            "failed output buffer publication leaked its native array/string");
        lua_pop(L, 1);
        payload(L, text); lua_setglobal(L, "buffer_payload");
        for (int budget = 0; budget <= 4; ++budget) {
            const auto before = native_allocations.load();
            require(luaL_dostring(L, "oom_owner = demo.demo__ByteBuffer.new(buffer_payload); "
                "oom_view = oom_owner:slice(0, #buffer_payload, false)") == LUA_OK, "buffer failure fixture setup failed");
            const auto with_owner = native_allocations.load();
            method(L, "resize"); lua_pushinteger(L, 16384); lua_pushinteger(L, 0);
            fail_native_allocations = true;
            const int resize_status = lua_pcall(L, 3, 0, 0);
            fail_native_allocations = false;
            require(resize_status == LUA_ERRRUN, "expected native resize allocation failure");
            lua_pop(L, 1);
            require(luaL_dostring(L, "assert(oom_view:read() == buffer_payload)") == LUA_OK,
                "failed resize invalidated a live view");
            for (const char* operation : {"read", "slice"}) {
                method(L, operation);
                const bool slice = std::strcmp(operation, "slice") == 0;
                if (slice) { lua_pushinteger(L, 0); lua_pushinteger(L, 1); lua_pushboolean(L, false); }
                vm.allocation.fail_growth = true;
                const int status = lua_pcall(L, slice ? 4 : 1, 1, 0);
                vm.allocation.fail_growth = false;
                require(status == LUA_ERRMEM && demo::ByteBuffer::live() == 1 && demo::ByteSlice::live() == 1 &&
                    native_allocations.load() == with_owner, "failed buffer/slice publication damaged ownership");
                lua_pop(L, 1);
            }
            method(L, "append"); payload(L, text);
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 2, 0, 0);
            native_allocation_budget = -1;
            if (status != LUA_OK) {
                lua_pop(L, 1);
                require(luaL_dostring(L, "assert(oom_view:read() == buffer_payload and oom_owner:read() == buffer_payload)") == LUA_OK,
                    "failed append altered bytes or invalidated its view");
            } else {
                require(luaL_dostring(L, "assert(oom_owner:size() == #buffer_payload * 2); "
                    "assert(not pcall(function() oom_view:read() end))") == LUA_OK, "successful append did not invalidate its view");
            }
            require(native_allocations.load() == with_owner, "buffer growth leaked native storage");
            require(luaL_dostring(L, "oom_view:close(); oom_owner:close(); oom_view = nil; oom_owner = nil; "
                "collectgarbage('collect'); collectgarbage('collect')") == LUA_OK, "buffer fixture close failed");
            require(native_allocations.load() == before, "buffer fixture leaked native allocations");
        }
        lua_pushnil(L); lua_setglobal(L, "buffer_payload");

        require(luaL_dostring(L, "oom_owner = demo.demo__TaggedRecord.new()") == LUA_OK, "record fixture creation failed");
        for (int budget = 0; budget <= 5; ++budget) {
            method(L, "set_integer"); lua_pushinteger(L, 42);
            require(lua_pcall(L, 2, 0, 0) == LUA_OK, "record reset failed");
            method(L, "set_text"); payload(L, text);
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 2, 0, 0);
            native_allocation_budget = -1;
            if (status != LUA_OK) {
                lua_pop(L, 1);
                require(luaL_dostring(L, "assert(oom_owner:integer() == 42)") == LUA_OK,
                    "failed union replacement lost its original value");
            }
            method(L, "set_integer"); lua_pushinteger(L, 42);
            require(lua_pcall(L, 2, 0, 0) == LUA_OK, "union reset after replacement failed");
            require(native_allocations.load() == before, "union replacement leaked native storage");
        }
        method(L, "set_text"); payload(L, text);
        require(lua_pcall(L, 2, 0, 0) == LUA_OK, "union clone fixture setup failed");
        for (int budget = 0; budget <= 8; ++budget) {
            method(L, "clone");
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 1, 1, 0);
            native_allocation_budget = -1;
            if (status == LUA_OK) {
                lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                require(lua_pcall(L, 1, 0, 0) == LUA_OK, "clone failure fixture close failed");
            }
            lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
            require(demo::TaggedRecord::live() == 1 && native_allocations.load() == before,
                "union clone allocation failure leaked or damaged its owner");
        }
        for (const char* operation : {"clone", "text"}) {
            method(L, operation);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::TaggedRecord::live() == 1 && native_allocations.load() == before,
                "failed union result publication leaked native storage");
            lua_pop(L, 1);
        }
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "record fixture close failed");
        require(luaL_dostring(L, "oom_owner = demo.demo__ValueStore.new(); oom_owner:scratch_integer(42)") == LUA_OK,
            "value store fixture creation failed");
        for (int budget = 0; budget <= 6; ++budget) {
            method(L, "scratch_pair"); lua_pushinteger(L, 7); payload(L, text);
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 3, 0, 0);
            native_allocation_budget = -1;
            if (status != LUA_OK) {
                lua_pop(L, 1);
                require(luaL_dostring(L, "assert(oom_owner:scratch_as_integer() == 42)") == LUA_OK,
                    "failed any replacement lost its original alternative");
            }
            require(luaL_dostring(L, "oom_owner:scratch_integer(42)") == LUA_OK, "any reset failed");
            require(native_allocations.load() == before, "any/tuple replacement leaked native storage");

            method(L, "put_text"); payload(L, text); payload(L, text);
            native_allocation_budget = budget;
            const int map_status = lua_pcall(L, 3, 0, 0);
            native_allocation_budget = -1;
            if (map_status != LUA_OK) {
                lua_pop(L, 1);
                method(L, "kind"); payload(L, text);
                require(lua_pcall(L, 2, 1, 0) == LUA_OK && lua_tointeger(L, -1) == 0,
                    "failed map insertion changed the store");
                lua_pop(L, 1);
            } else {
                method(L, "erase"); payload(L, text);
                require(lua_pcall(L, 2, 0, 0) == LUA_OK, "map reset failed");
            }
            require(native_allocations.load() == before, "map/variant insertion leaked native storage");
        }
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "store fixture close failed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner"); lua_gc(L, LUA_GCCOLLECT);
        require(demo::TaggedRecord::live() == 0 && demo::ValueStore::live() == 0, "advanced value fixtures leaked native storage");

        {
            feature_library::AsyncTask task(21, 0, false);
            int callbacks = 0;
            task.on_complete([&](int phase, int value) { require(phase == 1 && value == 42, "native async callback result"); ++callbacks; });
            require(task.wait(2000), "native async completion timed out");
            bool wrong_thread_rejected = false;
            std::thread caller([&] {
                try { task.dispatch(); }
                catch (const std::exception& error) {
                    wrong_thread_rejected = std::strstr(error.what(), "owning thread") != nullptr;
                }
            });
            caller.join();
            require(wrong_thread_rejected && callbacks == 0, "native worker dispatched an owner callback");
            require(task.dispatch() && callbacks == 1, "owner did not dispatch native completion");
        }
        int task_failures = 0, task_successes = 0;
        for (int budget = 0; budget <= 16; ++budget) {
            constructor(L, "demo__AsyncTask");
            lua_pushinteger(L, 21); lua_pushinteger(L, 60000); lua_pushboolean(L, false);
            const auto before = native_allocations.load();
            native_allocation_budget = budget;
            const int status = lua_pcall(L, 3, 1, 0);
            native_allocation_budget = -1;
            if (status == LUA_OK) {
                ++task_successes;
                lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                require(lua_pcall(L, 1, 0, 0) == LUA_OK, "partial-allocation task could not close");
            } else {
                ++task_failures;
                require(status == LUA_ERRRUN, "unexpected native task allocation error status");
            }
            lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
            require(demo::AsyncTask::live() == 0 && demo::AsyncTask::workers() == 0,
                "partial async construction left a task/worker alive");
            require(native_allocations.load() == before, "partial async construction leaked native state");
        }
        require(task_failures > 0 && task_successes > 0, "async allocation sweep missed success/failure paths");

        require(luaL_dostring(L, "do local values = {}; for i = 1, 64 do values[i] = i end; "
            "oom_owner = demo.demo__IntSeries.new(values) end") == LUA_OK,
            "cannot construct series failure fixture");
        for (int repetition = 0; repetition < 20; ++repetition) {
            for (const char* operation : {"times", "negative", "clone", "cursor", "text",
                "quotient", "remainder", "floor_quotient", "floor_modulo"}) {
                method(L, operation);
                const bool has_factor = std::strcmp(operation, "times") == 0 || std::strcmp(operation, "quotient") == 0 ||
                    std::strcmp(operation, "remainder") == 0 || std::strcmp(operation, "floor_quotient") == 0 ||
                    std::strcmp(operation, "floor_modulo") == 0;
                if (has_factor) lua_pushinteger(L, 2);
                const auto before = native_allocations.load();
                vm.allocation.fail_growth = true;
                const int status = lua_pcall(L, has_factor ? 2 : 1, 1, 0);
                vm.allocation.fail_growth = false;
                require(status == LUA_ERRMEM && demo::IntSeries::live() == 1 && demo::SeriesCursor::live() == 0,
                    "failed operator/cursor publication leaked or damaged native storage");
                require(native_allocations.load() == before, "failed operator/cursor publication leaked C++ allocations");
                lua_pop(L, 1);
            }
            method(L, "times"); lua_pushinteger(L, 2);
            const auto before = native_allocations.load();
            fail_native_allocations = true;
            const int status = lua_pcall(L, 2, 1, 0);
            fail_native_allocations = false;
            require(status == LUA_ERRRUN && native_allocations.load() == before && demo::IntSeries::live() == 1,
                "native operator allocation failure did not clean up");
            lua_pop(L, 1);
        }
        for (const char* operation : {"quotient", "remainder", "floor_quotient", "floor_modulo"}) {
            int failures = 0, successes = 0;
            for (int budget = 0; budget <= 8; ++budget) {
                method(L, operation); lua_pushinteger(L, -3);
                const auto before = native_allocations.load();
                native_allocation_budget = budget;
                const int status = lua_pcall(L, 2, 1, 0);
                native_allocation_budget = -1;
                if (status == LUA_OK) {
                    ++successes;
                    lua_getfield(L, -1, "close"); lua_pushvalue(L, -2);
                    require(lua_pcall(L, 1, 0, 0) == LUA_OK, "division result could not close");
                } else {
                    ++failures;
                    require(status == LUA_ERRRUN, "unexpected native division allocation error");
                }
                lua_settop(L, 0); lua_gc(L, LUA_GCCOLLECT);
                require(native_allocations.load() == before && demo::IntSeries::live() == 1,
                    "partial division construction damaged storage or leaked allocations");
            }
            require(failures && successes, "division allocation sweep missed success/failure paths");
        }
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "series fixture close failed");
        lua_pushnil(L); lua_setglobal(L, "oom_owner");
        lua_gc(L, LUA_GCCOLLECT);
        require(demo::IntSeries::live() == 0 && demo::SeriesCursor::live() == 0, "series fixture leaked native storage");

        for (int repetition = 0; repetition < 20; ++repetition) {
            function(L, "demo__make_diamond"); payload(L, text); lua_pushinteger(L, 42);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 2, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::EntityView::live() == 0,
                "failed hierarchy publication leaked its shared native owner");
            require(native_allocations.load() == before, "hierarchy publication leaked C++ allocations");
            lua_pop(L, 1);
        }

        function(L, "demo__make_diamond"); payload(L, text); lua_pushinteger(L, 42);
        require(lua_pcall(L, 2, 1, 0) == LUA_OK, "cannot construct hierarchy allocation fixture");
        lua_setglobal(L, "oom_owner");
        for (const char* cast : {"as_named", "as_value"}) {
            method(L, cast);
            const auto before = native_allocations.load();
            vm.allocation.fail_growth = true;
            const int status = lua_pcall(L, 1, 1, 0);
            vm.allocation.fail_growth = false;
            require(status == LUA_ERRMEM && demo::EntityView::live() == 1,
                "failed hierarchy cast publication damaged the native root");
            require(native_allocations.load() == before, "failed hierarchy cast leaked a native view");
            lua_pop(L, 1);
        }
        method(L, "close"); require(lua_pcall(L, 1, 0, 0) == LUA_OK, "hierarchy fixture close failed");
        require(demo::EntityView::live() == 0, "hierarchy fixture did not release native ownership");
        lua_pushnil(L); lua_setglobal(L, "oom_owner");
        lua_gc(L, LUA_GCCOLLECT);

        lua_pushcfunction(L, [](lua_State* state) -> int {
            if (!lua_checkstack(state, 256)) return luaL_error(state, "cannot reserve callback failure fixture stack");
            void* allocator = nullptr;
            lua_getallocf(state, &allocator);
            static_cast<AllocationControl*>(allocator)->fail_growth = true;
            return 0;
        });
        lua_setglobal(L, "fail_lua_now");
        require(luaL_dostring(L, "callback_identity = function(s) return s end; callback_arm = function() fail_lua_now() end; "
            "callback_allocate = function(s) callback_entered = true; fail_lua_now(); return string.rep(s, 8192) end; callback_entered = false") == LUA_OK,
            "callback error fixture setup failed");
        for (int repetition = 0; repetition < 20; ++repetition) {
            function(L, "demo__transform_after_notify"); lua_getglobal(L, "callback_arm"); payload(L, text); lua_getglobal(L, "callback_identity");
            const auto before = native_allocations.load();
            const auto entries = demo::callback_entries();
            const int status = lua_pcall(L, 3, 1, 0);
            vm.allocation.fail_growth = false;
            require(status != LUA_OK && demo::callback_entries() == entries + 1, "callback argument allocation failed before native invocation");
            require(native_allocations.load() == before, "callback argument allocation failure leaked C++ state");
            lua_pop(L, 1);

            function(L, "demo__transform_text"); lua_pushliteral(L, "a"); lua_getglobal(L, "callback_allocate");
            const auto before_body = native_allocations.load();
            lua_pushboolean(L, 0); lua_setglobal(L, "callback_entered");
            const int body_status = lua_pcall(L, 2, 1, 0);
            vm.allocation.fail_growth = false;
            lua_getglobal(L, "callback_entered"); const bool entered = lua_toboolean(L, -1); lua_pop(L, 1);
            require(body_status != LUA_OK && entered, "Lua callback body allocation failure was not exercised");
            require(native_allocations.load() == before_body, "Lua callback body allocation failure leaked C++ state");
            lua_pop(L, 1);
        }

        lua_pushcfunction(L, [](lua_State*) -> int {
            if (arm_native_from_callback) fail_native_allocations = true;
            return 0;
        });
        lua_setglobal(L, "fail_native_now");
        payload(L, text); lua_setglobal(L, "callback_payload");
        require(luaL_dostring(L, "demo.demo__store_text_callback(function() fail_native_now(); return callback_payload end)") == LUA_OK,
            "retained callback return failure fixture setup");
        function(L, "demo__fire_saved_text");
        const auto before_return = native_allocations.load();
        arm_native_from_callback = true;
        const int return_status = lua_pcall(L, 0, 1, 0);
        const bool armed = fail_native_allocations.exchange(false);
        arm_native_from_callback = false;
        require(armed && return_status == LUA_ERRRUN && native_allocations.load() == before_return,
            "C++ allocation failure while reading callback result did not clean up");
        lua_pop(L, 1);
        function(L, "demo__fire_saved_text"); check_balanced_call(L, 0, true);
        demo::clear_saved_text();

        const char* shutdown_fixture =
            "local owner = demo.demo__Counter.new(42, 'shutdown'); shutdown_borrow = owner:alias(); "
            "shutdown_shared = demo.demo__make_shared_counter(42); shutdown_share = demo.demo__share_counter(shutdown_shared); "
            "shutdown_move = demo.demo__make_move_only('shutdown'); "
            "shutdown_events = demo.demo__EventSource.new(); shutdown_events:set_callback(function(v) return v end); "
            "shutdown_hierarchy = demo.demo__make_scripted('shutdown', 41, function(v) return v + 1 end); "
            "shutdown_virtual = shutdown_hierarchy:as_value(); "
            "shutdown_series = demo.demo__IntSeries.new({20, 21}); shutdown_cursor = shutdown_series:cursor(); "
            "shutdown_task = demo.demo__AsyncTask.new(21, 60000, false); "
            "shutdown_task:on_complete(function() error('shutdown must not dispatch') end); "
            "shutdown_record = demo.demo__TaggedRecord.new(); shutdown_record:set_text(string.rep('union', 1000)); "
            "shutdown_store = demo.demo__ValueStore.new(); shutdown_store:put_text('map', string.rep('variant', 1000)); "
            "shutdown_store:scratch_pair(42, string.rep('tuple', 1000)); "
            "shutdown_buffer = demo.demo__ByteBuffer.new('shutdown'); shutdown_slice = shutdown_buffer:slice(0, 8, true); "
            "shutdown_call = demo.demo__make_call_text('42'); shutdown_call_view = shutdown_call:readonly(); "
            "shutdown_result = demo.demo__nested_result(); shutdown_status = demo.demo__status_result(true, string.rep('detail', 1000)); "
            "shutdown_unique = demo.demo__make_resource('unique', 11, false); "
            "local resource = demo.demo__make_resource('shared', 22, false); shutdown_resource_shared = demo.demo__SharedResource.new(); "
            "resource:promote_to(shutdown_resource_shared); resource:close(); shutdown_weak = shutdown_resource_shared:weak(); "
            "shutdown_generator = demo.demo__Generator.new(40, 3, string.rep('frame', 1000), -1); assert(shutdown_generator:advance()); "
            "shutdown_time = demo.demo__TimePoint.new(42, true); "
            "shutdown_concurrent = demo.demo__ConcurrentState.new(42, 7); "
            "local exchange=interop_a.interop_a__Record.new(string.rep('exchange',1000),42); shutdown_exchange=interop_b.interop_b__Record.from_ticket(exchange:ticket()); exchange:close(); "
            "shutdown_executor=demo.demo__AwaitExecutor.new(); shutdown_await=shutdown_executor:submit(40,2,string.rep('await',1000),false,function(v) return v+1 end); assert(shutdown_executor:run_one()); "
            "shutdown_class_record=demo.demo__CallbackRecord.new(string.rep('class',1000),42); shutdown_class_frame=demo.demo__ClassCallbackFrame.new(); "
            "shutdown_callable=demo.demo__make_native_capture(string.rep('capture',1000),20); shutdown_bound=demo.demo__make_native_bound(string.rep('bound',1000),20); "
            "shutdown_lua_callable=demo.demo__make_native_callback(function(v) return tostring(v) end); "
            "local stationary=demo.demo__NativeStationary.new(string.rep('immovable',1000),false); shutdown_stationary=stationary:readonly(); "
            "shutdown_boundary = demo.demo__NativeBoundary.new(demo.demo__BoundaryOperation.add, 1, string.rep('abi',1000)); "
            "local member = demo.demo__MemberObject.new(20, 21, string.rep('member',1000), true); shutdown_member_view = member:readonly(); "
            "shutdown_member_selector = demo.demo__MemberSelector.new(demo.demo__MemberChoice.label); "
            "shutdown_queue = demo.demo__Worklist.new(demo.demo__WorkOrder.queue); shutdown_queue:push(string.rep('queue',1000)); "
            "shutdown_priority = demo.demo__Worklist.new(demo.demo__WorkOrder.highest); shutdown_priority:push(string.rep('heap',1000)); "
            "shutdown_files = demo.demo__FileWorkspace.new(); shutdown_files:directories('nested'); shutdown_files:write('nested/file', 'shutdown'); "
            "shutdown_nested = demo.demo__NestedOwner.new(42); shutdown_nested_child = shutdown_nested:item(); "
            "demo.demo__store_callback(function(v) return v + 1 end)";
        require(luaL_dostring(L, shutdown_fixture) == LUA_OK, "shutdown fixture failed");
        require(demo::Counter::live() == 2 && demo::MoveOnly::live() == 1 && demo::EntityView::live() == 1 &&
            demo::IntSeries::live() == 1 && demo::SeriesCursor::live() == 1 && demo::AsyncTask::live() == 1 &&
            demo::TaggedRecord::live() == 1 && demo::ValueStore::live() == 1 &&
            demo::ByteBuffer::live() == 1 && demo::ByteSlice::live() == 1 && demo::CallObject::live() == 1 && demo::IntResult::live() == 2 &&
            demo::ResourceSlot::live() == 2 && demo::Generator::frames() == 1 && demo::Generator::bodies() == 1 && demo::TimePoint::live() == 1 &&
            demo::NestedOwner::live() == 1 && demo::NestedOwner::Item::live() == 1 && demo::FileWorkspace::live() == 1 && demo::Worklist::live() == 2 &&
            rosetta_example::exchange::live()==1 && rosetta_example::exchange::tickets()==1 &&
            demo::AwaitExecutor::live() == 1 && demo::AwaitTask::live() == 1 && demo::AwaitExecutor::frames() == 1 && demo::AwaitExecutor::bodies() == 1 &&
            demo::CallbackRecord::live() == 1 && demo::ClassCallbackFrame::live() == 1 &&
            demo::NativeCallable::live() == 3 && demo::NativeCallable::captures() == 1 && demo::NativeCallable::targets() == 1 &&
            demo::NativeStationary::live() == 1 && demo::NativeBoundary::live() == 1 && demo::ConcurrentState::live() == 1 && demo::MemberObject::live() == 1 && demo::MemberSelector::live() == 1,
            "shutdown fixture missing live objects");
        {
            method(L, "location", "shutdown_files");
            require(lua_pcall(L, 1, 1, 0) == LUA_OK, "shutdown workspace location unavailable");
            const std::string directory = lua_tostring(L, -1);
            lua_close(L); vm.state = nullptr;
            require(!std::filesystem::exists(directory), "VM shutdown left a workspace directory on disk");
        }
        require(demo::Counter::live() == 0 && demo::MoveOnly::live() == 0 && demo::EventSource::live() == 0 &&
            demo::EntityView::live() == 0 && demo::IntSeries::live() == 0 && demo::SeriesCursor::live() == 0 &&
            demo::AsyncTask::live() == 0 && demo::AsyncTask::workers() == 0 &&
            demo::TaggedRecord::live() == 0 && demo::ValueStore::live() == 0 &&
            demo::ByteBuffer::live() == 0 && demo::ByteSlice::live() == 0 && demo::CallObject::live() == 0 && demo::IntResult::live() == 0 &&
            demo::ResourceSlot::live() == 0 && demo::ResourceSlot::allocations() == demo::ResourceSlot::releases() &&
            demo::Generator::frames() == 0 && demo::Generator::bodies() == 0 && demo::Generator::allocations() == demo::Generator::releases() &&
            demo::TimePoint::live() == 0 && demo::NestedOwner::live() == 0 && demo::NestedOwner::Item::live() == 0 &&
            demo::FileWorkspace::live() == 0 && demo::FileWorkspace::cleanup_errors() == 0 && demo::Worklist::live() == 0 &&
            rosetta_example::exchange::live()==0 && rosetta_example::exchange::tickets()==0 &&
            demo::AwaitExecutor::live() == 0 && demo::AwaitTask::live() == 0 && demo::AwaitExecutor::frames() == 0 && demo::AwaitExecutor::bodies() == 0 &&
            demo::CallbackRecord::live() == 0 && demo::ClassCallbackFrame::live() == 0 &&
            demo::NativeCallable::live() == 0 && demo::NativeCallable::captures() == 0 && demo::NativeCallable::targets() == 0 &&
            demo::NativeStationary::live() == 0 && demo::NativeBoundary::live() == 0 && demo::ConcurrentState::live() == 0 && demo::ConcurrentState::workers() == 0 && demo::MemberObject::live() == 0 && demo::MemberSelector::live() == 0,
            "Lua shutdown did not destroy native objects");
        bool closed_callback_rejected = false;
        try { demo::fire_saved(41); }
        catch (const std::exception& error) { closed_callback_rejected = std::strstr(error.what(), "VM is closed") != nullptr; }
        require(closed_callback_rejected, "native retained callback touched a closed Lua VM");
        demo::clear_saved_on_worker(); // safe destruction on another thread after VM shutdown
        require(native_allocations.load() == initial_native, "Lua shutdown leaked native ownership/callback state");
        require(filesystem_descriptors() == initial_descriptors, "Lua shutdown leaked native file descriptors");
        std::cout << "Native values, objects, callbacks, allocation failures and shutdown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
