if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local Owner, Item = demo.demo__NestedOwner, demo.demo__NestedOwner__Item
local Mode, Access = demo.demo__NestedOwner__Mode, demo.demo__NestedOwner__Access
local State = demo.demo__NestedOwner__Item__State
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
assert(Owner.live() == 0 and Item.live() == 0)
assert(Mode.ready == 1 and Mode.active == 2 and Access.read == 1 and Access.write == 2)
assert(demo.demo__NestedOwner__Item__Tags__Mark.present == 1)
assert(demo.demo__NestedOwner__Item__Tags == nil) -- class policy does not select the enum's owner
assert(demo.demo__NestedOwner__Secret == nil and demo.demo__NestedOwner__Hidden == nil)
assert(demo.demo__NestedOwner__Hidden__Visible == nil and demo.demo__NestedOwner__Hidden__Deep__Visible == nil)
do
    local independent <close> = Item.new(41)
    assert(Owner.live() == 0 and Item.live() == 1)
    assert(independent:value() == 41 and independent:shifted() == 42 and independent:shifted(-1) == 40)
    assert(independent:get_label() == string.rep('i', 128))
    independent:set_state(State.busy)
    assert(independent:state() == State.busy)
    independent:set_value(2147483647)
    rejects(function() independent:shifted() end, 'nested value overflow')
    assert(independent:value() == 2147483647)
    rejects(function() independent:set_state(3) end, 'unknown enum value')
end
assert(Owner.live() == 0 and Item.live() == 0)
rejects(function() Owner.new(-1) end, 'negative nested value')
rejects(function() Item.new(-1) end, 'negative nested value')
do
    local owner <close> = Owner.new(41, 'nested\0label')
    local child <close> = owner:item()
    local readonly <close> = owner:readonly_item()
    assert(Owner.live() == 1 and Item.live() == 1 and child:get_label() == 'nested\0label')
    assert(owner:mode() == Mode.ready and readonly:shifted() == 42)
    owner:set_mode(Mode.active)
    owner:set_access(Access.read | Access.write)
    assert(owner:mode() == Mode.active and owner:get_access() == 3)
    owner:set_access(0); assert(owner:get_access() == 0)
    rejects(function() owner:set_mode(0) end, 'unknown enum value')
    rejects(function() owner:set_access(4) end, 'unknown enum flag bits')
    child:set_value(42)
    assert(readonly:value() == 42)
    rejects(function() readonly:set_value(1) end, 'readonly')
    rejects(function() readonly:set_label('changed') end, 'readonly')
    assert(child:with_callback(function(value)
        rejects(function() child:close() end, 'active native calls')
        rejects(function() owner:close() end, 'active native calls')
        return value
    end) == 42)
    rejects(function() child:with_callback(function() owner:close(); return 0 end) end, 'active native calls')
    assert(child:value() == 42)
    owner:close(); owner:close()
    assert(Owner.live() == 0 and Item.live() == 0)
    rejects(function() child:value() end, 'closed')
    rejects(function() readonly:shifted() end, 'closed')
end
do
    local owner = Owner.new(42)
    local child = owner:item()
    owner = nil
    collectgarbage('collect'); collectgarbage('collect')
    assert(Owner.live() == 1 and child:value() == 42)
    child:close(); child = nil
    collectgarbage('collect'); collectgarbage('collect')
    assert(Owner.live() == 0 and Item.live() == 0)
end
local values = demo.demo__nested_modes({Mode.ready, Mode.active})
assert(values[1] == Mode.ready and values[2] == Mode.active)
rejects(function() demo.demo__nested_modes({Mode.ready, 3}) end, 'unknown enum value')
assert(demo.demo__nested_mode_callback(function(value) assert(value == Mode.ready); return Mode.active end, Mode.ready) == Mode.active)
rejects(function() demo.demo__nested_mode_callback(function() return 3 end, Mode.ready) end, 'unknown enum value')
collectgarbage('collect'); collectgarbage('collect')
assert(Owner.live() == 0 and Item.live() == 0)
print('Nested public classes/enums, declaration contexts, defaults, access and lifetimes passed')
