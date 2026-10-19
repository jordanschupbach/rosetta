if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local directory = debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])") or "./"
local Generators = dofile(directory .. "../examples/demo_generators.lua")()
local Generator = demo.demo__Generator
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Generator.frames() == 0 and Generator.bodies() == 0)
rejects(function() Generator.new(0, -1, "", -1) end, "nonnegative")
rejects(function() Generator.new(0, 1, "", -2) end, "failure position")
rejects(function() Generator.new(0, 1, "", 2) end, "failure position")
do
    local source <close> = Generator.new(40, 3, "v\0", -1)
    assert(Generator.frames() == 1 and Generator.bodies() == 0)
    assert(not source:done() and not source:cancelled())
    rejects(function() source:value() end, "no current value")
    for i = 40, 42 do
        assert(source:advance() and source:value() == "v\0" .. i)
        assert(source:value() == "v\0" .. i and Generator.bodies() == 1)
    end
    assert(not source:advance() and not source:advance() and source:done())
    assert(Generator.bodies() == 0 and Generator.frames() == 1 and not source:cancel())
    rejects(function() source:value() end, "no current value")
    source:close(); source:close()
    assert(Generator.frames() == 0)
    rejects(function() source:advance() end, "closed")
end
do
    local empty <close> = Generator.new(0, 0, "", -1)
    assert(not empty:done() and not empty:advance() and empty:done())
end
for _, fail_after in ipairs({0, 1, 2}) do
    local source <close> = Generator.new(0, 2, "", fail_after)
    for i = 0, fail_after - 1 do assert(source:advance() and source:value() == tostring(i)) end
    rejects(function() source:advance() end, "native generator failure")
    assert(source:done() and Generator.bodies() == 0)
    rejects(function() source:advance() end, "native generator failure")
    rejects(function() source:value() end, "native generator failure")
end
do
    local source <close> = Generator.new(2147483647, 2, "", -1)
    assert(source:advance() and source:value() == "2147483647")
    rejects(function() source:advance() end, "integer overflow")
    assert(source:done())
end
for _, start in ipairs({false, true}) do
    local source <close> = Generator.new(0, 100, string.rep("x", 1000), -1)
    if start then assert(source:advance() and Generator.bodies() == 1) end
    assert(source:cancel() and source:done() and source:cancelled())
    assert(not source:advance() and not source:cancel())
    assert(Generator.frames() == 0 and Generator.bodies() == 0)
    rejects(function() source:value() end, "cancelled")
end
do
    local source <close> = Generator.new(-1, 3, "item:", -1)
    local values = {}
    for value in Generators.values(source) do values[#values + 1] = value end
    assert(#values == 3 and values[1] == "item:-1" and values[3] == "item:1")
    assert(source:done() and Generator.bodies() == 0)
end
-- A retained iterator keeps the Lua/native handle alive until exhausted/GC.
do
    local source = Generator.new(40, 3, "", -1)
    local iterator = Generators.values(source)
    source = nil
    collect()
    assert(iterator() == "40" and Generator.frames() == 1)
    iterator = nil
end
collect()
assert(Generator.frames() == 0 and Generator.bodies() == 0)
-- Lua yields happen after the bound native advance/value calls have returned.
local thread = coroutine.create(function()
    local source <close> = Generator.new(40, 3, "", -1)
    for value in Generators.values(source) do coroutine.yield(value) end
end)
local ok, value = coroutine.resume(thread)
assert(ok and value == "40" and Generator.bodies() == 1)
assert(coroutine.close(thread)) -- closes Lua local, destroys suspended C++ frame
assert(Generator.frames() == 0 and Generator.bodies() == 0)
do
    local abandoned = Generator.new(0, 100, string.rep("x", 1000), -1)
    assert(abandoned:advance())
end
collect()
assert(Generator.frames() == 0 and Generator.bodies() == 0 and Generator.allocations() == Generator.releases())
print("Lua native coroutine generators passed")
