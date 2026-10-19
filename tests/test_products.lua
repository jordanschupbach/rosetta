if arg and arg[1] then package.preload.demo = assert(package.loadlib(arg[1], 'luaopen_demo')) end
local demo = require('demo')
local identity = demo.demo__product_identity
local function rejects(fn, expected)
    local ok, message = pcall(fn)
    assert(not ok and tostring(message):find(expected, 1, true), tostring(message))
end
local binary = string.rep('product\0', 128)
local input = {binary, 42, 'optional\0text'}
local result = identity(input)
assert(result ~= input and result[1] == binary and result[2] == 42 and result[3] == input[3])
result[1] = 'changed'; assert(input[1] == binary)
local absent = identity({binary, -1})
assert(absent[1] == binary and absent[2] == -1 and absent[3] == nil)
local empty = demo.demo__empty_product({})
assert(type(empty) == 'table' and next(empty) == nil)
local pair = demo.demo__pair_identity({binary, true})
assert(pair[1] == binary and pair[2] == true)
pair[1] = 'Lua tables remain mutable' -- the native pair's const element is copied
local nested = demo.demo__nested_product({input, {{1, true}, {2, false}}})
assert(nested[1][1] == binary and nested[1][2] == 42 and nested[2][2][2] == false)
local vector = demo.demo__product_vector({input, {binary, 7}})
assert(#vector == 2 and vector[2][1] == binary and vector[2][2] == 7 and vector[2][3] == nil)
assert(demo.demo__optional_product(nil) == nil)
assert(demo.demo__optional_product(input)[3] == input[3])
assert(demo.demo__sparse_product(nil) == nil)
local engaged = demo.demo__sparse_product({})
assert(type(engaged) == 'table' and next(engaged) == nil) -- engaged product of empty optionals
local holes = demo.demo__sparse_product({[2]=binary, [3]=false})
assert(holes[1] == nil and holes[2] == binary and holes[3] == false)
local interior = demo.demo__sparse_product({[1]=42, [3]=true})
assert(interior[1] == 42 and interior[2] == nil and interior[3] == true)
local view = demo.demo__product_view(binary)
assert(view[1] == binary and view[2] == 42)
assert(demo.demo__product_view()[1] == string.rep('v', 128))
do
    local function string_pins()
        local count = 0
        for key, value in pairs(debug.getregistry()) do
            if type(key) == 'number' and type(value) == 'string' then count = count + 1 end
        end
        return count
    end
    local previous = string_pins()
    local borrowed = {'tuple:' .. string.rep('v', 8192), 42}
    local text = demo.demo__product_view_after(borrowed, function()
        assert(string_pins() == previous + 1)
        borrowed[1] = nil; collectgarbage('collect')
        -- A nested view-reading call exercises restoration of the enclosing pin scope.
        assert(demo.demo__view_identity('nested view') == 'nested view')
        assert(string_pins() == previous + 1)
    end)
    assert(text == 'tuple:' .. string.rep('v', 8192) .. '42')
    assert(string_pins() == previous)
    local sequence = {'vector:' .. string.rep('v', 8192), 'second:' .. string.rep('s', 8192)}
    local copied = demo.demo__vector_view_after(sequence, function()
        assert(string_pins() == previous + 2)
        sequence[1] = nil; sequence[2] = nil; collectgarbage('collect')
    end)
    assert(copied[1] == 'vector:' .. string.rep('v', 8192) and copied[2] == 'second:' .. string.rep('s', 8192))
    assert(string_pins() == previous)
    rejects(function() demo.demo__product_view_after({'error:' .. string.rep('v', 8192), 42}, function()
        error('view scope callback failure')
    end) end, 'view scope callback failure')
    assert(string_pins() == previous)
end
local constants = demo.demo__product_const({true, {binary, 'second'}, false})
assert(constants[1] == true and constants[2][1] == binary and constants[2][2] == 'second' and constants[3] == false)
assert(demo.demo__product_const({false, {'first', 'second'}})[3] == nil)
rejects(function() demo.demo__product_const({true, {binary, 42}, false}) end, 'string expected')
rejects(function() demo.demo__product_const({true, {binary, 'second'}, 1}) end, 'boolean expected')
local callback = demo.demo__product_callback(function(value)
    assert(value[1] == binary and value[2] == 42 and value[3] == input[3])
    value[2] = value[2] + 1
    value[3] = nil
    return value
end, input)
assert(callback[1] == binary and callback[2] == 43 and callback[3] == nil and input[2] == 42)
rejects(function() demo.demo__product_callback(function() return {binary, 'bad'} end, input) end, 'number expected')
rejects(function() demo.demo__product_callback(function() error('product callback error') end, input) end, 'product callback error')
rejects(function() demo.demo__product_callback(function() coroutine.yield() end, input) end, 'yield')
rejects(function() demo.demo__product_failure(input) end, 'product failure')
rejects(function() demo.demo__product_large_result(input) end, 'Lua integer range')
for _, bad in ipairs({
    {binary, 42, 'text', 'extra'}, {[0]='extra', [1]=binary, [2]=42},
    {[1]=binary, [2]=42, named='extra'}, {[1]=binary, [2]=42, [4]=true},
    {[1]=binary, [2]=42, [1.5]=true}, {['1']=binary, [2]=42},
    {[true]=binary, [1]=binary, [2]=42},
}) do rejects(function() identity(bad) end, 'product keys') end
rejects(function() identity({binary}) end, 'number expected')
rejects(function() identity({false, 'also invalid'}) end, 'string expected') -- declared slot order
rejects(function() identity({[2]=42}) end, 'string expected')
rejects(function() identity({binary, false}) end, 'number expected')
rejects(function() identity({binary, 42, false}) end, 'string expected')
rejects(function() identity('not a table') end, 'product table expected')
rejects(function() demo.demo__empty_product({1}) end, 'product keys')
rejects(function() demo.demo__pair_identity({binary, 1}) end, 'boolean expected')
rejects(function() demo.demo__nested_product({input, {{1, 'bad'}}}) end, 'boolean expected')
rejects(function() demo.demo__product_vector({input, {binary, 'bad'}}) end, 'number expected')
local invoked = false
local raw = setmetatable({binary, 42}, {__index=function() invoked=true; error('metamethod') end,
    __pairs=function() invoked=true; error('metamethod') end, __len=function() invoked=true; error('metamethod') end})
assert(identity(raw)[3] == nil and not invoked)
assert(identity({binary, 42})[2] == 42) -- recovery after every failure
print('Automatic tuple/pair products, optional slots, nesting and callbacks passed')
