if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local Slot, Shared, Weak = demo.demo__ResourceSlot, demo.demo__SharedResource, demo.demo__WeakResource
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Slot.live() == 0 and Slot.new == nil and Weak.new == nil)
local binary = string.rep("a\0b\255", 1000)
local allocated, released, deleted = Slot.allocations(), Slot.releases(), Slot.deleted()
rejects(function() demo.demo__make_resource(binary, 42, true) end, "construction failed")
assert(Slot.live() == 0 and Slot.allocations() == allocated + 1 and Slot.releases() == released + 1)
assert(Slot.deleted() == deleted) -- no complete object existed for the custom deleter
do
    local source <close> = demo.demo__make_resource(binary, 42, false)
    local target <close> = demo.demo__empty_resource()
    local occupied <close> = demo.demo__make_resource("occupied", 7, false)
    assert(source:aligned() and Slot.alignment() == 64 and source:deleter_tag() == 42)
    assert(source:text() == binary and not target:has_value())
    rejects(function() source:move_to(source) end, "itself")
    rejects(function() source:move_to(occupied) end, "not empty")
    local closed = demo.demo__empty_resource(); closed:close()
    rejects(function() source:move_to(closed) end, "closed")
    rejects(function() source:move_to({}) end, "expected class")
    assert(source:has_value() and source:text() == binary)
    local before = Slot.deleted()
    source:move_to(target)
    assert(not source:has_value() and target:has_value() and target:text() == binary)
    assert(target:aligned() and target:deleter_tag() == 42 and Slot.deleted() == before)
    rejects(function() source:text() end, "empty")
    rejects(function() source:move_to(occupied) end, "empty")
    source:close()
    assert(target:text() == binary)
    target:reset(); target:reset()
    assert(not target:has_value() and Slot.deleted() == before + 1 and Slot.last_deleter_tag() == 42)
end
assert(Slot.live() == 0)
do
    local source <close> = demo.demo__make_resource(binary, 99, false)
    local shared <close> = Shared.new()
    assert(not shared:has_value())
    local empty_weak <close> = shared:weak()
    assert(empty_weak:expired() and empty_weak:lock() == nil)
    source:promote_to(shared)
    assert(not source:has_value() and shared:has_value() and shared:deleter_tag() == 99)
    local another <close> = shared:clone()
    local weak <close> = shared:weak()
    local locked <close> = weak:lock()
    assert(not weak:expired() and locked:text() == binary and another:text() == binary)
    local extra <close> = demo.demo__make_resource("extra", 10, false)
    rejects(function() extra:promote_to(shared) end, "not empty")
    assert(extra:text() == "extra")
    shared:reset(); shared:reset(); another:close()
    assert(not weak:expired() and locked:deleter_tag() == 99)
    local before = Slot.deleted()
    locked:close()
    assert(weak:expired() and weak:lock() == nil and Slot.deleted() == before + 1)
    assert(Slot.last_deleter_tag() == 99)
    rejects(function() shared:text() end, "empty")
    weak:close(); weak:close()
    rejects(function() weak:lock() end, "closed")
end
-- Weak handles do not retain the resource through GC.
do
    local weak
    do
        local source <close> = demo.demo__make_resource("GC", -7, false)
        local shared = Shared.new()
        source:promote_to(shared)
        weak = shared:weak()
        shared = nil
    end
    collect()
    assert(weak:expired() and weak:lock() == nil and Slot.last_deleter_tag() == -7)
    weak:close()
end
collect()
assert(Slot.live() == 0 and Slot.allocations() == Slot.releases())
print("Lua ownership transfer/custom deleter/weak adapters passed")
