if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local identity = demo.demo__variant_identity
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local binary = string.rep('variant\0', 128)
local values = {{index=1}, {index=2, value=42}, {index=3, value=binary},
    {index=4, value=demo.demo__Operation.multiply}, {index=5}, {index=5, value=binary}, {index=6, value={1,2,42}}}
for _, value in ipairs(values) do
    local result = identity(value)
    assert(result ~= value and result.index == value.index)
    if type(value.value) == 'table' then assert(result.value[3] == 42 and result.value ~= value.value)
    else assert(result.value == value.value) end
end
for index=1,2 do
    local result = demo.demo__duplicate_variant({index=index, value=42})
    assert(result.index == index and result.value == 42) -- the two int alternatives stay distinct
end
assert(demo.demo__duplicate_variant({index=3, value=binary}).value == binary)
assert(demo.demo__const_variant({index=2, value=binary}).value == binary)
local vector = demo.demo__variant_vector(values)
assert(#vector == #values and vector[1].index == 1 and vector[5].index == 5 and vector[5].value == nil)
assert(demo.demo__optional_variant(nil) == nil)
assert(demo.demo__optional_variant({index=1}).index == 1)
local map = demo.demo__variant_map({empty={index=1}, optional={index=5}, text={index=3, value=binary}})
assert(map.empty.index == 1 and map.optional.index == 5 and map.text.value == binary)
local keys = demo.demo__variant_keys({{{index=1, value=42}, 'first'}, {{index=2, value=42}, 'second'}})
assert(#keys == 2)
local found = {}; for _, entry in ipairs(keys) do found[entry[1].index] = entry[2] end
assert(found[1] == 'first' and found[2] == 'second')
rejects(function() demo.demo__variant_keys({{{index=1, value=42}, 'first'}, {{index=1, value=42}, 'duplicate'}}) end, 'duplicate native map key')
local empties = demo.demo__monostate_map({{'a'}, {'b'}})
assert(#empties == 2 and empties[1][1] == 'a' and empties[1][2] == nil)
assert(select('#', demo.demo__monostate_identity(nil)) == 1 and demo.demo__monostate_identity(nil) == nil)
rejects(function() demo.demo__monostate_identity(false) end, 'nil expected for monostate')
local callback = demo.demo__variant_callback(function(value)
    assert(value.index == 3 and value.value == binary)
    return {index=2, value=42}
end, {index=3, value=binary})
assert(callback.index == 2 and callback.value == 42)
rejects(function() demo.demo__variant_callback(function() return {index=4, value=3} end, {index=1}) end, 'unknown enum value')
rejects(function() demo.demo__variant_callback(function() return {index=0} end, {index=1}) end, 'variant index out of range')
rejects(function() demo.demo__variant_callback(function() error('sum callback failure') end, {index=1}) end, 'sum callback failure')
rejects(function() demo.demo__variant_failure({index=3, value=binary}) end, 'variant failure')
assert(demo.demo__variant_oversized_result(binary, false).value == binary)
rejects(function() demo.demo__variant_oversized_result(binary, true) end, 'Lua integer range')
if demo.demo__variant_exception_is_valueless() then
    rejects(function() demo.demo__variant_exception() end, 'valueless variant')
else
    local preserved = demo.demo__variant_exception()
    assert(preserved.index == 1 and preserved.value == 42) -- native strong guarantee is also valid
end
assert(demo.demo__variant_view().value == string.rep('v',128))
do
    local source = {index=1, value='view:' .. string.rep('v',8192)}
    local copied = demo.demo__variant_view_after(source, function()
        source.value = nil; collectgarbage('collect')
        assert(demo.demo__variant_view({index=2, value=42}).value == 42)
    end)
    assert(copied == 'view:' .. string.rep('v',8192))
end
for _, bad in ipairs({{}, {index=0}, {index=-1}, {index=7}, {index=1.5}, {index=true}, {index='1'}}) do
    assert(not pcall(identity,bad))
end
rejects(function() identity(nil) end, 'variant table expected')
rejects(function() identity({index=1, extra=true}) end, 'variant keys')
rejects(function() identity({index=1, [1]='positional'}) end, 'variant keys')
rejects(function() identity({index=1, value=false}) end, 'nil expected for monostate')
rejects(function() identity({index=2}) end, 'number expected')
rejects(function() identity({index=3, value=42}) end, 'string expected')
rejects(function() identity({index=4, value=3}) end, 'unknown enum value')
local invoked = false
local raw = setmetatable({index=1}, {__index=function() invoked=true; error('index') end,
    __pairs=function() invoked=true; error('pairs') end, __len=function() invoked=true; error('len') end})
assert(identity(raw).index == 1 and not invoked)
assert(identity({index=3,value=binary}).value == binary)
print('Tagged variants, duplicate alternatives, monostate, optional slots, keys, callbacks and views passed')
