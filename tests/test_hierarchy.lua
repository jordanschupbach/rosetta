if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local Entity = demo.demo__EntityView
local function collect() collectgarbage("collect"); collectgarbage("collect") end
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Entity.live() == 0)
assert(Entity.new == nil and demo.demo__NamedView.new == nil and demo.demo__ValueView.new == nil)

-- One virtual Entity base, with crosscasts through two distinct base classes.
do
    local root <close> = demo.demo__make_diamond("a\0b", 42)
    local named <close> = root:as_named()
    local value <close> = root:as_value()
    local cross <close> = named:as_value()
    local left <close> = named:as_entity()
    local right <close> = value:as_entity()
    assert(Entity.live() == 1 and root:kind() == "diamond")
    assert(named:name() == "a\0b" and cross:value() == 42)
    assert(left:same_entity(right) and root:same_entity(left))
    cross:set_value(7)
    assert(value:value() == 7)
    rejects(function() value:set_value("bad") end, "number expected")
    rejects(function() value:set_value(2147483648) end, "range")
    rejects(function() root.same_entity(root, value) end, "expected class")
    -- Each facade owns a share; closing one facade cannot invalidate siblings.
    root:close()
    rejects(function() root:kind() end, "closed")
    assert(named:name() == "a\0b" and value:value() == 7 and Entity.live() == 1)
    left:close(); right:close(); named:close(); cross:close()
    assert(Entity.live() == 1)
    value:close(); value:close()
    assert(Entity.live() == 0)
end

do
    local named_only <close> = demo.demo__make_named("label")
    local name <close> = named_only:as_named()
    assert(named_only:kind() == "named" and name:name() == "label")
    assert(named_only:as_value() == nil and name:as_value() == nil)
    local other <close> = demo.demo__make_diamond("other", 0)
    assert(not other:same_entity(named_only))
end
assert(Entity.live() == 0)

-- A C++ virtual override delegates to a retained Lua closure; it is invoked
-- through Valued's virtual interface, not through a Lua-specific native class.
do
    local marker = {offset = 2}
    local weak = setmetatable({marker}, {__mode = "v"})
    local root = demo.demo__make_scripted("Lua virtual", 40,
        (function(capture) return function(seed) return seed + capture.offset end end)(marker))
    local view <close> = root:as_value()
    marker = nil
    root:close(); root = nil
    collect()
    assert(weak[1] ~= nil and view:value() == 42)
    view:set_value(5)
    assert(view:value() == 7)
    view:close()
    collect()
    assert(weak[1] == nil and Entity.live() == 0)
end

do
    local root <close> = demo.demo__make_scripted("failing", 1, function() error("virtual failure") end)
    local view <close> = root:as_value()
    rejects(function() view:value() end, "virtual failure")
    assert(root:kind() == "scripted") -- exceptions unwind safely through virtual dispatch
end
do
    local invalid = true
    local root <close> = demo.demo__make_scripted("strict result", 42, function(seed)
        if invalid then return "42" end
        return seed
    end)
    local view <close> = root:as_value()
    rejects(function() view:value() end, "number expected")
    invalid = false
    assert(view:value() == 42)
end
rejects(function() demo.demo__make_scripted("missing", 0, nil) end, "virtual override is required")
collect()
assert(Entity.live() == 0)

-- Active receiver pins prevent reentrant close of the virtual-call facade.
do
    local view
    local root <close> = demo.demo__make_scripted("reentrant", 42, function(seed)
        rejects(function() view:close() end, "active")
        return seed
    end)
    view = root:as_value()
    assert(view:value() == 42)
    view:close()
end
collect()
assert(Entity.live() == 0)

-- Native shared owners survive GC of a root facade and release on last close.
do
    local root = demo.demo__make_diamond("GC", 42)
    local view = root:as_value()
    root = nil
    collect()
    assert(Entity.live() == 1 and view:value() == 42)
    view = nil
end
collect()
assert(Entity.live() == 0)
print("Lua hierarchy adapters passed")
