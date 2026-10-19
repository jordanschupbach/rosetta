#pragma once

#include <string_view>

namespace rosetta::detail {
[[maybe_unused]] inline constexpr std::string_view lua_callbacks = R"cpp(
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <tuple>

namespace {

struct rosetta_callback_state;
struct rosetta_callback_vm {
    const std::thread::id thread = std::this_thread::get_id();
    std::atomic<bool> alive{true};
    std::mutex mutex;
    rosetta_callback_state* pending = nullptr;
};
struct rosetta_callback_state {
    std::shared_ptr<rosetta_callback_vm> vm;
    lua_State* L;
    int function_ref = LUA_NOREF;
    int thread_ref = LUA_NOREF;
    std::atomic<bool> active{true};
    rosetta_callback_state* next = nullptr;
};

[[maybe_unused]] inline bool rosetta_release_callback_refs(rosetta_callback_state& state) noexcept {
    if (state.function_ref < 0 && state.thread_ref < 0) return true;
    // The originating coroutine may be suspended with a full stack. Failure
    // to reserve one slot defers release instead of longjmp-ing from a deleter.
    if (!lua_checkstack(state.L, 1)) return false;
    if (state.function_ref >= 0) { luaL_unref(state.L, LUA_REGISTRYINDEX, state.function_ref); state.function_ref = LUA_NOREF; }
    if (state.thread_ref >= 0) { luaL_unref(state.L, LUA_REGISTRYINDEX, state.thread_ref); state.thread_ref = LUA_NOREF; }
    return true;
}
[[maybe_unused]] inline void rosetta_delete_callback(rosetta_callback_state* state) noexcept {
    auto vm = state->vm;
    {
        std::lock_guard<std::mutex> lock(vm->mutex);
        if (vm->alive && std::this_thread::get_id() != vm->thread) {
            // Intrusive queue: off-thread release never allocates or touches Lua.
            state->next = vm->pending;
            vm->pending = state;
            return;
        }
    }
    if (vm->alive && !rosetta_release_callback_refs(*state)) {
        std::lock_guard<std::mutex> lock(vm->mutex);
        if (vm->alive) { state->next = vm->pending; vm->pending = state; return; }
    }
    delete state;
}
[[maybe_unused]] inline void rosetta_drain_callback_refs(const std::shared_ptr<rosetta_callback_vm>& vm) {
    if (vm->alive && std::this_thread::get_id() != vm->thread) throw rosetta_conversion_error("callback release requires the owning thread");
    rosetta_callback_state* pending;
    {
        std::lock_guard<std::mutex> lock(vm->mutex);
        pending = vm->pending;
        vm->pending = nullptr;
    }
    while (pending) {
        auto* next = pending->next;
        bool deferred = false;
        if (vm->alive && !rosetta_release_callback_refs(*pending)) {
            std::lock_guard<std::mutex> lock(vm->mutex);
            if (vm->alive) { pending->next = vm->pending; vm->pending = pending; deferred = true; }
        }
        if (!deferred) delete pending;
        pending = next;
    }
}

[[maybe_unused]] static char rosetta_callback_vm_key;
struct rosetta_callback_vm_token {
    const void* tag;
    std::shared_ptr<rosetta_callback_vm>* holder;
};
[[maybe_unused]] inline rosetta_callback_vm_token* rosetta_callback_vm_token_at(lua_State* L, int index) noexcept;
[[maybe_unused]] inline int rosetta_finalize_callback_vm(lua_State* L) noexcept {
    auto* token = rosetta_callback_vm_token_at(L, 1);
    if (!token || token->tag != &rosetta_callback_vm_key || !token->holder) return 0;
    auto vm = *token->holder;
    {
        std::lock_guard<std::mutex> lock(vm->mutex);
        vm->alive = false;
    }
    // No Lua calls after marking the VM dead. A native retained closure may
    // outlive this VM; it keeps only C++ state and rejects future invocation.
    rosetta_drain_callback_refs(vm);
    delete token->holder;
    token->holder = nullptr;
    return 0;
}
[[maybe_unused]] inline rosetta_callback_vm_token* rosetta_callback_vm_token_at(lua_State* L, int index) noexcept {
    if (lua_type(L, index) != LUA_TUSERDATA || lua_rawlen(L, index) != sizeof(rosetta_callback_vm_token)) return nullptr;
    const void* tag = nullptr;
    auto* memory = lua_touserdata(L, index);
    std::memcpy(&tag, memory, sizeof(tag));
    return tag == &rosetta_callback_vm_key ? static_cast<rosetta_callback_vm_token*>(memory) : nullptr;
}
struct rosetta_callback_vm_context {
    std::shared_ptr<rosetta_callback_vm>* holder;
};
[[maybe_unused]] inline int rosetta_callback_vm_thunk(lua_State* L) {
    auto* context = static_cast<rosetta_callback_vm_context*>(lua_touserdata(L, 1));
    auto* token = ::new(lua_newuserdatauv(L, sizeof(rosetta_callback_vm_token), 0)) rosetta_callback_vm_token{&rosetta_callback_vm_key, nullptr};
    lua_newtable(L);
    lua_pushcfunction(L, rosetta_finalize_callback_vm);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &rosetta_callback_vm_key);
    token->holder = context->holder; // commit only after all potentially failing Lua operations
    return 0;
}
[[maybe_unused]] inline std::shared_ptr<rosetta_callback_vm> rosetta_callback_vm_for(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &rosetta_callback_vm_key);
    auto* token = rosetta_callback_vm_token_at(L, -1);
    if (token && token->holder) {
        auto vm = *token->holder;
        lua_pop(L, 1);
        return vm;
    }
    lua_pop(L, 1);
    auto holder = std::make_unique<std::shared_ptr<rosetta_callback_vm>>(std::make_shared<rosetta_callback_vm>());
    rosetta_callback_vm_context context{holder.get()};
    lua_pushcfunction(L, rosetta_callback_vm_thunk);
    lua_pushlightuserdata(L, &context);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const rosetta_conversion_error error(lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "callback VM setup failed");
        lua_pop(L, 1);
        throw error;
    }
    auto vm = *holder;
    holder.release();
    return vm;
}
[[maybe_unused]] inline void rosetta_drain_callbacks(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &rosetta_callback_vm_key);
    auto* token = rosetta_callback_vm_token_at(L, -1);
    auto* holder = token ? token->holder : nullptr;
    lua_pop(L, 1);
    if (holder) rosetta_drain_callback_refs(*holder);
}

class rosetta_callback_scope {
    std::vector<std::shared_ptr<rosetta_callback_state>> temporary_;
public:
    void track(const std::shared_ptr<rosetta_callback_state>& state) { temporary_.push_back(state); }
    ~rosetta_callback_scope() {
        for (const auto& state : temporary_) {
            state->active = false;
            if (state->vm->alive) rosetta_release_callback_refs(*state);
        }
    }
};

[[maybe_unused]] inline int rosetta_register_callback_thunk(lua_State* L) {
    auto* state = static_cast<rosetta_callback_state*>(lua_touserdata(L, 1));
    lua_pushvalue(L, 2);
    state->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushthread(L);
    state->thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

template<class Signature> struct rosetta_callback;
template<class R, class... Args> struct rosetta_callback<R(Args...)> {
    struct Context {
        const rosetta_callback_state* state;
        const std::tuple<const rosetta_value_t<Args>*...>* arguments;
    };
    static int thunk(lua_State* L) {
        auto* context = static_cast<Context*>(lua_touserdata(L, 1));
        lua_rawgeti(L, LUA_REGISTRYINDEX, context->state->function_ref);
        luaL_checkstack(L, static_cast<int>(sizeof...(Args)) + 3, "callback argument stack exhausted");
        std::apply([&](const auto*... arguments) {
            (rosetta_codec<rosetta_value_t<Args>>::push(L, *arguments), ...);
        }, *context->arguments);
        lua_call(L, static_cast<int>(sizeof...(Args)), std::is_void_v<R> ? 0 : 1);
        return std::is_void_v<R> ? 0 : 1;
    }
    static R invoke(const std::shared_ptr<rosetta_callback_state>& state, const rosetta_value_t<Args>&... arguments) {
        if (std::this_thread::get_id() != state->vm->thread) throw rosetta_conversion_error("callback requires the owning thread");
        if (!state->vm->alive) throw rosetta_conversion_error("callback Lua VM is closed");
        if (!state->active) throw rosetta_conversion_error("callback call lifetime has expired");
        auto* L = state->L;
        rosetta_drain_callback_refs(state->vm);
        if (!lua_checkstack(L, 4)) throw rosetta_conversion_error("callback stack allocation failed");
        const int top = lua_gettop(L);
        const auto pointers = std::make_tuple(&arguments...);
        Context context{state.get(), &pointers};
        lua_pushcfunction(L, thunk);
        lua_pushlightuserdata(L, &context);
        if (lua_pcall(L, 1, std::is_void_v<R> ? 0 : 1, 0) != LUA_OK) {
            const rosetta_conversion_error error(lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "Lua callback failed with a non-string error");
            lua_settop(L, top);
            throw error;
        }
        if constexpr (!std::is_void_v<R>) {
            try {
                auto result = rosetta_read<R>(L, -1);
                lua_settop(L, top);
                return result;
            } catch (...) { lua_settop(L, top); throw; }
        }
    }
    static std::function<R(Args...)> read(lua_State* L, int index, bool retained, rosetta_callback_scope& scope) {
        if (lua_type(L, index) == LUA_TNIL) return {};
        if (lua_type(L, index) != LUA_TFUNCTION) throw rosetta_conversion_error(index, "function expected");
        auto state = std::shared_ptr<rosetta_callback_state>(new rosetta_callback_state{rosetta_callback_vm_for(L), L}, rosetta_delete_callback);
        lua_pushcfunction(L, rosetta_register_callback_thunk);
        lua_pushlightuserdata(L, state.get());
        lua_pushvalue(L, index);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const rosetta_conversion_error error(lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "callback reference allocation failed");
            lua_pop(L, 1);
            throw error;
        }
        if (!retained) scope.track(state);
        return [captured = std::move(state)](Args... arguments) -> R {
            auto state = captured; // survive native self-unregistration during the Lua call
            return invoke(state, arguments...);
        };
    }
};

} // namespace
)cpp";
} // namespace rosetta::detail
