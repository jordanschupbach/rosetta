if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local State = demo.demo__ConcurrentState
local limits = State.limits()
local minimum, maximum = limits[1], limits[2]
local function rejects(fn, message)
    local ok, error = pcall(fn)
    assert(not ok and tostring(error):find(message, 1, true), tostring(error))
end
local original = demo.demo__thread_local_get()
demo.demo__thread_local_set(42)
for _, value in ipairs({-7, 0, 123}) do
    local probe = demo.demo__thread_local_probe(value)
    assert(probe[1] == 42 and probe[2] == 0 and probe[3] == value and probe[4] == 42)
end
do
    local state <close> = State.new(20, 0)
    assert(state:load() == 20 and state:add(22) == 20 and state:load() == 42)
    local failed = state:compare_exchange(20, 100)
    assert(failed[1] == false and failed[2] == 42 and state:load() == 42)
    local changed = state:compare_exchange(42, 100)
    assert(changed[1] == true and changed[2] == 42 and state:load() == 100)
    state:store(0)
    for _, delta in ipairs({1, -2, 0}) do
        local before = state:load()
        local completed = state:parallel_add(4, 200, delta)
        assert(#completed == 4)
        for _, count in ipairs(completed) do assert(count == 200) end
        assert(state:load() == before + 800 * delta and State.workers() == 0)
        assert(demo.demo__thread_local_get() == 42)
    end
    local empty = state:parallel_add(2, 0, 1)
    assert(#empty == 2 and empty[1] == 0 and empty[2] == 0)
    rejects(function() state:parallel_add(0, 10, 1) end, 'worker count')
    rejects(function() state:parallel_add(17, 10, 1) end, 'worker count')
    rejects(function() state:parallel_add(1, -1, 1) end, 'iterations')
    rejects(function() state:parallel_add(1, 10001, 1) end, 'iterations')
    state:store(maximum)
    rejects(function() state:add(1) end, 'atomic counter overflow')
    assert(state:load() == maximum)
    rejects(function() state:parallel_add(4, 2, 1) end, 'atomic counter overflow')
    assert(state:load() == maximum and State.workers() == 0)
    state:store(minimum)
    rejects(function() state:add(-1) end, 'atomic counter overflow')
    assert(state:load() == minimum)
    state:store(0)
    assert(state:add(minimum) == 0 and state:load() == minimum)
    state:store(0)
    assert(state:add(maximum) == 0 and state:load() == maximum)
    assert(state:add(minimum) == maximum and state:load() == minimum + maximum)
    assert(state:register_load() == 0)
    if State.register_bits() < 63 then
        local register_max = (1 << State.register_bits()) - 1
        state:register_store(register_max)
        assert(state:register_exchange(42) == register_max and state:register_load() == 42)
        rejects(function() state:register_store(register_max + 1) end, 'integer out of C++ range')
    else state:register_store(42) end
    rejects(function() state:register_store(-1) end, 'integer out of C++ range')
    assert(state:register_load() == 42)
    local shared <close> = state:share()
    assert(State.live() == 1)
    state:close()
    shared:store(41); assert(shared:add(1) == 41 and shared:load() == 42)
    rejects(function() state:load() end, 'closed')
end
do local discarded = State.new(0, 42) end
collectgarbage('collect')
demo.demo__thread_local_set(original)
assert(State.live() == 0 and State.workers() == 0)
print('Thread-local isolation, checked atomics, joined workers, volatile access and cleanup passed')
