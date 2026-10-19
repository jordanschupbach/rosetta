if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local function collect() collectgarbage("collect"); collectgarbage("collect") end

local increment = 2
assert(demo.demo__invoke_callback(function(v) return v + increment end, 40) == 42)
assert(demo.demo__invoke_callback(function(v) return v + 1 end) == 42)
rejects(function() demo.demo__invoke_callback(function() error('default callback failure') end) end, 'default callback failure')
assert(demo.demo__invoke_twice(function(v) return v + 1 end, 40) == 42)
assert(demo.demo__invoke_binary(function(a, b) return a + b end, 20, 22) == 42)
assert(demo.demo__compose_callbacks(function(v) return v + 1 end, function(v) return v * 2 end, 20) == 42)
local action_ran = false
demo.demo__run_action(function() action_ran = true end)
assert(action_ran)
assert(demo.demo__invoke_callback(function(v)
    return demo.demo__invoke_callback(function(w) return w + 1 end, v) + 1
end, 40) == 42)
local binary = "a\0b\255"
assert(demo.demo__transform_text(binary, function(s) return s .. "!" end) == binary .. "!")
local notified = false
assert(demo.demo__transform_after_notify(function() notified = true end, binary, function(s) return s .. "!" end) == binary .. "!" and notified)
local values = demo.demo__transform_numbers(function(v) return {v[3], v[2], v[1]} end, {1, 2, 42})
assert(values[1] == 42 and values[3] == 1)
local notification
demo.demo__notify(function(message) notification = message end, binary)
assert(notification == binary)
assert(demo.demo__c_style_callback(function(v) return v + 2 end, 40) == 42)
rejects(function() demo.demo__c_style_callback(function() error("C adapter callback error") end, 1) end, "C adapter callback error")
assert(demo.demo__catch_callback(function() error("caught by native C++") end, 1) == -1)

rejects(function() demo.demo__invoke_callback({}, 1) end, "function expected")
rejects(function() demo.demo__invoke_callback(function() return "1" end, 1) end, "number expected")
rejects(function() demo.demo__invoke_callback(function() return 1.5 end, 1) end, "integer expected")
rejects(function() demo.demo__invoke_callback(function() return 2147483648 end, 1) end, "range")
rejects(function() demo.demo__invoke_callback(function() error("Lua callback error") end, 1) end, "Lua callback error")
rejects(function() demo.demo__transform_numbers(function() return {1, "bad"} end, {1}) end, "number expected")
local stringified = false
rejects(function() demo.demo__invoke_callback(function()
    error(setmetatable({}, {__tostring = function() stringified = true; error("should not stringify") end}))
end, 1) end, "non-string error")
assert(not stringified)
local ran_on_worker = false
rejects(function() demo.demo__invoke_on_worker(function() ran_on_worker = true; return 1 end, 1) end, "owning thread")
assert(not ran_on_worker)

local co = coroutine.create(function()
    return demo.demo__invoke_callback(function() coroutine.yield("unsupported") end, 1)
end)
local ok, message = coroutine.resume(co)
assert(not ok and tostring(message):find("yield", 1, true))

local weak = setmetatable({}, {__mode="v"})
do
    local marker = {}
    weak[1] = marker
    rejects(function() demo.demo__compose_callbacks(function(v) return marker and v end, false, 1) end, "function expected")
end
collect()
assert(weak[1] == nil) -- later-argument errors release earlier callback references
do
    local marker = {increment = 2}
    weak[1] = marker
    demo.demo__store_callback(function(v) return v + marker.increment end)
end
collect()
assert(weak[1] ~= nil and demo.demo__fire_saved(40) == 42)
demo.demo__clear_saved()
collect()
assert(weak[1] == nil)

do
    local marker = {increment = 2}
    weak[1] = marker
    -- This fixture deliberately retains a call-only callback in native code.
    demo.demo__store_call_only(function(v) return v + marker.increment end)
end
collect()
assert(weak[1] == nil) -- expiry drops Lua references even while C++ keeps a copy
assert(demo.demo__invoke_callback(function(v)
    rejects(function() demo.demo__fire_saved(v) end, "lifetime has expired")
    return v
end, 42) == 42) -- a recycled registry reference cannot revive the stale callback
demo.demo__clear_saved()

do
    local marker = {increment = 2}
    weak[1] = marker
    demo.demo__store_callback(function(v) return v + marker.increment end)
end
demo.demo__clear_saved_on_worker() -- native destruction occurs off the Lua thread
collect()
assert(weak[1] == nil) -- wrapper exit drained the deferred registry releases

do
    local thread = coroutine.create(function()
        demo.demo__store_callback(function(v) return v + 2 end)
    end)
    assert(coroutine.resume(thread))
    weak[1] = thread
end
collect()
assert(weak[1] ~= nil and demo.demo__fire_saved(40) == 42)
demo.demo__clear_saved()
collect()
assert(weak[1] == nil) -- both the closure and originating coroutine were released

do
    local events <close> = demo.demo__EventSource.new()
    assert(not events:has_callback())
    events:set_callback(function(v) return v + 2 end)
    assert(events:has_callback() and events:emit(40) == 42)
    events:set_callback(function(v) events:clear_callback(); return v + 1 end)
    assert(events:emit(41) == 42 and not events:has_callback())
    events:set_callback(function() events:close(); return 0 end)
    rejects(function() events:emit(1) end, "active native calls")
    assert(events:has_callback())
    events:clear_callback()
    events:set_callback(nil)
    assert(not events:has_callback())
end
assert(demo.demo__EventSource.live() == 0)

do
    local counter <close> = demo.demo__Counter.new(40, "pinned")
    assert(counter:with_callback(function()
        rejects(function() counter:close() end, "active native calls")
        return 2
    end) == 42)
    rejects(function() counter:with_callback(function() counter:close(); return 0 end) end, "active native calls")
    assert(counter:value() == 42)
    local alias <close> = counter:alias()
    rejects(function() demo.demo__use_counter_callback(alias, function()
        counter:close(); return 0
    end) end, "active native calls")
    local child <close> = counter:point()
    rejects(function() demo.demo__use_point_callback(child, function()
        counter:close(); return 0
    end) end, "active native calls")
    assert(child:sum() == 0 and counter:value() == 42)
    assert(demo.demo__use_point_callback(child, function(v) return v + 42 end) == 42)
end
assert(demo.demo__Counter.live() == 0)

do
    local events = demo.demo__EventSource.new()
    events:set_callback(function(v) return v end)
end
collect()
assert(demo.demo__EventSource.live() == 0) -- noncapturing handlers do not retain their owner

do
    local events = demo.demo__EventSource.new()
    weak[1] = events
    local captured_owner = events
    events:set_callback(function(v) return captured_owner:has_callback() and v or 0 end)
end
collect()
assert(weak[1] ~= nil) -- retained self-captures require explicit unregister/close
weak[1]:close()
collect()
assert(weak[1] == nil and demo.demo__EventSource.live() == 0)
demo.demo__clear_saved()
demo.demo__clear_saved_text()
assert(demo.demo__invoke_callback(function(v) return v end, 42) == 42)
print("Callback lifetimes, errors, reentrancy, owner pins and thread rejection passed")
