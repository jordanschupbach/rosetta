if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], "luaopen_demo")) end
local demo = require("demo")
local Counter, MoveOnly = demo.demo__Counter, demo.demo__MoveOnly
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local function collect() collectgarbage("collect"); collectgarbage("collect") end
assert(Counter.live() == 0 and MoveOnly.live() == 0)
assert(Counter.adjusted(40) == 42 and Counter.adjusted(20, 22) == 42)
rejects(function() Counter.adjusted(2147483647) end, 'adjustment overflow')
assert(demo.demo__share_counter(nil) == nil)

rejects(function() Counter.new(-1, string.rep("x", 1024)) end, "negative initial counter")
rejects(function() Counter.new() end, "expected")
do
    local object <close> = Counter.new(41)
    assert(object:get_label() == string.rep('c', 128))
    object:add()
    assert(object:value() == 42)
end
rejects(function() Counter.new(1, false) end, "string expected")
collect()
assert(Counter.live() == 0)

do
    local object <close> = Counter.new(20, "initial")
    assert(Counter.live() == 1 and object:value() == 20)
    object:add(22)
    assert(object:value() == 42)
    object:set_label("a\0b")
    assert(object:get_label() == "a\0b")
    assert(object.secret == nil and object.get_value_ == nil)
    rejects(function() object:fail() end, "counter failure")
    rejects(function() object:add(2147483647) end, "counter overflow")
    assert(object:value() == 42)
    local view <close> = object:readonly()
    assert(view:value() == 42 and demo.demo__counter_value(view) == 42)
    rejects(function() view:add(1) end, "readonly")
    rejects(function() view:set_label("bad") end, "readonly")
    rejects(function() demo.demo__counter_add(view, 1) end, "readonly")
    rejects(function() view:alias() end, "readonly")
    local nested <close> = view:readonly()
    assert(nested:value() == 42)
    local child <close> = object:point()
    child:set_x(20); child:set_y(22)
    assert(child:sum() == 42)
    local readonly_child <close> = view:readonly_point()
    assert(readonly_child:sum() == 42)
    rejects(function() readonly_child:set_x(0) end, "readonly")
    local moved <close> = MoveOnly.new("different class")
    rejects(function() demo.demo__inspect_move_only(moved) end, "not copy constructible")
    assert(moved:text() == "different class")
    rejects(function() object.value(moved) end, "expected class")
    rejects(function() object.value(nil) end, "expected class")
    local foreign = assert(io.tmpfile())
    rejects(function() object.value(foreign) end, "expected class")
    foreign:close()
    assert(getmetatable(object) == "protected Rosetta class")
    assert(object:maybe_self(false) == nil)
    local pointer <close> = object:maybe_self(true)
    assert(pointer:value() == 42)
    assert(demo.demo__counter_pointer(nil) == -1)
    assert(demo.demo__counter_pointer(pointer) == 42)
end
assert(Counter.live() == 0 and MoveOnly.live() == 0)

do
    local object = Counter.new(42, "root")
    local borrowed = object:alias()
    local child = object:point()
    local before = Counter.destroyed()
    object:close(); object:close()
    assert(Counter.destroyed() == before + 1)
    rejects(function() borrowed:value() end, "expired")
    rejects(function() child:sum() end, "expired")
    rejects(function() demo.demo__counter_value(object) end, "closed")
    borrowed:close(); borrowed:close(); child:close()
    object, borrowed, child = nil, nil, nil
    collect()
    assert(Counter.destroyed() == before + 1 and Counter.live() == 0)
end

do
    local object = Counter.new(42, "retained")
    local weak = setmetatable({object}, {__mode="v"})
    local borrowed = object:alias()
    object = nil
    collect()
    assert(weak[1] ~= nil and borrowed:value() == 42)
    borrowed:close()
    collect()
    assert(weak[1] == nil and Counter.live() == 0)
    rejects(function() borrowed:value() end, "closed")
end

do
    local object = Counter.new(42, "chain")
    local middle = object:alias()
    local leaf = middle:alias()
    object, middle = nil, nil
    collect()
    assert(leaf:value() == 42 and Counter.live() == 1)
    leaf:close()
    collect()
    assert(Counter.live() == 0)
    -- Parent headers may now be collected; a closed child must not dereference them.
    rejects(function() leaf:value() end, "closed")
end

do
    local object <close> = Counter.new(42, "intermediate owner")
    local middle = object:alias()
    local leaf <close> = middle:alias()
    middle:close()
    assert(object:value() == 42)
    rejects(function() leaf:value() end, "expired")
end

do
    local object <close> = Counter.new(42, "independent")
    local copied <close> = demo.demo__copy_counter(object)
    local clone <close> = object:clone()
    object:close()
    copied:add(1)
    assert(copied:value() == 43 and clone:value() == 42)
    local from_value <close> = demo.demo__make_counter_value(9)
    assert(from_value:value() == 9)
    local from_unique <close> = demo.demo__make_counter(10)
    assert(from_unique:value() == 10 and demo.demo__null_counter() == nil)
    rejects(function() demo.demo__share_counter(from_unique) end, "shared ownership")
end
assert(Counter.live() == 0)

do
    local first = demo.demo__make_shared_counter(20)
    local second = demo.demo__share_counter(first)
    local borrowed = first:alias()
    second:add(22)
    assert(first:value() == 42 and Counter.live() == 1)
    first:close()
    assert(second:value() == 42 and Counter.live() == 1)
    rejects(function() borrowed:value() end, "expired")
    borrowed:close(); second:close()
    assert(Counter.live() == 0)
end

rejects(function()
    local temporary <close> = Counter.new(42, "scope failure")
    error("scope failure")
end, "scope failure")
assert(Counter.live() == 0)

do
    local point <close> = demo.demo__Point.new()
    assert(point:get_x() == 0 and point:get_y() == 0)
    point:set_x(20); point:set_y(22)
    assert(point:sum() == 42 and point:get_version() == 1 and point.set_version == nil)
    point:set_x(2147483647); point:set_y(1)
    rejects(function() point:sum() end, "point overflow")
end

collect()
local before = Counter.destroyed()
for i=1,100 do
    local unused = Counter.new(i, "garbage collected")
    local alias = unused:alias()
    assert(alias:value() == i)
end
collect()
assert(Counter.live() == 0 and Counter.destroyed() == before + 100)
assert(MoveOnly.live() == 0)
print("Class ownership, borrowing, readonly access, close and GC passed")
