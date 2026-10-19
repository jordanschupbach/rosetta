if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local Task, State = demo.demo__AsyncTask, demo.demo__TaskState
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Task.live() == 0 and Task.workers() == 0)
rejects(function() Task.new(21, -1, false) end, "delay")
rejects(function() Task.new(21, 60001, false) end, "delay")
rejects(function() Task.new(21, 0, 1) end, "boolean expected")

do
    local task <close> = Task.new(21, 60000, false)
    assert(task:state() == State.running and not task:ready() and not task:wait(0))
    rejects(function() task:result() end, "not ready")
    rejects(function() task:wait(-1) end, "timeout")
    rejects(function() task:wait(60001) end, "timeout")
    rejects(function() task:on_complete({}) end, "function expected")
    assert(not task:dispatch())
    assert(task:cancel() and not task:cancel())
    assert(task:wait(2000) and task:ready() and task:state() == State.cancelled)
    rejects(function() task:result() end, "cancelled")
    rejects(function() task:result() end, "cancelled")
    local delivered = 0
    task:on_complete(function(state, value)
        delivered = delivered + 1
        assert(state == State.cancelled and value == 0)
    end)
    assert(task:dispatch() and delivered == 1 and not task:dispatch())
end

do
    local task <close> = Task.new(21, 0, false)
    local origin = coroutine.running()
    local delivered = 0
    task:on_complete(function(state, value)
        assert(coroutine.running() == origin) -- worker never enters Lua
        assert(state == State.succeeded and value == 42)
        assert(task:result() == 42 and not task:dispatch())
        rejects(function() task:close() end, "active")
        task:on_complete(nil) -- safe self-unregistration
        delivered = delivered + 1
    end)
    assert(task:wait(2000) and task:state() == State.succeeded)
    assert(delivered == 0 and task:result() == 42 and task:result() == 42)
    assert(task:dispatch() and delivered == 1 and not task:dispatch())
    assert(not task:cancel())
    rejects(function() task:on_complete(function() end) end, "already dispatched")
end

for _, overflow in ipairs({false, true}) do
    local task <close> = Task.new(overflow and 2147483647 or 21, 0, not overflow)
    assert(task:wait(2000) and task:state() == State.failed)
    rejects(function() task:result() end, overflow and "overflow" or "worker failure")
    local delivered = false
    task:on_complete(function(state, value) delivered = true; assert(state == State.failed and value == 0) end)
    assert(task:dispatch() and delivered)
end

do
    local task <close> = Task.new(21, 0, false)
    local calls = 0
    task:on_complete(function() calls = calls + 1; error("async callback failure") end)
    assert(task:wait(2000))
    rejects(function() task:dispatch() end, "async callback failure")
    assert(calls == 1 and not task:dispatch() and task:result() == 42)
end

do
    local task <close> = Task.new(21, 60000, false)
    local marker = {}
    local weak = setmetatable({marker}, {__mode = "v"})
    task:on_complete((function(capture) return function() assert(capture) end end)(marker))
    marker = nil
    collect()
    assert(weak[1] ~= nil)
    task:on_complete(function() error("replaced callback") end)
    collect()
    assert(weak[1] == nil)
    task:on_complete(nil)
    task:close(); task:close() -- cancels and joins without invoking any callback
    assert(Task.workers() == 0)
    rejects(function() task:dispatch() end, "closed")
    rejects(function() task:ready() end, "closed")
end

-- Dropped pending tasks cancel/join during GC. No callbacks run in finalizers.
for _ = 1, 20 do
    local task <close> = Task.new(21, 0, false)
    local accepted = task:cancel()
    assert(task:wait(2000))
    if accepted then
        assert(task:state() == State.cancelled)
        rejects(function() task:result() end, "cancelled")
    else
        assert(task:state() == State.succeeded and task:result() == 42)
    end
end
do
    local task = Task.new(21, 60000, false)
    task:on_complete(function() error("GC must not dispatch") end)
    task = nil
end
collect()
assert(Task.live() == 0 and Task.workers() == 0)
print("Lua async tasks passed")
